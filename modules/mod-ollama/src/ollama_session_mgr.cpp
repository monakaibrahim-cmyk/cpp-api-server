#include <ollama_session_mgr.h>
#include <core/cache.h>
#include <core/logger.h>

#include <algorithm>
#include <chrono>
#include <sstream>

namespace api
{

ollama_session_mgr::ollama_session_mgr(int session_ttl_seconds)
    : session_ttl_seconds_(session_ttl_seconds)
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
        // Trim whitespace
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

    // 4. Default fallback for local testing
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
    session.session_id = "sess_" + sanitize_ip(user_ip) + "_" + std::to_string(now_ts);
    session.created_at = now_ts;
    session.last_active = now_ts;
    session.selected_agent = default_agent;
    session.message_count = 0;

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
    const std::string &user_ip, const std::vector<ChatMessage> &history,
    const std::string &agent_name)
{
    auto now = std::chrono::system_clock::now();
    int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                         now.time_since_epoch())
                         .count();

    // 1. Serialize history array
    std::vector<crow::json::wvalue> messages_array;
    for (const auto &item : history)
    {
        messages_array.push_back(item.to_json());
    }

    crow::json::wvalue history_root;
    history_root = std::move(messages_array);
    std::string history_json = history_root.dump();

    s_cache_engine().set(history_cache_key(user_ip), 200, history_json,
                         "application/json",
                         std::chrono::seconds(session_ttl_seconds_));

    // 2. Update and refresh session metadata
    ChatSession session = get_or_create_session(user_ip, agent_name);
    session.last_active = now_ts;
    session.message_count = history.size();
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
    ChatSession session = get_or_create_session(user_ip, agent_name);
    session.selected_agent = agent_name;

    std::string session_json = session.to_json().dump();
    s_cache_engine().set(session_cache_key(user_ip), 200, session_json,
                         "application/json",
                         std::chrono::seconds(session_ttl_seconds_));
}

} // namespace api
