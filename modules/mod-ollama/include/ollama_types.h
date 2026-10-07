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
 * @brief Represents a specialized agent persona with tailored system instructions.
 */
struct AgentPersona
{
    std::string id;
    std::string name;
    std::string description;
    std::string system_prompt;
    std::string model; // Specific model override, or empty for default

    crow::json::wvalue to_json() const
    {
        crow::json::wvalue json_p;
        json_p["id"] = id;
        json_p["name"] = name;
        json_p["description"] = description;
        json_p["system_prompt"] = system_prompt;
        json_p["model"] = model;
        return json_p;
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
    std::string custom_system_prompt;
    std::string persona;
    size_t message_count = 0;
    size_t total_characters = 0;
    size_t token_limit = 0; // 0 = unlimited tokens per user

    crow::json::wvalue to_json() const
    {
        crow::json::wvalue json_session;
        json_session["user_ip"] = user_ip;
        json_session["session_id"] = session_id;
        json_session["created_at"] = created_at;
        json_session["last_active"] = last_active;
        json_session["selected_agent"] = selected_agent;
        json_session["preferred_agent"] = selected_agent;
        if (!persona.empty())
        {
            json_session["persona"] = persona;
        }
        if (!custom_system_prompt.empty())
        {
            json_session["custom_system_prompt"] = custom_system_prompt;
        }
        json_session["message_count"] = message_count;
        json_session["total_characters"] = total_characters;
        json_session["estimated_tokens"] = (total_characters / 4);
        json_session["token_limit"] = token_limit;
        json_session["unlimited_tokens"] = true;
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
        else if (jv.has("preferred_agent"))
        {
            session.selected_agent = jv["preferred_agent"].s();
        }
        if (jv.has("persona"))
        {
            session.persona = jv["persona"].s();
        }
        if (jv.has("custom_system_prompt"))
        {
            session.custom_system_prompt = jv["custom_system_prompt"].s();
        }
        if (jv.has("message_count"))
        {
            session.message_count = static_cast<size_t>(jv["message_count"].u());
        }
        if (jv.has("total_characters"))
        {
            session.total_characters = static_cast<size_t>(jv["total_characters"].u());
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

/**
 * @brief Result of a vector embedding request to Ollama.
 */
struct EmbedResult
{
    bool success = false;
    std::string model;
    std::string error;
    std::vector<std::vector<double>> embeddings;
    int64_t total_duration_ns = 0;
    int prompt_eval_count = 0;
};

} // namespace api
