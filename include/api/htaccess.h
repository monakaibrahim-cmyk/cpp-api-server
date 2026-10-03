#pragma once

#include <memory>
#include <mutex>
#include <regex>
#include <string>
#include <unordered_map>
#include <vector>

namespace sol
{
class state;
} // namespace sol

namespace api
{

/**
 * @brief Policy outcome action evaluated by the htaccess engine for an incoming
 * request.
 */
enum class htaccess_action
{
    /** @brief Request is permitted to continue to routing unmodified. */
    pass,

    /** @brief Request URI was rewritten internally before routing. */
    rewrite,

    /** @brief Server returns an immediate HTTP redirect response (301, 302,
       307, 308). */
    redirect,

    /** @brief Access is forbidden; server returns an immediate HTTP 403
       Forbidden. */
    forbidden,

    /** @brief Resource is gone; server returns an immediate HTTP 410 Gone. */
    gone,

    /** @brief Custom HTTP status code returned directly. */
    custom
};

/**
 * @brief Represents the final evaluation decision for an incoming HTTP request.
 *
 * @details Emitted by @ref htaccess_engine::evaluate after matching incoming
 * URL, client IP address, and headers against active rewrite rules, IP
 * policies, and header filters.
 */
struct HtaccessDecision
{
    /** @brief Policy action decided by the rules. */
    htaccess_action action = htaccess_action::pass;

    /** @brief HTTP response status code (e.g. 200, 301, 403, 404, 410). */
    int status_code = 200;

    /** @brief Target rewritten URL if action is rewrite or redirect. */
    std::string rewritten_url;

    /** @brief Custom response body payload to return if forbidden or error. */
    std::string body;

    /** @brief Set of HTTP response headers to attach to the outgoing response.
     */
    std::unordered_map<std::string, std::string> headers;
};

/// Backward compatibility alias
using htaccess_decision = HtaccessDecision;

/**
 * @brief Precondition test clause evaluated before applying a rewrite rule.
 *
 * @details Emulates Apache @c RewriteCond directives. Supports server variable
 * interpolation (e.g. "%{REQUEST_METHOD}", "%{HTTP_USER_AGENT}"), regular
 * expression matching, case insensitivity ([NC]), logical OR chaining ([OR]),
 * and negation (!pattern).
 */
struct HtaccessCondition
{
    /** @brief Test string subject to pattern matching (e.g.
     * "%{REQUEST_METHOD}"). */
    std::string test_string;

    /** @brief Regular expression pattern to match against test_string. */
    std::string pattern;

    /** @brief Case-insensitive matching enabled ([NC] flag). */
    bool no_case = false;

    /** @brief Logical OR chaining with the subsequent condition ([OR] flag). */
    bool or_next = false;

    /** @brief Inverts match result (!pattern). */
    bool negate = false;
};

/// Backward compatibility alias
using htaccess_condition = HtaccessCondition;

/**
 * @brief Rewrite rule specifying regex pattern, substitution, preconditions,
 * and flags.
 *
 * @details Emulates Apache @c RewriteRule directives. When matched, can perform
 * internal URL rewrites, external HTTP redirects, or terminal access denials.
 */
struct HtaccessRule
{
    /** @brief Regular expression matching incoming URL path. */
    std::string pattern;

    /** @brief Replacement substitution path (supports $1, $2 capture
     * references) or "-" for no change. */
    std::string substitution;

    /** @brief Preconditions that must all evaluate to true before rule fires.
     */
    std::vector<HtaccessCondition> conditions;

    /** @brief Case-insensitive regular expression matching ([NC] flag). */
    bool no_case = false;

    /** @brief Terminates rule evaluation if this rule matches ([L] flag). */
    bool last = false;

    /** @brief Appends incoming query string to substituted URL ([QSA] flag). */
    bool query_string_append = false;

    /** @brief Immediately rejects request with HTTP 403 Forbidden ([F] flag).
     */
    bool forbidden = false;

    /** @brief Immediately rejects request with HTTP 410 Gone ([G] flag). */
    bool gone = false;

    /** @brief Emits HTTP redirect with specified status code (e.g. 301, 302)
     * ([R=301] flag). */
    int redirect_code = 0;
};

/// Backward compatibility alias
using htaccess_rule = HtaccessRule;

/**
 * @brief Header manipulation directive adding, setting, or removing HTTP
 * response headers.
 *
 * @details Emulates Apache @c Header directives (e.g. "Header set
 * X-Frame-Options DENY").
 */
struct HtaccessHeaderRule
{
    /** @brief Action to perform: "set", "add", "unset", or "append". */
    std::string action;

    /** @brief HTTP response header name (e.g. "X-Content-Type-Options"). */
    std::string name;

    /** @brief HTTP response header value. */
    std::string value;

    /** @brief Applies header unconditionally even on error responses when true
     * ("always" flag). */
    bool always = false;
};

/// Backward compatibility alias
using htaccess_header_rule = HtaccessHeaderRule;

/**
 * @brief Client IP address filtering rule for access control.
 *
 * @details Emulates Apache @c Allow and @c Deny directives supporting single
 * IPs, CIDR subnet blocks (e.g. "192.168.1.0/24"), and "all".
 */
struct HtaccessIpRule
{
    /** @brief Grant or deny access. */
    enum class type
    {
        allow,
        deny
    };

    /** @brief Rule action type (allow or deny). */
    type rule_type = type::allow;

    /** @brief Client IP address, subnet CIDR, or "all". */
    std::string ip_or_cidr;
};

/// Backward compatibility alias
using htaccess_ip_rule = HtaccessIpRule;

/**
 * @brief Engine evaluating Apache .htaccess directives, rewrite rules, and
 * access policies.
 *
 * @details Evaluates incoming requests through a multi-stage security pipeline:
 * 1. IP filtering (Order Allow,Deny or Deny,Allow).
 * 2. Header mutations (Header set/add/append/unset).
 * 3. URL rewriting & redirection (RewriteRule with RewriteCond).
 * 4. Custom error documents (ErrorDocument).
 *
 * Rules can be sourced from a physical @c .htaccess file on disk, or configured
 * dynamically in Lua route scripts via the global @c htaccess builder table.
 *
 * Example .htaccess file:
 * @code{.text}
 * RewriteEngine On
 * RewriteCond %{REQUEST_METHOD} POST
 * RewriteRule ^/legacy/(.*) /api/v2/$1 [R=301,L]
 * Header set X-Frame-Options "DENY"
 * @endcode
 *
 * Example Lua usage:
 * @code{.lua}
 * htaccess.rule("^/admin", "-", { forbidden = true })
 * htaccess.header("set", "X-Frame-Options", "DENY")
 * @endcode
 */
class htaccess_engine
{
  public:
    htaccess_engine();
    ~htaccess_engine();

    /**
     * @brief Accesses the global singleton instance of htaccess_engine.
     *
     * @return htaccess_engine& Reference to global engine.
     */
    static htaccess_engine &instance();

    /**
     * @brief Reads and parses an Apache .htaccess file from disk.
     *
     * @param[in] file_path Filesystem path to the .htaccess file.
     * @return true If file was loaded and parsed; false if file could not be
     * opened.
     */
    bool load_file(const std::string &file_path);

    /**
     * @brief Parses .htaccess directives directly from a string buffer.
     *
     * @param[in] configuration_content String containing Apache configuration
     * lines.
     * @return true If parsed successfully; false on unrecoverable syntax error.
     */
    bool load_string(const std::string &configuration_content);

    /**
     * @brief Tests if a referenced file exists on disk.
     *
     * @param[in] file_path Target file path.
     * @return true If file exists; false otherwise.
     */
    bool file_exists(const std::string &file_path) const;

    /**
     * @brief Returns active policy source identifier ("lua_script" or
     * "htaccess_file").
     *
     * @return const std::string& Source name.
     */
    const std::string &source() const;

    /**
     * @brief Sets active policy source identifier.
     *
     * @param[in] source_name Source identifier string.
     */
    void set_source(const std::string &source_name);

    /**
     * @brief Resets all rules, conditions, headers, and error documents.
     */
    void clear();

    /**
     * @brief Enables or disables rule evaluation in the HTTP pipeline.
     *
     * @param[in] enabled Master toggle flag.
     */
    void set_enabled(bool enabled);

    /**
     * @brief Checks if rule evaluation is currently active.
     *
     * @return true If enabled; false otherwise.
     */
    bool is_enabled() const;

    /**
     * @brief Appends a rewrite rule to the evaluation pipeline.
     *
     * @param[in] rule Rewrite rule structure.
     */
    void add_rewrite_rule(const HtaccessRule &rule);

    /**
     * @brief Appends a response header manipulation rule.
     *
     * @param[in] rule Header rule structure.
     */
    void add_header_rule(const HtaccessHeaderRule &rule);

    /**
     * @brief Appends an IP access control rule.
     *
     * @param[in] rule IP rule structure.
     */
    void add_ip_rule(const HtaccessIpRule &rule);

    /**
     * @brief Associates a custom HTML/JSON error document with an HTTP status
     * code.
     *
     * @param[in] status_code Target HTTP status (e.g. 403, 404, 500).
     * @param[in] document_body Custom document response string.
     */
    void set_error_document(int status_code, const std::string &document_body);

    /**
     * @brief Retrieves a custom error document for an HTTP status code if
     * configured.
     *
     * @param[in] status_code Target HTTP status.
     * @return std::string Document body string, or empty if none configured.
     */
    std::string get_error_document(int status_code) const;

    /**
     * @brief Configures IP evaluation priority order.
     *
     * @param[in] evaluation_order Either "allow,deny" or "deny,allow".
     */
    void set_order(const std::string &evaluation_order);

    /**
     * @brief Evaluates an incoming HTTP request against active rules and
     * returns policy decision.
     *
     * @param[in] method HTTP request method (e.g. "GET", "POST").
     * @param[in] url Request URI path.
     * @param[in] remote_address Client IP address.
     * @param[in] headers Map of incoming HTTP request headers.
     * @return HtaccessDecision Action decision, rewritten URL, and headers.
     */
    HtaccessDecision
    evaluate(const std::string &method, const std::string &url,
             const std::string &remote_address,
             const std::unordered_map<std::string, std::string> &headers) const;

    /**
     * @brief Binds the htaccess engine and rule builder into a Lua state.
     *
     * @param[in,out] lua_state Reference to Sol2 state.
     */
    void bind_lua(sol::state &lua_state);

  private:
    mutable std::mutex mutex_;
    bool enabled_ = false;
    std::string order_ = "allow,deny";
    std::string source_ = "lua_script";
    std::vector<HtaccessRule> rules_;
    std::vector<HtaccessHeaderRule> header_rules_;
    std::vector<HtaccessIpRule> ip_rules_;
    std::unordered_map<int, std::string> error_documents_;

    bool parse_line(const std::string &line,
                    std::vector<HtaccessCondition> &pending_conditions);

    bool match_ip(const std::string &client_ip,
                  const std::string &pattern) const;

    std::string resolve_variable(
        const std::string &variable_name, const std::string &method,
        const std::string &url, const std::string &remote_address,
        const std::unordered_map<std::string, std::string> &headers) const;

    std::string apply_substitution(const std::string &substitution_pattern,
                                   const std::smatch &match_results,
                                   const std::string &original_url,
                                   bool query_string_append) const;
};

/**
 * @brief Returns the global singleton instance of htaccess_engine.
 *
 * @return htaccess_engine& Reference to global htaccess engine.
 */
inline htaccess_engine &s_htaccess() { return htaccess_engine::instance(); }

} // namespace api
