#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <ollama_types.h>

namespace api
{

/**
 * @brief High-performance HTTP client for communicating with local Ollama daemon.
 */
class ollama_client
{
  public:
    explicit ollama_client(std::string host = "127.0.0.1", uint16_t port = 11434,
                          int timeout_seconds = 120);

    /**
     * @brief Tests if local Ollama daemon is reachable.
     * @param[out] out_version Version string if returned by Ollama.
     * @param[out] out_error Error description if unreachable.
     * @return true if Ollama is running and responsive.
     */
    bool is_available(std::string &out_version, std::string &out_error);

    /**
     * @brief Auto-discovers installed models from Ollama daemon via /api/tags and /api/ps.
     * @param[out] out_error Error message if discovery failed.
     * @return List of discovered agents.
     */
    std::vector<OllamaAgent> list_agents(std::string &out_error);

    /**
     * @brief Sends a chat conversation to Ollama /api/chat.
     * @param[in] model Name of the model/agent to use.
     * @param[in] messages Full conversation history.
     * @param[in] system_prompt Optional system prompt.
     * @return ChatResult containing the assistant's reply or error.
     */
    ChatResult chat(const std::string &model,
                    const std::vector<ChatMessage> &messages,
                    const std::string &system_prompt = "");

    /**
     * @brief Generates vector embeddings for one or more text inputs.
     * @param[in] model Embedding model name.
     * @param[in] inputs Text inputs to embed.
     * @return EmbedResult containing vector embeddings.
     */
    EmbedResult embed(const std::string &model,
                      const std::vector<std::string> &inputs);

    /**
     * @brief Triggers pulling a model from Ollama library.
     */
    bool pull_model(const std::string &model_name, std::string &out_error);

    /**
     * @brief Retrieves detailed model inspection data (modelfile, parameters, license).
     */
    bool show_model(const std::string &model_name,
                    crow::json::wvalue &out_details,
                    std::string &out_error);

    /**
     * @brief Deletes a local model from Ollama.
     */
    bool delete_model(const std::string &model_name, std::string &out_error);

    const std::string &get_host() const { return host_; }
    uint16_t get_port() const { return port_; }
    int get_timeout_seconds() const { return timeout_seconds_; }

    void set_host(std::string host) { host_ = std::move(host); }
    void set_port(uint16_t port) { port_ = port; }
    void set_timeout_seconds(int timeout) { timeout_seconds_ = timeout; }

  private:
    std::string host_;
    uint16_t port_;
    int timeout_seconds_;

    bool http_request(const std::string &method, const std::string &target,
                      const std::string &body, int timeout_seconds,
                      std::string &response_body, int &status_code,
                      std::string &error_msg);
};

} // namespace api
