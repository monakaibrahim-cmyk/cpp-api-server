#include "api/htaccess.h"
#include "api/logger.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sol/sol.hpp>
#include <sstream>

namespace api
{

namespace
{

std::string trim(const std::string &str)
{
    size_t first = str.find_first_not_of(" \t\r\n");

    if (first == std::string::npos)
    {
        return "";
    }

    size_t last = str.find_last_not_of(" \t\r\n");

    return str.substr(first, (last - first + 1));
}

std::vector<std::string> split_whitespace(const std::string &str)
{
    std::vector<std::string> tokens;
    std::string token;
    bool in_quotes = false;
    char quote_char = '"';

    for (size_t i = 0; i < str.size(); ++i)
    {
        char c = str[i];

        if ((c == '"' || c == '\'') && (!in_quotes || c == quote_char))
        {
            in_quotes = !in_quotes;
            quote_char = c;
        }
        else if ((c == ' ' || c == '\t') && !in_quotes)
        {
            if (!token.empty())
            {
                tokens.push_back(token);
                token.clear();
            }
        }
        else
        {
            token += c;
        }
    }

    if (!token.empty())
    {
        tokens.push_back(token);
    }

    return tokens;
}

std::string to_lower(const std::string &str)
{
    std::string res = str;

    std::transform(res.begin(), res.end(), res.begin(), [](unsigned char c)
                   { return static_cast<char>(std::tolower(c)); });

    return res;
}

} // namespace

htaccess_engine::htaccess_engine() = default;

htaccess_engine::~htaccess_engine() = default;

htaccess_engine &htaccess_engine::instance()
{
    static htaccess_engine s_instance;

    return s_instance;
}

void htaccess_engine::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);

    rules_.clear();
    header_rules_.clear();
    ip_rules_.clear();
    error_documents_.clear();
    order_ = "allow,deny";
    enabled_ = false;
}

void htaccess_engine::set_enabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);

    enabled_ = enabled;
}

bool htaccess_engine::is_enabled() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return enabled_;
}

void htaccess_engine::add_rewrite_rule(const htaccess_rule &rule)
{
    std::lock_guard<std::mutex> lock(mutex_);

    rules_.push_back(rule);
}

void htaccess_engine::add_header_rule(const htaccess_header_rule &rule)
{
    std::lock_guard<std::mutex> lock(mutex_);

    header_rules_.push_back(rule);
}

void htaccess_engine::add_ip_rule(const htaccess_ip_rule &rule)
{
    std::lock_guard<std::mutex> lock(mutex_);

    ip_rules_.push_back(rule);
}

void htaccess_engine::set_error_document(int code, const std::string &document)
{
    std::lock_guard<std::mutex> lock(mutex_);

    error_documents_[code] = document;
}

std::string htaccess_engine::get_error_document(int status_code) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto iterator = error_documents_.find(status_code);

    if (iterator != error_documents_.end())
    {
        return iterator->second;
    }

    return "";
}

void htaccess_engine::set_order(const std::string &order)
{
    std::lock_guard<std::mutex> lock(mutex_);

    order_ = to_lower(order);
}

bool htaccess_engine::file_exists(const std::string &path) const
{
    namespace fs = std::filesystem;

    std::error_code ec;

    return fs::exists(path, ec) && fs::is_regular_file(path, ec);
}

const std::string &htaccess_engine::source() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return source_;
}

void htaccess_engine::set_source(const std::string &src)
{
    std::lock_guard<std::mutex> lock(mutex_);

    source_ = src;
}

bool htaccess_engine::load_file(const std::string &path)
{
    if (!file_exists(path))
    {
        return false;
    }

    std::ifstream file(path);

    if (!file.is_open())
    {
        return false;
    }

    std::stringstream buffer;

    buffer << file.rdbuf();

    bool ok = load_string(buffer.str());

    if (ok)
    {
        set_source("htaccess_file");
    }

    return ok;
}

bool htaccess_engine::load_string(const std::string &content)
{
    std::lock_guard<std::mutex> lock(mutex_);

    rules_.clear();
    header_rules_.clear();
    ip_rules_.clear();
    error_documents_.clear();

    std::istringstream stream(content);
    std::string line;
    std::vector<htaccess_condition> pending_conds;

    while (std::getline(stream, line))
    {
        parse_line(line, pending_conds);
    }

    enabled_ = true;

    LOG_INFO("htaccess", "Loaded .htaccess rules: "
                             << rules_.size() << " rewrite rules, "
                             << header_rules_.size() << " header rules, "
                             << ip_rules_.size() << " IP rules");

    return true;
}

bool htaccess_engine::parse_line(const std::string &raw_line,
                                 std::vector<htaccess_condition> &pending_conds)
{
    std::string line = trim(raw_line);

    if (line.empty() || line[0] == '#')
    {
        return true;
    }

    auto tokens = split_whitespace(line);

    if (tokens.empty())
    {
        return true;
    }

    std::string directive = to_lower(tokens[0]);

    if (directive == "rewriteengine")
    {
        if (tokens.size() > 1 && to_lower(tokens[1]) == "on")
        {
            enabled_ = true;
        }
        else
        {
            enabled_ = false;
        }

        return true;
    }

    if (directive == "rewritecond")
    {
        if (tokens.size() < 3)
        {
            return false;
        }

        htaccess_condition cond;

        cond.test_string = tokens[1];
        std::string pattern = tokens[2];

        if (!pattern.empty() && pattern[0] == '!')
        {
            cond.negate = true;
            pattern = pattern.substr(1);
        }

        cond.pattern = pattern;

        if (tokens.size() > 3)
        {
            std::string flags = to_lower(tokens[3]);

            if (flags.find("nc") != std::string::npos)
            {
                cond.no_case = true;
            }

            if (flags.find("or") != std::string::npos)
            {
                cond.or_next = true;
            }
        }

        pending_conds.push_back(std::move(cond));

        return true;
    }

    if (directive == "rewriterule")
    {
        if (tokens.size() < 3)
        {
            return false;
        }

        htaccess_rule rule;

        rule.pattern = tokens[1];
        rule.substitution = tokens[2];
        rule.conditions = std::move(pending_conds);
        pending_conds.clear();

        if (tokens.size() > 3)
        {
            std::string flags = tokens[3];

            if (flags.front() == '[' && flags.back() == ']')
            {
                flags = flags.substr(1, flags.size() - 2);
            }

            std::stringstream flag_stream(flags);
            std::string flag;

            while (std::getline(flag_stream, flag, ','))
            {
                flag = trim(flag);
                std::string lflag = to_lower(flag);

                if (lflag == "nc")
                {
                    rule.no_case = true;
                }
                else if (lflag == "l")
                {
                    rule.last = true;
                }
                else if (lflag == "qsa")
                {
                    rule.query_string_append = true;
                }
                else if (lflag == "f")
                {
                    rule.forbidden = true;
                }
                else if (lflag == "g")
                {
                    rule.gone = true;
                }
                else if (lflag.rfind("r=", 0) == 0)
                {
                    try
                    {
                        rule.redirect_code = std::stoi(lflag.substr(2));
                    }
                    catch (...)
                    {
                        rule.redirect_code = 302;
                    }
                }
                else if (lflag == "r")
                {
                    rule.redirect_code = 302;
                }
            }
        }

        rules_.push_back(std::move(rule));

        return true;
    }

    if (directive == "redirect" || directive == "redirectmatch")
    {
        htaccess_rule rule;
        int status_code = 302;
        size_t idx = 1;

        if (tokens.size() >= 4)
        {
            try
            {
                status_code = std::stoi(tokens[1]);
                idx = 2;
            }
            catch (...)
            {
                status_code = 302;
            }
        }

        if (tokens.size() > idx + 1)
        {
            rule.pattern = tokens[idx];
            rule.substitution = tokens[idx + 1];
            rule.redirect_code = status_code;
            rule.last = true;

            rules_.push_back(std::move(rule));
        }

        return true;
    }

    if (directive == "header")
    {
        if (tokens.size() < 3)
        {
            return false;
        }

        htaccess_header_rule hrule;
        size_t idx = 1;

        if (to_lower(tokens[idx]) == "always")
        {
            hrule.always = true;
            idx++;
        }

        if (idx < tokens.size())
        {
            hrule.action = to_lower(tokens[idx]);
            idx++;
        }

        if (idx < tokens.size())
        {
            hrule.name = tokens[idx];
            idx++;
        }

        if (idx < tokens.size())
        {
            hrule.value = tokens[idx];
        }

        header_rules_.push_back(std::move(hrule));

        return true;
    }

    if (directive == "order")
    {
        if (tokens.size() > 1)
        {
            order_ = to_lower(tokens[1]);
        }

        return true;
    }

    if (directive == "deny")
    {
        if (tokens.size() > 2 && to_lower(tokens[1]) == "from")
        {
            htaccess_ip_rule ip_rule;

            ip_rule.rule_type = htaccess_ip_rule::type::deny;
            ip_rule.ip_or_cidr = tokens[2];

            ip_rules_.push_back(std::move(ip_rule));
        }

        return true;
    }

    if (directive == "allow")
    {
        if (tokens.size() > 2 && to_lower(tokens[1]) == "from")
        {
            htaccess_ip_rule ip_rule;

            ip_rule.rule_type = htaccess_ip_rule::type::allow;
            ip_rule.ip_or_cidr = tokens[2];

            ip_rules_.push_back(std::move(ip_rule));
        }

        return true;
    }

    if (directive == "require")
    {
        if (tokens.size() > 2 && to_lower(tokens[1]) == "ip")
        {
            htaccess_ip_rule ip_rule;

            ip_rule.rule_type = htaccess_ip_rule::type::allow;
            ip_rule.ip_or_cidr = tokens[2];

            ip_rules_.push_back(std::move(ip_rule));
        }
        else if (tokens.size() > 3 && to_lower(tokens[1]) == "not" &&
                 to_lower(tokens[2]) == "ip")
        {
            htaccess_ip_rule ip_rule;

            ip_rule.rule_type = htaccess_ip_rule::type::deny;
            ip_rule.ip_or_cidr = tokens[3];

            ip_rules_.push_back(std::move(ip_rule));
        }

        return true;
    }

    if (directive == "errordocument")
    {
        if (tokens.size() > 2)
        {
            try
            {
                int code = std::stoi(tokens[1]);
                std::string doc = tokens[2];

                for (size_t i = 3; i < tokens.size(); ++i)
                {
                    doc += " " + tokens[i];
                }

                error_documents_[code] = doc;
            }
            catch (...)
            {
            }
        }

        return true;
    }

    return true;
}

bool htaccess_engine::match_ip(const std::string &client_ip,
                               const std::string &pattern) const
{
    if (pattern == "all")
    {
        return true;
    }

    if (client_ip == pattern)
    {
        return true;
    }

    if (pattern.find('/') != std::string::npos)
    {
        std::string base_net = pattern.substr(0, pattern.find('/'));

        if (client_ip.rfind(base_net, 0) == 0)
        {
            return true;
        }
    }

    if (client_ip.rfind(pattern, 0) == 0)
    {
        return true;
    }

    return false;
}

std::string htaccess_engine::resolve_variable(
    const std::string &var, const std::string &method, const std::string &url,
    const std::string &remote_addr,
    const std::unordered_map<std::string, std::string> &headers) const
{
    if (var.rfind("%{", 0) != 0 || var.back() != '}')
    {
        return var;
    }

    std::string key = var.substr(2, var.size() - 3);
    std::string ukey = key;

    std::transform(ukey.begin(), ukey.end(), ukey.begin(), [](unsigned char c)
                   { return static_cast<char>(std::toupper(c)); });

    if (ukey == "REQUEST_METHOD")
    {
        return method;
    }

    if (ukey == "REQUEST_URI")
    {
        return url;
    }

    if (ukey == "REMOTE_ADDR")
    {
        return remote_addr;
    }

    if (ukey == "QUERY_STRING")
    {
        auto qpos = url.find('?');

        if (qpos != std::string::npos)
        {
            return url.substr(qpos + 1);
        }

        return "";
    }

    if (ukey.rfind("HTTP:", 0) == 0)
    {
        std::string header_name = key.substr(5);
        auto it = headers.find(header_name);

        if (it != headers.end())
        {
            return it->second;
        }

        for (const auto &pair : headers)
        {
            if (to_lower(pair.first) == to_lower(header_name))
            {
                return pair.second;
            }
        }
    }

    return "";
}

std::string htaccess_engine::apply_substitution(const std::string &sub,
                                                const std::smatch &match,
                                                const std::string &orig_url,
                                                bool qsa) const
{
    if (sub == "-")
    {
        return orig_url;
    }

    std::string res = sub;

    for (size_t i = 1; i < match.size(); ++i)
    {
        std::string placeholder1 = "$" + std::to_string(i);
        std::string placeholder2 = "%" + std::to_string(i);
        std::string rep = match[i].str();

        size_t pos = 0;

        while ((pos = res.find(placeholder1, pos)) != std::string::npos)
        {
            res.replace(pos, placeholder1.length(), rep);
            pos += rep.length();
        }

        pos = 0;

        while ((pos = res.find(placeholder2, pos)) != std::string::npos)
        {
            res.replace(pos, placeholder2.length(), rep);
            pos += rep.length();
        }
    }

    if (qsa)
    {
        auto qpos = orig_url.find('?');

        if (qpos != std::string::npos)
        {
            std::string qs = orig_url.substr(qpos + 1);

            if (!qs.empty())
            {
                if (res.find('?') == std::string::npos)
                {
                    res += "?" + qs;
                }
                else
                {
                    res += "&" + qs;
                }
            }
        }
    }

    return res;
}

htaccess_decision htaccess_engine::evaluate(
    const std::string &method, const std::string &url,
    const std::string &remote_addr,
    const std::unordered_map<std::string, std::string> &headers) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    htaccess_decision decision;

    decision.action = htaccess_action::pass;
    decision.status_code = 200;
    decision.rewritten_url = url;

    // 1. IP Access Control (Allow / Deny)
    if (!ip_rules_.empty())
    {
        bool allowed = true;

        if (order_ == "deny,allow")
        {
            allowed = true;

            for (const auto &r : ip_rules_)
            {
                if (r.rule_type == htaccess_ip_rule::type::deny &&
                    match_ip(remote_addr, r.ip_or_cidr))
                {
                    allowed = false;
                }
            }

            for (const auto &r : ip_rules_)
            {
                if (r.rule_type == htaccess_ip_rule::type::allow &&
                    match_ip(remote_addr, r.ip_or_cidr))
                {
                    allowed = true;
                }
            }
        }
        else
        {
            allowed = false;

            for (const auto &r : ip_rules_)
            {
                if (r.rule_type == htaccess_ip_rule::type::allow &&
                    match_ip(remote_addr, r.ip_or_cidr))
                {
                    allowed = true;
                }
            }

            for (const auto &r : ip_rules_)
            {
                if (r.rule_type == htaccess_ip_rule::type::deny &&
                    match_ip(remote_addr, r.ip_or_cidr))
                {
                    allowed = false;
                }
            }
        }

        if (!allowed)
        {
            decision.action = htaccess_action::forbidden;
            decision.status_code = 403;

            auto it = error_documents_.find(403);

            if (it != error_documents_.end())
            {
                decision.body = it->second;
            }
            else
            {
                decision.body =
                    "{\"error\":\"Access Forbidden by .htaccess policy\"}";
            }

            return decision;
        }
    }

    // 2. Evaluate Headers
    for (const auto &h : header_rules_)
    {
        if (h.action == "set" || h.action == "add")
        {
            decision.headers[h.name] = h.value;
        }
        else if (h.action == "append")
        {
            auto existing = decision.headers.find(h.name);

            if (existing != decision.headers.end())
            {
                existing->second += ", " + h.value;
            }
            else
            {
                decision.headers[h.name] = h.value;
            }
        }
        else if (h.action == "unset")
        {
            decision.headers.erase(h.name);
        }
    }

    // 3. Rewrite Rules
    std::string current_url = url;

    for (const auto &rule : rules_)
    {
        bool conditions_passed = true;

        if (!rule.conditions.empty())
        {
            bool group_result = true;
            bool in_or_chain = false;
            bool or_accumulator = false;

            for (size_t i = 0; i < rule.conditions.size(); ++i)
            {
                const auto &cond = rule.conditions[i];
                std::string val =
                    resolve_variable(cond.test_string, method, current_url,
                                     remote_addr, headers);

                bool cond_matched = false;

                try
                {
                    auto flags = std::regex_constants::ECMAScript;

                    if (cond.no_case)
                    {
                        flags |= std::regex_constants::icase;
                    }

                    std::regex re(cond.pattern, flags);

                    cond_matched = std::regex_search(val, re);
                }
                catch (...)
                {
                    cond_matched = false;
                }

                if (cond.negate)
                {
                    cond_matched = !cond_matched;
                }

                if (cond.or_next)
                {
                    in_or_chain = true;
                    or_accumulator = or_accumulator || cond_matched;
                }
                else if (in_or_chain)
                {
                    or_accumulator = or_accumulator || cond_matched;
                    group_result = group_result && or_accumulator;
                    in_or_chain = false;
                    or_accumulator = false;
                }
                else
                {
                    group_result = group_result && cond_matched;
                }
            }

            conditions_passed = group_result;
        }

        if (!conditions_passed)
        {
            continue;
        }

        std::smatch match;
        bool rule_matched = false;

        try
        {
            auto flags = std::regex_constants::ECMAScript;

            if (rule.no_case)
            {
                flags |= std::regex_constants::icase;
            }

            std::regex re(rule.pattern, flags);

            rule_matched = std::regex_search(current_url, match, re);
        }
        catch (...)
        {
            rule_matched = false;
        }

        if (!rule_matched)
        {
            continue;
        }

        if (rule.forbidden)
        {
            decision.action = htaccess_action::forbidden;
            decision.status_code = 403;

            auto it = error_documents_.find(403);

            if (it != error_documents_.end())
            {
                decision.body = it->second;
            }
            else
            {
                decision.body =
                    "{\"error\":\"Access Forbidden by RewriteRule [F]\"}";
            }

            return decision;
        }

        if (rule.gone)
        {
            decision.action = htaccess_action::gone;
            decision.status_code = 410;

            auto it = error_documents_.find(410);

            if (it != error_documents_.end())
            {
                decision.body = it->second;
            }
            else
            {
                decision.body = "{\"error\":\"410 Resource Gone\"}";
            }

            return decision;
        }

        std::string substituted = apply_substitution(
            rule.substitution, match, current_url, rule.query_string_append);

        if (rule.redirect_code > 0)
        {
            decision.action = htaccess_action::redirect;
            decision.status_code = rule.redirect_code;
            decision.headers["Location"] = substituted;

            return decision;
        }

        decision.action = htaccess_action::rewrite;
        current_url = substituted;
        decision.rewritten_url = current_url;

        if (rule.last)
        {
            break;
        }
    }

    return decision;
}

void htaccess_engine::bind_lua(sol::state &lua)
{
    lua.new_usertype<HtaccessDecision>(
        "htaccess_decision", sol::no_constructor, "action",
        [](const HtaccessDecision &decision) -> std::string
        {
            switch (decision.action)
            {
            case htaccess_action::pass:
                return "pass";
            case htaccess_action::rewrite:
                return "rewrite";
            case htaccess_action::redirect:
                return "redirect";
            case htaccess_action::forbidden:
                return "forbidden";
            case htaccess_action::gone:
                return "gone";
            case htaccess_action::custom:
                return "custom";
            }
            return "pass";
        },
        "status", &HtaccessDecision::status_code, "url",
        &HtaccessDecision::rewritten_url, "body", &HtaccessDecision::body,
        "headers",
        [](const HtaccessDecision &decision,
           sol::this_state lua_state_holder) -> sol::table
        {
            sol::state_view state_view(lua_state_holder);
            sol::table result_table = state_view.create_table();

            for (const auto &pair : decision.headers)
            {
                result_table[pair.first] = pair.second;
            }

            return result_table;
        });

    lua.new_usertype<htaccess_engine>(
        "htaccess_engine", sol::constructors<htaccess_engine()>(), "load_file",
        &htaccess_engine::load_file, "load_string",
        &htaccess_engine::load_string, "file_exists",
        &htaccess_engine::file_exists, "source", &htaccess_engine::source,
        "set_source", &htaccess_engine::set_source, "clear",
        &htaccess_engine::clear, "set_enabled", &htaccess_engine::set_enabled,
        "is_enabled", &htaccess_engine::is_enabled, "set_error_document",
        &htaccess_engine::set_error_document, "set_order",
        &htaccess_engine::set_order, "apply",
        [this](sol::table request_table) -> HtaccessDecision
        {
            std::string method =
                request_table.get_or<std::string>("method", "GET");
            std::string url = request_table.get_or<std::string>("url", "/");
            std::string remote_addr =
                request_table.get_or<std::string>("remote_addr", "127.0.0.1");

            std::unordered_map<std::string, std::string> headers;
            sol::optional<sol::table> headers_table = request_table["headers"];

            if (headers_table)
            {
                headers_table.value().for_each(
                    [&headers](sol::object key, sol::object value)
                    {
                        if (key.is<std::string>() && value.is<std::string>())
                        {
                            headers[key.as<std::string>()] =
                                value.as<std::string>();
                        }
                    });
            }

            return evaluate(method, url, remote_addr, headers);
        },
        "add_rule",
        [](htaccess_engine &self, const std::string &pattern,
           const std::string &substitution, sol::optional<sol::table> flags_opt)
        {
            HtaccessRule rule;

            rule.pattern = pattern;
            rule.substitution = substitution;

            if (flags_opt)
            {
                sol::table flags_table = flags_opt.value();

                rule.no_case = flags_table.get_or("nc", false) ||
                               flags_table.get_or("no_case", false);
                rule.last = flags_table.get_or("l", false) ||
                            flags_table.get_or("last", false);
                rule.query_string_append = flags_table.get_or("qsa", false);
                rule.forbidden = flags_table.get_or("f", false) ||
                                 flags_table.get_or("forbidden", false);
                rule.gone = flags_table.get_or("g", false) ||
                            flags_table.get_or("gone", false);

                sol::optional<int> redirect_optional = flags_table["r"];

                if (!redirect_optional)
                {
                    redirect_optional = flags_table["redirect"];
                }

                if (redirect_optional)
                {
                    rule.redirect_code = redirect_optional.value();
                }
            }

            self.add_rewrite_rule(rule);
        },
        "add_condition",
        [](htaccess_engine &self, const std::string &test_string,
           const std::string &pattern, sol::optional<sol::table> flags_opt)
        {
            HtaccessCondition condition;

            condition.test_string = test_string;
            condition.pattern = pattern;

            if (!condition.pattern.empty() && condition.pattern[0] == '!')
            {
                condition.negate = true;
                condition.pattern = condition.pattern.substr(1);
            }

            if (flags_opt)
            {
                sol::table flags_table = flags_opt.value();

                condition.no_case = flags_table.get_or("nc", false) ||
                                    flags_table.get_or("no_case", false);
                condition.or_next = flags_table.get_or("or", false);
            }

            HtaccessRule temp_rule;

            temp_rule.pattern = ".*";
            temp_rule.substitution = "-";
            temp_rule.conditions.push_back(condition);

            self.add_rewrite_rule(temp_rule);
        },
        "add_header",
        [](htaccess_engine &self, const std::string &action,
           const std::string &name, const std::string &value,
           sol::optional<bool> always_opt)
        {
            htaccess_header_rule hrule;

            hrule.action = action;
            hrule.name = name;
            hrule.value = value;
            hrule.always = always_opt.value_or(false);

            self.add_header_rule(hrule);
        },
        "deny",
        [](htaccess_engine &self, const std::string &ip_or_cidr)
        {
            htaccess_ip_rule r;

            r.rule_type = htaccess_ip_rule::type::deny;
            r.ip_or_cidr = ip_or_cidr;

            self.add_ip_rule(r);
        },
        "allow",
        [](htaccess_engine &self, const std::string &ip_or_cidr)
        {
            htaccess_ip_rule r;

            r.rule_type = htaccess_ip_rule::type::allow;
            r.ip_or_cidr = ip_or_cidr;

            self.add_ip_rule(r);
        });

    sol::table ht_tbl = lua.create_named_table("htaccess");

    ht_tbl["new"] = []() -> std::shared_ptr<htaccess_engine>
    { return std::make_shared<htaccess_engine>(); };

    ht_tbl["load_file"] = [this](const std::string &path) -> bool
    { return load_file(path); };

    ht_tbl["parse"] =
        [](const std::string &content) -> std::shared_ptr<htaccess_engine>
    {
        auto eng = std::make_shared<htaccess_engine>();

        eng->load_string(content);

        return eng;
    };

    ht_tbl["load_string"] = [this](const std::string &content) -> bool
    { return load_string(content); };

    ht_tbl["file_exists"] = [this](const std::string &path) -> bool
    { return file_exists(path); };

    ht_tbl["source"] = [this]() -> std::string { return source(); };

    ht_tbl["set_source"] = [this](const std::string &src) { set_source(src); };

    ht_tbl["clear"] = [this]() { clear(); };

    ht_tbl["is_enabled"] = [this]() -> bool { return is_enabled(); };

    ht_tbl["set_enabled"] = [this](bool enabled) { set_enabled(enabled); };

    ht_tbl["get_default"] = [this]() -> htaccess_engine & { return *this; };

    ht_tbl["apply"] = [this](sol::table req_tbl,
                             sol::this_state s) -> sol::table
    {
        sol::state_view lua(s);
        std::string method = req_tbl.get_or<std::string>("method", "GET");
        std::string url = req_tbl.get_or<std::string>("url", "/");
        std::string remote_addr =
            req_tbl.get_or<std::string>("remote_addr", "127.0.0.1");

        std::unordered_map<std::string, std::string> headers;
        sol::optional<sol::table> h_tbl = req_tbl["headers"];

        if (h_tbl)
        {
            h_tbl.value().for_each(
                [&headers](sol::object key, sol::object val)
                {
                    if (key.is<std::string>() && val.is<std::string>())
                    {
                        headers[key.as<std::string>()] = val.as<std::string>();
                    }
                });
        }

        htaccess_decision d = evaluate(method, url, remote_addr, headers);
        sol::table res = lua.create_table();

        switch (d.action)
        {
        case htaccess_action::pass:
            res["action"] = "pass";
            break;
        case htaccess_action::rewrite:
            res["action"] = "rewrite";
            break;
        case htaccess_action::redirect:
            res["action"] = "redirect";
            break;
        case htaccess_action::forbidden:
            res["action"] = "forbidden";
            break;
        case htaccess_action::gone:
            res["action"] = "gone";
            break;
        case htaccess_action::custom:
            res["action"] = "custom";
            break;
        }

        res["status"] = d.status_code;
        res["url"] = d.rewritten_url;
        res["body"] = d.body;

        sol::table hres = lua.create_table();

        for (const auto &pair : d.headers)
        {
            hres[pair.first] = pair.second;
        }

        res["headers"] = hres;

        return res;
    };

    ht_tbl["deny"] = [this](const std::string &ip_or_cidr)
    {
        htaccess_ip_rule r;

        r.rule_type = htaccess_ip_rule::type::deny;
        r.ip_or_cidr = ip_or_cidr;

        add_ip_rule(r);
    };

    ht_tbl["allow"] = [this](const std::string &ip_or_cidr)
    {
        htaccess_ip_rule r;

        r.rule_type = htaccess_ip_rule::type::allow;
        r.ip_or_cidr = ip_or_cidr;

        add_ip_rule(r);
    };

    ht_tbl["header"] = [this](const std::string &name, const std::string &value,
                              sol::optional<std::string> action_opt)
    {
        htaccess_header_rule r;

        r.action = action_opt.value_or("set");
        r.name = name;
        r.value = value;

        add_header_rule(r);
    };

    ht_tbl["set_error_document"] = [this](int code, const std::string &document)
    { set_error_document(code, document); };

    ht_tbl["set_order"] = [this](const std::string &order)
    { set_order(order); };
}

} // namespace api
