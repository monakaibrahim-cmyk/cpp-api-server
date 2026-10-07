#include <ollama_session_mgr.h>
#include <core/cache.h>
#include <core/logger.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace api
{

ollama_session_mgr::ollama_session_mgr(int session_ttl_seconds,
                                       size_t max_history_turns)
    : session_ttl_seconds_(session_ttl_seconds),
      max_history_turns_(max_history_turns)
{
}

std::string ollama_session_mgr::session_cache_key(const std::string &user_ip) const
{
    return "session:" + user_ip;
}

std::string ollama_session_mgr::history_cache_key(const std::string &user_ip) const
{
    return "chat_history:" + user_ip;
}

std::string ollama_session_mgr::sanitize_ip(const std::string &ip)
{
    std::string safe = ip;
    for (char &c : safe)
    {
        if (c == '.' || c == ':' || c == '[' || c == ']')
        {
            c = '_';
        }
    }
    return safe;
}

std::string ollama_session_mgr::extract_user_ip(const crow::request &req)
{
    // 1. Check X-Forwarded-For header
    std::string forwarded = req.get_header_value("X-Forwarded-For");
    if (!forwarded.empty())
    {
        size_t comma_pos = forwarded.find(',');
        std::string first_ip = (comma_pos != std::string::npos)
                                   ? forwarded.substr(0, comma_pos)
                                   : forwarded;
        first_ip.erase(0, first_ip.find_first_not_of(" \t\r\n"));
        first_ip.erase(first_ip.find_last_not_of(" \t\r\n") + 1);
        if (!first_ip.empty())
        {
            return first_ip;
        }
    }

    // 2. Check X-Real-IP or X-Client-IP
    std::string real_ip = req.get_header_value("X-Real-IP");
    if (!real_ip.empty())
    {
        return real_ip;
    }

    std::string client_ip = req.get_header_value("X-Client-IP");
    if (!client_ip.empty())
    {
        return client_ip;
    }

    // 3. Fallback to remote IP address from TCP socket
    if (!req.remote_ip_address.empty())
    {
        return req.remote_ip_address;
    }

    return "127.0.0.1";
}

ChatSession ollama_session_mgr::get_or_create_session(
    const std::string &user_ip, const std::string &default_agent)
{
    ChatSession session;
    if (get_session(user_ip, session))
    {
        return session;
    }

    auto now = std::chrono::system_clock::now();
    int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                         now.time_since_epoch())
                         .count();

    session.user_ip = user_ip;
    session.session_id =
        "sess_" + sanitize_ip(user_ip) + "_" + std::to_string(now_ts);
    session.created_at = now_ts;
    session.last_active = now_ts;
    session.selected_agent = default_agent;
    session.message_count = 0;
    session.total_characters = 0;

    std::string session_json = session.to_json().dump();
    s_cache_engine().set(session_cache_key(user_ip), 200, session_json,
                         "application/json",
                         std::chrono::seconds(session_ttl_seconds_));

    return session;
}

bool ollama_session_mgr::get_session(const std::string &user_ip,
                                    ChatSession &out_session)
{
    CachedResponse cached;
    if (!s_cache_engine().get(session_cache_key(user_ip), cached))
    {
        return false;
    }

    auto parsed = crow::json::load(cached.body);
    if (!parsed)
    {
        return false;
    }

    out_session = ChatSession::from_json(parsed);
    return true;
}

void ollama_session_mgr::update_session(const std::string &user_ip,
                                        const std::string &agent_name,
                                        const std::string &custom_system,
                                        const std::string &persona)
{
    ChatSession session = get_or_create_session(user_ip);
    if (!agent_name.empty())
    {
        session.selected_agent = agent_name;
    }
    if (!custom_system.empty())
    {
        session.custom_system_prompt = custom_system;
    }
    if (!persona.empty())
    {
        session.persona = persona;
    }

    auto now = std::chrono::system_clock::now();
    session.last_active = std::chrono::duration_cast<std::chrono::seconds>(
                              now.time_since_epoch())
                              .count();

    std::string session_json = session.to_json().dump();
    s_cache_engine().set(session_cache_key(user_ip), 200, session_json,
                         "application/json",
                         std::chrono::seconds(session_ttl_seconds_));
}

std::vector<ChatMessage> ollama_session_mgr::get_chat_history(
    const std::string &user_ip)
{
    std::vector<ChatMessage> history;
    CachedResponse cached;

    if (!s_cache_engine().get(history_cache_key(user_ip), cached))
    {
        return history;
    }

    auto parsed = crow::json::load(cached.body);
    if (!parsed)
    {
        return history;
    }

    for (size_t index = 0; index < parsed.size(); ++index)
    {
        history.push_back(ChatMessage::from_json(parsed[index]));
    }

    return history;
}

void ollama_session_mgr::save_chat_history(
    const std::string &user_ip, std::vector<ChatMessage> history,
    const std::string &agent_name)
{
    auto now = std::chrono::system_clock::now();
    int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                         now.time_since_epoch())
                         .count();

    // Enforce sliding window on conversation turns if exceeding limit
    if (max_history_turns_ > 0 && history.size() > max_history_turns_)
    {
        size_t excess = history.size() - max_history_turns_;
        history.erase(history.begin(), history.begin() + excess);
    }

    // Calculate total character length
    size_t char_count = 0;
    std::vector<crow::json::wvalue> messages_array;
    for (const auto &item : history)
    {
        char_count += item.content.size();
        messages_array.push_back(item.to_json());
    }

    crow::json::wvalue history_root;
    history_root = std::move(messages_array);
    std::string history_json = history_root.dump();

    s_cache_engine().set(history_cache_key(user_ip), 200, history_json,
                         "application/json",
                         std::chrono::seconds(session_ttl_seconds_));

    // Update and refresh session metadata
    ChatSession session = get_or_create_session(user_ip, agent_name);
    session.last_active = now_ts;
    session.message_count = history.size();
    session.total_characters = char_count;
    if (!agent_name.empty())
    {
        session.selected_agent = agent_name;
    }

    std::string session_json = session.to_json().dump();
    s_cache_engine().set(session_cache_key(user_ip), 200, session_json,
                         "application/json",
                         std::chrono::seconds(session_ttl_seconds_));
}

bool ollama_session_mgr::clear_session(const std::string &user_ip)
{
    bool removed_session = s_cache_engine().remove(session_cache_key(user_ip));
    bool removed_history = s_cache_engine().remove(history_cache_key(user_ip));
    return removed_session || removed_history;
}

void ollama_session_mgr::set_session_agent(const std::string &user_ip,
                                          const std::string &agent_name)
{
    update_session(user_ip, agent_name, "");
}

std::string ollama_session_mgr::export_history_markdown(
    const std::string &user_ip)
{
    ChatSession session;
    get_session(user_ip, session);
    auto history = get_chat_history(user_ip);

    std::ostringstream ss;
    ss << "# Chat Session Export\n\n";
    ss << "- **User IP**: `" << user_ip << "`\n";
    ss << "- **Session ID**: `" << session.session_id << "`\n";
    ss << "- **Active Agent**: `" << session.selected_agent << "`\n";
    ss << "- **Messages Count**: " << history.size() << "\n";
    ss << "- **Estimated Tokens**: " << (session.total_characters / 4) << "\n\n";
    ss << "---\n\n";

    for (size_t i = 0; i < history.size(); ++i)
    {
        const auto &msg = history[i];
        std::time_t t = static_cast<std::time_t>(msg.timestamp);
        std::tm tm_buf{};
#if defined(_WIN32) || defined(_WIN64)
        localtime_s(&tm_buf, &t);
#else
        localtime_r(&t, &tm_buf);
#endif
        char time_str[32];
        std::strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);

        if (msg.role == "user")
        {
            ss << "### 👤 User (" << time_str << ")\n\n";
            ss << msg.content << "\n\n";
        }
        else if (msg.role == "assistant")
        {
            ss << "### 🤖 Assistant ["
               << (msg.agent.empty() ? session.selected_agent : msg.agent)
               << "] (" << time_str << ")\n\n";
            ss << msg.content << "\n\n";
        }
        else
        {
            ss << "### ⚙️ " << msg.role << " (" << time_str << ")\n\n";
            ss << msg.content << "\n\n";
        }
        ss << "---\n\n";
    }

    return ss.str();
}

void ollama_session_mgr::import_history(
    const std::string &user_ip, const std::vector<ChatMessage> &messages,
    bool replace)
{
    std::vector<ChatMessage> history;
    if (!replace)
    {
        history = get_chat_history(user_ip);
    }
    history.insert(history.end(), messages.begin(), messages.end());
    save_chat_history(user_ip, std::move(history), "");
}

} // namespace api
