#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <core/module.h>
#include <core/service_registry.h>
#include <ollama_client.h>
#include <ollama_session_mgr.h>
#include <ollama_types.h>

namespace api
{

class router;
class lua_engine;
class api_server;
struct ServerConfig;

/**
 * @brief Ollama module providing local LLM agent auto-discovery, persona management,
 * embeddings, model lifecycle, and IP-identified cached sessions & chat history.
 */
class mod_ollama : public module
{
  public:
    mod_ollama();
    ~mod_ollama() override = default;

    void on_config_load(ServerConfig &configuration) override;
    void on_init(const ServerConfig &configuration) override;
    void on_handlers_register(router &server_router) override;
    void on_lua_init(lua_engine &lua_engine_instance) override;
    void on_server_startup(api_server &server_instance) override;
    void on_server_shutdown() override;

    std::shared_ptr<ollama_client> get_client() const { return client_; }
    std::shared_ptr<ollama_session_mgr> get_session_mgr() const
    {
        return session_mgr_;
    }

    /**
     * @brief Auto-discovers agents from Ollama or returns cached list.
     * @param force_refresh If true, queries Ollama directly instead of cache.
     */
    std::vector<OllamaAgent> get_or_discover_agents(bool force_refresh = false);

    /**
     * @brief Resolves which agent/model should be used for a request,
     * checking requested agent, persona mapping, session agent, and auto-discovered default.
     */
    std::string resolve_agent(const std::string &requested_agent,
                              const std::string &session_agent,
                              std::string *out_persona_system = nullptr);

    /**
     * @brief Registers an agent persona with tailored system instructions.
     */
    void register_persona(AgentPersona persona);

    /**
     * @brief Returns all currently registered personas.
     */
    std::vector<AgentPersona> list_personas() const;

    /**
     * @brief Finds a persona by id or name if registered.
     */
    std::optional<AgentPersona> get_persona(const std::string &id_or_name) const;

  private:
    std::shared_ptr<ollama_client> client_;
    std::shared_ptr<ollama_session_mgr> session_mgr_;

    bool enabled_ = true;
    std::string host_ = "127.0.0.1";
    uint16_t port_ = 11434;
    std::string default_model_;
    std::string system_prompt_ = "You are a helpful AI assistant.";
    int session_ttl_ = 3600;
    int timeout_seconds_ = 120;
    bool auto_discover_ = true;
    size_t max_history_turns_ = 20;

    mutable std::mutex personas_mutex_;
    std::unordered_map<std::string, AgentPersona> personas_;

    mutable std::mutex agents_mutex_;
    std::vector<OllamaAgent> cached_agents_;
    std::chrono::steady_clock::time_point last_agent_discovery_;
};

} // namespace api
