#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <crow.h>

namespace api
{

/**
 * @brief Represents an auto-discovered Ollama model / agent.
 */
struct OllamaAgent
{
    std::string name;
    std::string model;
    std::string family;
    std::string parameter_size;
    std::string format;
    std::string quantization_level;
    uint64_t size_bytes = 0;
    std::string modified_at;
    bool is_running = false;

    crow::json::wvalue to_json() const
    {
        crow::json::wvalue json_agent;
        json_agent["name"] = name;
        json_agent["model"] = model.empty() ? name : model;
        json_agent["family"] = family;
        json_agent["parameter_size"] = parameter_size;
        json_agent["format"] = format;
        json_agent["quantization_level"] = quantization_level;
        json_agent["size_bytes"] = size_bytes;
        json_agent["modified_at"] = modified_at;
        json_agent["is_running"] = is_running;
        return json_agent;
    }
};

/**
 * @brief Represents a single turn in a chat conversation.
 */
struct ChatMessage
{
    std::string role;      // "system", "user", "assistant"
    std::string content;
    std::string agent;     // Optional: model/agent that produced this message
    int64_t timestamp = 0; // Unix epoch seconds

    crow::json::wvalue to_json() const
    {
        crow::json::wvalue json_msg;
        json_msg["role"] = role;
        json_msg["content"] = content;
        if (!agent.empty())
        {
            json_msg["agent"] = agent;
        }
        json_msg["timestamp"] = timestamp;
        return json_msg;
    }

    static ChatMessage from_json(const crow::json::rvalue &jv)
    {
        ChatMessage msg;
        if (jv.has("role"))
        {
            msg.role = jv["role"].s();
        }
        if (jv.has("content"))
        {
            msg.content = jv["content"].s();
        }
        if (jv.has("agent"))
        {
            msg.agent = jv["agent"].s();
        }
        if (jv.has("timestamp"))
        {
            msg.timestamp = jv["timestamp"].i();
        }
        else
        {
            auto now = std::chrono::system_clock::now();
            msg.timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                                now.time_since_epoch())
                                .count();
        }
        return msg;
    }
};

/**
 * @brief Represents a user session identified by client IP.
 */
struct ChatSession
{
    std::string user_ip;
    std::string session_id;
    int64_t created_at = 0;
    int64_t last_active = 0;
    std::string selected_agent;
    size_t message_count = 0;

    crow::json::wvalue to_json() const
    {
        crow::json::wvalue json_session;
        json_session["user_ip"] = user_ip;
        json_session["session_id"] = session_id;
        json_session["created_at"] = created_at;
        json_session["last_active"] = last_active;
        json_session["selected_agent"] = selected_agent;
        json_session["message_count"] = message_count;
        return json_session;
    }

    static ChatSession from_json(const crow::json::rvalue &jv)
    {
        ChatSession session;
        if (jv.has("user_ip"))
        {
            session.user_ip = jv["user_ip"].s();
        }
        if (jv.has("session_id"))
        {
            session.session_id = jv["session_id"].s();
        }
        if (jv.has("created_at"))
        {
            session.created_at = jv["created_at"].i();
        }
        if (jv.has("last_active"))
        {
            session.last_active = jv["last_active"].i();
        }
        if (jv.has("selected_agent"))
        {
            session.selected_agent = jv["selected_agent"].s();
        }
        if (jv.has("message_count"))
        {
            session.message_count = static_cast<size_t>(jv["message_count"].u());
        }
        return session;
    }
};

/**
 * @brief Result of a chat completion request to Ollama.
 */
struct ChatResult
{
    bool success = false;
    std::string response;
    std::string agent;
    std::string error;
    int64_t total_duration_ns = 0;
    int eval_count = 0;
};

} // namespace api
