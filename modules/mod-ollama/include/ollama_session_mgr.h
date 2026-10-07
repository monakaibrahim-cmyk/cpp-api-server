#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <crow.h>
#include <core/cache.h>
#include <ollama_types.h>

namespace api
{

/**
 * @brief Manages client sessions and chat history cached in LRU cache identified by user IP.
 */
class ollama_session_mgr
{
  public:
    explicit ollama_session_mgr(int session_ttl_seconds = 3600,
                               size_t max_history_turns = 20);

    /**
     * @brief Extracts the client IP address from an incoming HTTP request.
     * Looks at X-Forwarded-For, X-Client-IP, and remote_ip_address.
     */
    static std::string extract_user_ip(const crow::request &req);

    /**
     * @brief Sanitizes an IP address into a safe string for session IDs.
     */
    static std::string sanitize_ip(const std::string &ip);

    /**
     * @brief Retrieves an existing session or creates a new one for user IP.
     */
    ChatSession get_or_create_session(const std::string &user_ip,
                                      const std::string &default_agent = "");

    /**
     * @brief Retrieves session for user IP if it exists.
     */
    bool get_session(const std::string &user_ip, ChatSession &out_session);

    /**
     * @brief Updates preferences for user session (agent and system prompt).
     */
    void update_session(const std::string &user_ip,
                        const std::string &agent_name = "",
                        const std::string &custom_system = "",
                        const std::string &persona = "");

    /**
     * @brief Retrieves the cached chat history for user IP.
     */
    std::vector<ChatMessage> get_chat_history(const std::string &user_ip);

    /**
     * @brief Saves updated chat history and updates session in cache,
     * applying max history turns sliding window.
     */
    void save_chat_history(const std::string &user_ip,
                           std::vector<ChatMessage> history,
                           const std::string &agent_name);

    /**
     * @brief Clears session and chat history from cache for user IP.
     * @return true if cleared successfully.
     */
    bool clear_session(const std::string &user_ip);

    /**
     * @brief Updates user's preferred agent in their session.
     */
    void set_session_agent(const std::string &user_ip,
                           const std::string &agent_name);

    /**
     * @brief Exports conversation as a Markdown formatted document.
     */
    std::string export_history_markdown(const std::string &user_ip);

    /**
     * @brief Imports conversation messages into session cache.
     */
    void import_history(const std::string &user_ip,
                        const std::vector<ChatMessage> &messages,
                        bool replace = true);

    int get_session_ttl() const { return session_ttl_seconds_; }
    void set_session_ttl(int ttl_seconds) { session_ttl_seconds_ = ttl_seconds; }

    size_t get_max_history_turns() const { return max_history_turns_; }
    void set_max_history_turns(size_t max_turns)
    {
        max_history_turns_ = max_turns;
    }

  private:
    int session_ttl_seconds_;
    size_t max_history_turns_;

    std::string session_cache_key(const std::string &user_ip) const;
    std::string history_cache_key(const std::string &user_ip) const;
};

} // namespace api
