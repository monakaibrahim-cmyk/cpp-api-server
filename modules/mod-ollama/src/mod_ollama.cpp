#include <mod_ollama.h>
#include <core/config.h>
#include <core/logger.h>
#include <scripting/lua_binding.h>
#include <scripting/lua_engine.h>
#include <core/module_registry.h>
#include <server/router.h>
#include <server/server.h>

#include <crow.h>

namespace api
{

mod_ollama::mod_ollama()
    : module("mod_ollama", "1.1.0",
             "Local Ollama AI model integration with auto-discovery, persona "
             "management, vector embeddings, and IP-based cached sessions")
{
}

void mod_ollama::on_config_load(ServerConfig &configuration)
{
    enabled_ = configuration.ollama_enabled;
    host_ = configuration.ollama_host;
    port_ = configuration.ollama_port;
    default_model_ = configuration.ollama_default_model;
    system_prompt_ = configuration.ollama_system_prompt;
    session_ttl_ = configuration.ollama_session_ttl;
    timeout_seconds_ = configuration.ollama_timeout_seconds;
    auto_discover_ = configuration.ollama_auto_discover;
    max_history_turns_ = configuration.ollama_max_history_turns;
}

void mod_ollama::on_init(const ServerConfig &configuration)
{
    on_config_load(const_cast<ServerConfig &>(configuration));

    client_ = std::make_shared<ollama_client>(host_, port_, timeout_seconds_);
    session_mgr_ = std::make_shared<ollama_session_mgr>(session_ttl_,
                                                        max_history_turns_);

    // Register built-in agent personas
    register_persona({
        .id = "assistant",
        .name = "General Assistant",
        .description = "Helpful, thoughtful, and versatile general assistant",
        .system_prompt = system_prompt_,
        .model = "",
    });

    register_persona({
        .id = "coder",
        .name = "Code Architect",
        .description = "Specialized in software engineering, debugging, and clean architecture",
        .system_prompt =
            "You are an expert senior software engineer and system architect. "
            "Write clean, idiomatic, and robust code. Include explanations for key decisions.",
        .model = "",
    });

    register_persona({
        .id = "summarizer",
        .name = "Executive Summarizer",
        .description = "Specialized in concise, structured, and factual executive summaries",
        .system_prompt =
            "You are an executive summarizer. Analyze content thoroughly and produce "
            "concise, bulleted, and structured takeaways highlighting key insights.",
        .model = "",
    });

    register_persona({
        .id = "tutor",
        .name = "Socratic Tutor",
        .description = "Specialized in teaching complex concepts step-by-step",
        .system_prompt =
            "You are a patient and encouraging academic tutor. Guide the learner step by step, "
            "using illustrative analogies and check-in questions to ensure understanding.",
        .model = "",
    });

    register_persona({
        .id = "analyst",
        .name = "Logic & Data Analyst",
        .description = "Specialized in rigorous logic analysis and structured problem-solving",
        .system_prompt =
            "You are a rigorous analytical thinking assistant. Break down problems systematically, "
            "evaluate assumptions, and provide clear trade-offs and data-backed deductions.",
        .model = "",
    });

    LOG_INFO("ollama", "Initialized mod_ollama [Target: http://"
                           << host_ << ":" << port_ << ", Session TTL: "
                           << session_ttl_ << "s, Max Turns: "
                           << max_history_turns_ << "]");
}

void mod_ollama::register_persona(AgentPersona persona)
{
    std::lock_guard<std::mutex> lock(personas_mutex_);
    personas_[persona.id] = std::move(persona);
}

std::vector<AgentPersona> mod_ollama::list_personas() const
{
    std::lock_guard<std::mutex> lock(personas_mutex_);
    std::vector<AgentPersona> list;
    list.reserve(personas_.size());
    for (const auto &[_, p] : personas_)
    {
        list.push_back(p);
    }
    return list;
}

std::optional<AgentPersona> mod_ollama::get_persona(const std::string &id_or_name) const
{
    std::lock_guard<std::mutex> lock(personas_mutex_);
    auto it = personas_.find(id_or_name);
    if (it != personas_.end())
    {
        return it->second;
    }
    for (const auto &[k, p] : personas_)
    {
        if (p.name == id_or_name || p.id == id_or_name)
        {
            return p;
        }
    }
    return std::nullopt;
}

void mod_ollama::on_server_startup(api_server & /*server_instance*/)
{
    if (!enabled_)
    {
        return;
    }

    if (auto_discover_)
    {
        std::string version;
        std::string error;
        if (client_->is_available(version, error))
        {
            LOG_INFO("ollama", "Ollama daemon connected successfully (v"
                                   << version << ")");
            auto agents = get_or_discover_agents(true);
            if (!agents.empty())
            {
                LOG_INFO("ollama", "Auto-discovered "
                                       << agents.size()
                                       << " local Ollama model(s). Active default: "
                                       << (default_model_.empty()
                                               ? agents.front().name
                                               : default_model_));
            }
            else
            {
                LOG_INFO("ollama", "Ollama daemon is online, but no models "
                                   "installed yet.");
            }
        }
        else
        {
            LOG_INFO("ollama",
                     "Ollama daemon is not reachable at startup ("
                         << error
                         << "). Auto-discovery will retry when queried.");
        }
    }
}

void mod_ollama::on_server_shutdown()
{
    LOG_INFO("ollama", "mod_ollama shut down.");
}

std::vector<OllamaAgent> mod_ollama::get_or_discover_agents(bool force_refresh)
{
    std::lock_guard<std::mutex> lock(agents_mutex_);
    auto now = std::chrono::steady_clock::now();

    if (!force_refresh && !cached_agents_.empty() &&
        (now - last_agent_discovery_) < std::chrono::seconds(30))
    {
        return cached_agents_;
    }

    if (!client_)
    {
        return cached_agents_;
    }

    std::string err;
    auto agents = client_->list_agents(err);
    if (!agents.empty())
    {
        cached_agents_ = agents;
        last_agent_discovery_ = now;
        LOG_INFO("ollama", "Auto-discovered " << agents.size()
                                              << " local Ollama model(s)");
    }
    else if (!err.empty())
    {
        LOG_DEBUG("ollama", "Agent discovery notice: " << err);
    }

    return cached_agents_.empty() ? agents : cached_agents_;
}

std::string mod_ollama::resolve_agent(const std::string &requested_agent,
                                      const std::string &session_agent,
                                      std::string *out_persona_system)
{
    // 1. Check if requested agent is a registered persona
    if (!requested_agent.empty())
    {
        std::lock_guard<std::mutex> lock(personas_mutex_);
        auto it = personas_.find(requested_agent);
        if (it != personas_.end())
        {
            if (out_persona_system != nullptr)
            {
                *out_persona_system = it->second.system_prompt;
            }
            if (!it->second.model.empty())
            {
                return it->second.model;
            }
        }
        else
        {
            return requested_agent;
        }
    }

    // 2. Fallback to session preferred agent
    if (!session_agent.empty())
    {
        return session_agent;
    }

    // 3. Fallback to server config default model
    if (!default_model_.empty())
    {
        return default_model_;
    }

    // 4. Fallback to first auto-discovered model from Ollama
    auto agents = get_or_discover_agents(false);
    if (!agents.empty())
    {
        return agents.front().name;
    }

    return "";
}

void mod_ollama::on_handlers_register(router &server_router)
{
    auto self = this;
    auto client = client_;
    auto session_mgr = session_mgr_;

    // ─────────────────────────────────────────────────────────────────────────
    // Handler: ollama.status
    // ─────────────────────────────────────────────────────────────────────────
    server_router.register_native_handler(
        "ollama.status",
        [self, client](const crow::request & /*request*/)
        {
            crow::json::wvalue res;
            std::string version;
            std::string error;

            bool available = client->is_available(version, error);

            res["status"] = available ? "ok" : "offline";
            res["available"] = available;
            res["version"] = version;
            res["host"] = self->host_;
            res["port"] = self->port_;
            res["default_model"] = self->default_model_;
            res["session_ttl_seconds"] = self->session_ttl_;
            res["max_history_turns"] = self->max_history_turns_;
            res["timeout_seconds"] = self->timeout_seconds_;

            if (!available)
            {
                res["error"] = error;
            }

            auto cache_stats = s_cache_engine().get_stats();
            crow::json::wvalue cache_json;
            cache_json["hits"] = cache_stats.hits;
            cache_json["misses"] = cache_stats.misses;
            cache_json["items"] = cache_stats.items;
            cache_json["evictions"] = cache_stats.evictions;
            res["cache"] = std::move(cache_json);

            crow::response response(200, res.dump());
            response.add_header("Content-Type", "application/json");
            return response;
        });

    // ─────────────────────────────────────────────────────────────────────────
    // Handler: ollama.agents (Auto-find agents + personas)
    // ─────────────────────────────────────────────────────────────────────────
    server_router.register_native_handler(
        "ollama.agents",
        [self](const crow::request &request)
        {
            crow::query_string qs(request.raw_url);
            bool force_refresh = false;
            char *refresh_val = qs.get("refresh");
            if (refresh_val != nullptr &&
                (std::string(refresh_val) == "true" ||
                 std::string(refresh_val) == "1"))
            {
                force_refresh = true;
            }

            auto agents = self->get_or_discover_agents(force_refresh);
            auto personas = self->list_personas();

            crow::json::wvalue res;
            std::vector<crow::json::wvalue> agents_array;
            agents_array.reserve(agents.size());

            for (const auto &agent : agents)
            {
                agents_array.push_back(agent.to_json());
            }

            std::vector<crow::json::wvalue> personas_array;
            personas_array.reserve(personas.size());
            for (const auto &p : personas)
            {
                personas_array.push_back(p.to_json());
            }

            res["status"] = "ok";
            res["count"] = agents.size();
            res["default_agent"] =
                self->default_model_.empty()
                    ? (agents.empty() ? "" : agents.front().name)
                    : self->default_model_;
            res["agents"] = std::move(agents_array);
            res["personas"] = std::move(personas_array);

            if (agents.empty())
            {
                std::string ver, err;
                if (!self->client_->is_available(ver, err))
                {
                    res["warning"] = "Ollama daemon unreachable at http://" +
                                     self->host_ + ":" +
                                     std::to_string(self->port_) + " (" + err +
                                     ")";
                }
                else
                {
                    res["warning"] =
                        "No models currently installed. Pull one using: "
                        "ollama pull <model_name>";
                }
            }

            crow::response response(200, res.dump());
            response.add_header("Content-Type", "application/json");
            return response;
        });

    // ─────────────────────────────────────────────────────────────────────────
    // Handler: ollama.chat (IP-identified session & cached chat history)
    // ─────────────────────────────────────────────────────────────────────────
    server_router.register_native_handler(
        "ollama.chat",
        [self, client, session_mgr](const crow::request &request)
        {
            std::string user_ip = ollama_session_mgr::extract_user_ip(request);

            auto body_json = crow::json::load(request.body);
            if (!body_json)
            {
                crow::json::wvalue err_res;
                err_res["status"] = "error";
                err_res["error"] = "Invalid JSON payload in request body";
                crow::response response(400, err_res.dump());
                response.add_header("Content-Type", "application/json");
                return response;
            }

            // IP parameter override support
            crow::query_string qs(request.raw_url);
            char *ip_param = qs.get("ip");
            if (ip_param != nullptr && std::strlen(ip_param) > 0)
            {
                user_ip = std::string(ip_param);
            }
            else if (body_json.has("ip"))
            {
                user_ip = body_json["ip"].s();
            }
            else if (body_json.has("user_ip"))
            {
                user_ip = body_json["user_ip"].s();
            }

            if (!body_json.has("message"))
            {
                crow::json::wvalue err_res;
                err_res["status"] = "error";
                err_res["error"] = "Missing required field 'message'";
                crow::response response(400, err_res.dump());
                response.add_header("Content-Type", "application/json");
                return response;
            }

            std::string message_text = body_json["message"].s();
            std::string requested_agent;
            if (body_json.has("agent"))
            {
                requested_agent = body_json["agent"].s();
            }
            else if (body_json.has("model"))
            {
                requested_agent = body_json["model"].s();
            }

            std::string requested_system = self->system_prompt_;
            if (body_json.has("system"))
            {
                requested_system = body_json["system"].s();
            }

            bool clear_first = false;
            if (body_json.has("clear_history"))
            {
                clear_first = body_json["clear_history"].b();
            }

            if (clear_first)
            {
                session_mgr->clear_session(user_ip);
            }

            auto session = session_mgr->get_or_create_session(user_ip);

            // Use session's custom system prompt if set and not overridden in request
            if (!body_json.has("system") &&
                !session.custom_system_prompt.empty())
            {
                requested_system = session.custom_system_prompt;
            }

            std::string persona_system;
            std::string target_agent = self->resolve_agent(
                requested_agent, session.selected_agent, &persona_system);

            if (!persona_system.empty() && !body_json.has("system"))
            {
                requested_system = persona_system;
            }

            if (target_agent.empty())
            {
                crow::json::wvalue err_res;
                err_res["status"] = "error";
                err_res["error"] =
                    "No Ollama model available or specified. Please ensure "
                    "Ollama is running with an installed model (e.g. 'ollama "
                    "run llama3') or specify an agent.";
                crow::response response(503, err_res.dump());
                response.add_header("Content-Type", "application/json");
                return response;
            }

            auto history = session_mgr->get_chat_history(user_ip);

            auto now = std::chrono::system_clock::now();
            int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                                 now.time_since_epoch())
                                 .count();

            ChatMessage user_msg;
            user_msg.role = "user";
            user_msg.content = message_text;
            user_msg.timestamp = now_ts;
            history.push_back(user_msg);

            auto chat_result =
                client->chat(target_agent, history, requested_system);

            if (!chat_result.success)
            {
                history.pop_back();

                crow::json::wvalue err_res;
                err_res["status"] = "error";
                err_res["error"] = chat_result.error;
                err_res["agent"] = target_agent;
                err_res["user_ip"] = user_ip;
                crow::response response(502, err_res.dump());
                response.add_header("Content-Type", "application/json");
                return response;
            }

            auto reply_time = std::chrono::system_clock::now();
            int64_t reply_ts =
                std::chrono::duration_cast<std::chrono::seconds>(
                    reply_time.time_since_epoch())
                    .count();

            ChatMessage assistant_msg;
            assistant_msg.role = "assistant";
            assistant_msg.content = chat_result.response;
            assistant_msg.agent = target_agent;
            assistant_msg.timestamp = reply_ts;
            history.push_back(assistant_msg);

            session_mgr->save_chat_history(user_ip, history, target_agent);
            session_mgr->get_session(user_ip, session);

            crow::json::wvalue res;
            res["status"] = "ok";
            res["response"] = chat_result.response;
            res["agent"] = target_agent;
            res["user_ip"] = user_ip;
            res["session"] = session.to_json();
            res["eval_count"] = chat_result.eval_count;
            res["total_duration_ms"] =
                chat_result.total_duration_ns / 1000000;

            std::vector<crow::json::wvalue> history_array;
            std::vector<crow::json::wvalue> messages_array;
            for (const auto &item : history)
            {
                history_array.push_back(item.to_json());
                messages_array.push_back(item.to_json());
            }
            res["chat_history"] = std::move(history_array);
            res["messages"] = std::move(messages_array);

            crow::response response(200, res.dump());
            response.add_header("Content-Type", "application/json");
            return response;
        });

    // ─────────────────────────────────────────────────────────────────────────
    // Handler: ollama.embed (Vector embeddings)
    // ─────────────────────────────────────────────────────────────────────────
    server_router.register_native_handler(
        "ollama.embed",
        [self, client](const crow::request &request)
        {
            auto body_json = crow::json::load(request.body);
            if (!body_json)
            {
                crow::json::wvalue err_res;
                err_res["status"] = "error";
                err_res["error"] = "Invalid JSON payload in request body";
                crow::response response(400, err_res.dump());
                response.add_header("Content-Type", "application/json");
                return response;
            }

            std::string model;
            if (body_json.has("model"))
            {
                model = body_json["model"].s();
            }
            else
            {
                model = self->resolve_agent("", "");
            }

            if (model.empty())
            {
                crow::json::wvalue err_res;
                err_res["status"] = "error";
                err_res["error"] = "No model specified or discovered for embedding";
                crow::response response(400, err_res.dump());
                response.add_header("Content-Type", "application/json");
                return response;
            }

            std::vector<std::string> inputs;
            if (body_json.has("input"))
            {
                const auto &in_val = body_json["input"];
                if (in_val.t() == crow::json::type::List)
                {
                    for (size_t i = 0; i < in_val.size(); ++i)
                    {
                        inputs.push_back(in_val[i].s());
                    }
                }
                else if (in_val.t() == crow::json::type::String)
                {
                    inputs.push_back(in_val.s());
                }
            }
            else if (body_json.has("prompt"))
            {
                inputs.push_back(body_json["prompt"].s());
            }

            if (inputs.empty())
            {
                crow::json::wvalue err_res;
                err_res["status"] = "error";
                err_res["error"] = "Missing required 'input' field (string or array)";
                crow::response response(400, err_res.dump());
                response.add_header("Content-Type", "application/json");
                return response;
            }

            auto embed_res = client->embed(model, inputs);
            if (!embed_res.success)
            {
                crow::json::wvalue err_res;
                err_res["status"] = "error";
                err_res["error"] = embed_res.error;
                err_res["model"] = model;
                crow::response response(502, err_res.dump());
                response.add_header("Content-Type", "application/json");
                return response;
            }

            crow::json::wvalue res;
            res["status"] = "ok";
            res["model"] = model;
            res["count"] = embed_res.embeddings.size();
            res["dimensions"] =
                embed_res.embeddings.empty() ? 0 : embed_res.embeddings.front().size();
            res["total_duration_ms"] =
                embed_res.total_duration_ns / 1000000;

            std::vector<crow::json::wvalue> emb_array;
            for (const auto &vec : embed_res.embeddings)
            {
                std::vector<crow::json::wvalue> v_items;
                v_items.reserve(vec.size());
                for (double d : vec)
                {
                    v_items.push_back(d);
                }
                emb_array.push_back(std::move(v_items));
            }
            res["embeddings"] = std::move(emb_array);

            crow::response response(200, res.dump());
            response.add_header("Content-Type", "application/json");
            return response;
        });

    // ─────────────────────────────────────────────────────────────────────────
    // Handler: ollama.pull (Pull model from library)
    // ─────────────────────────────────────────────────────────────────────────
    server_router.register_native_handler(
        "ollama.pull",
        [client](const crow::request &request)
        {
            auto body_json = crow::json::load(request.body);
            std::string model;
            if (body_json && body_json.has("model"))
            {
                model = body_json["model"].s();
            }
            else if (body_json && body_json.has("name"))
            {
                model = body_json["name"].s();
            }

            if (model.empty())
            {
                crow::json::wvalue err;
                err["status"] = "error";
                err["error"] = "Missing 'model' name to pull";
                return crow::response(400, err.dump());
            }

            std::string err_msg;
            bool ok = client->pull_model(model, err_msg);

            crow::json::wvalue res;
            res["status"] = ok ? "ok" : "error";
            res["model"] = model;
            if (!ok)
            {
                res["error"] = err_msg;
                return crow::response(502, res.dump());
            }
            res["message"] = "Model successfully pulled";
            return crow::response(200, res.dump());
        });

    // ─────────────────────────────────────────────────────────────────────────
    // Handler: ollama.show (Inspect model details)
    // ─────────────────────────────────────────────────────────────────────────
    server_router.register_native_handler(
        "ollama.show",
        [self, client](const crow::request &request)
        {
            crow::query_string qs(request.raw_url);
            std::string model;
            char *m_param = qs.get("model");
            if (m_param != nullptr)
            {
                model = std::string(m_param);
            }
            else
            {
                auto body_json = crow::json::load(request.body);
                if (body_json && body_json.has("model"))
                {
                    model = body_json["model"].s();
                }
            }

            if (model.empty())
            {
                model = self->resolve_agent("", "");
            }

            crow::json::wvalue details;
            std::string err;
            bool ok = client->show_model(model, details, err);

            if (!ok)
            {
                crow::json::wvalue err_res;
                err_res["status"] = "error";
                err_res["error"] = err;
                err_res["model"] = model;
                return crow::response(502, err_res.dump());
            }

            crow::json::wvalue res;
            res["status"] = "ok";
            res["model"] = model;
            res["details"] = std::move(details);
            crow::response response(200, res.dump());
            response.add_header("Content-Type", "application/json");
            return response;
        });

    // ─────────────────────────────────────────────────────────────────────────
    // Handler: ollama.delete (Delete / unload model)
    // ─────────────────────────────────────────────────────────────────────────
    server_router.register_native_handler(
        "ollama.delete",
        [client](const crow::request &request)
        {
            crow::query_string qs(request.raw_url);
            std::string model;
            char *m_param = qs.get("model");
            if (m_param != nullptr)
            {
                model = std::string(m_param);
            }
            else
            {
                auto body_json = crow::json::load(request.body);
                if (body_json && body_json.has("model"))
                {
                    model = body_json["model"].s();
                }
            }

            if (model.empty())
            {
                crow::json::wvalue err;
                err["status"] = "error";
                err["error"] = "Missing 'model' parameter";
                crow::response response(400, err.dump());
                response.add_header("Content-Type", "application/json");
                return response;
            }

            std::string err;
            bool ok = client->delete_model(model, err);

            crow::json::wvalue res;
            res["status"] = ok ? "ok" : "error";
            res["model"] = model;
            if (!ok)
            {
                res["error"] = err;
                crow::response response(502, res.dump());
                response.add_header("Content-Type", "application/json");
                return response;
            }
            res["message"] = "Model successfully deleted";
            crow::response response(200, res.dump());
            response.add_header("Content-Type", "application/json");
            return response;
        });

    // ─────────────────────────────────────────────────────────────────────────
    // Handler: ollama.history (Retrieve cached chat history for user IP)
    // ─────────────────────────────────────────────────────────────────────────
    server_router.register_native_handler(
        "ollama.history",
        [session_mgr](const crow::request &request)
        {
            std::string user_ip = ollama_session_mgr::extract_user_ip(request);

            crow::query_string qs(request.raw_url);
            char *ip_param = qs.get("ip");
            if (ip_param != nullptr && std::strlen(ip_param) > 0)
            {
                user_ip = std::string(ip_param);
            }

            ChatSession session;
            bool has_session = session_mgr->get_session(user_ip, session);
            auto history = session_mgr->get_chat_history(user_ip);

            crow::json::wvalue res;
            res["status"] = "ok";
            res["user_ip"] = user_ip;
            res["has_session"] = has_session;

            if (has_session)
            {
                res["session"] = session.to_json();
            }

            std::vector<crow::json::wvalue> history_array;
            std::vector<crow::json::wvalue> messages_array;
            for (const auto &item : history)
            {
                history_array.push_back(item.to_json());
                messages_array.push_back(item.to_json());
            }
            res["chat_history"] = std::move(history_array);
            res["messages"] = std::move(messages_array);
            res["message_count"] = history.size();

            crow::response response(200, res.dump());
            response.add_header("Content-Type", "application/json");
            return response;
        });

    // ─────────────────────────────────────────────────────────────────────────
    // Handler: ollama.export (Export history as JSON or Markdown)
    // ─────────────────────────────────────────────────────────────────────────
    server_router.register_native_handler(
        "ollama.export",
        [session_mgr](const crow::request &request)
        {
            std::string user_ip = ollama_session_mgr::extract_user_ip(request);
            crow::query_string qs(request.raw_url);
            char *ip_param = qs.get("ip");
            if (ip_param != nullptr && std::strlen(ip_param) > 0)
            {
                user_ip = std::string(ip_param);
            }

            char *fmt_param = qs.get("format");
            std::string format = fmt_param != nullptr ? std::string(fmt_param) : "json";

            if (format == "markdown" || format == "md")
            {
                std::string md = session_mgr->export_history_markdown(user_ip);
                crow::response res(200, md);
                res.add_header("Content-Type", "text/markdown; charset=utf-8");
                res.add_header("Content-Disposition",
                               "attachment; filename=\"chat_" +
                                   ollama_session_mgr::sanitize_ip(user_ip) +
                                   ".md\"");
                return res;
            }

            auto history = session_mgr->get_chat_history(user_ip);
            ChatSession session;
            session_mgr->get_session(user_ip, session);

            crow::json::wvalue res_json;
            res_json["user_ip"] = user_ip;
            res_json["session"] = session.to_json();
            std::vector<crow::json::wvalue> history_array;
            std::vector<crow::json::wvalue> messages_array;
            for (const auto &item : history)
            {
                history_array.push_back(item.to_json());
                messages_array.push_back(item.to_json());
            }
            res_json["chat_history"] = std::move(history_array);
            res_json["messages"] = std::move(messages_array);
            res_json["message_count"] = history.size();

            crow::response res(200, res_json.dump());
            res.add_header("Content-Type", "application/json");
            return res;
        });

    // ─────────────────────────────────────────────────────────────────────────
    // Handler: ollama.import (Import history into session)
    // ─────────────────────────────────────────────────────────────────────────
    server_router.register_native_handler(
        "ollama.import",
        [session_mgr](const crow::request &request)
        {
            std::string user_ip = ollama_session_mgr::extract_user_ip(request);
            auto body_json = crow::json::load(request.body);
            if (!body_json || !body_json.has("messages"))
            {
                crow::json::wvalue err;
                err["status"] = "error";
                err["error"] = "Invalid payload: 'messages' array required";
                crow::response response(400, err.dump());
                response.add_header("Content-Type", "application/json");
                return response;
            }

            if (body_json.has("ip"))
            {
                user_ip = body_json["ip"].s();
            }

            bool replace = true;
            if (body_json.has("replace"))
            {
                replace = body_json["replace"].b();
            }

            std::vector<ChatMessage> messages;
            for (const auto &item : body_json["messages"])
            {
                messages.push_back(ChatMessage::from_json(item));
            }

            session_mgr->import_history(user_ip, messages, replace);
            auto updated_history = session_mgr->get_chat_history(user_ip);

            crow::json::wvalue res;
            res["status"] = "ok";
            res["user_ip"] = user_ip;
            res["imported_count"] = messages.size();
            res["total_messages"] = updated_history.size();
            crow::response response(200, res.dump());
            response.add_header("Content-Type", "application/json");
            return response;
        });

    // ─────────────────────────────────────────────────────────────────────────
    // Handler: ollama.clear_history
    // ─────────────────────────────────────────────────────────────────────────
    server_router.register_native_handler(
        "ollama.clear_history",
        [session_mgr](const crow::request &request)
        {
            std::string user_ip = ollama_session_mgr::extract_user_ip(request);

            crow::query_string qs(request.raw_url);
            char *ip_param = qs.get("ip");
            if (ip_param != nullptr && std::strlen(ip_param) > 0)
            {
                user_ip = std::string(ip_param);
            }

            bool cleared = session_mgr->clear_session(user_ip);

            crow::json::wvalue res;
            res["status"] = "ok";
            res["cleared"] = cleared;
            res["user_ip"] = user_ip;
            res["message"] = "Chat session and history cleared from cache for IP " +
                             user_ip;

            crow::response response(200, res.dump());
            response.add_header("Content-Type", "application/json");
            return response;
        });

    // ─────────────────────────────────────────────────────────────────────────
    // Handler: ollama.session & ollama.set_session
    // ─────────────────────────────────────────────────────────────────────────
    server_router.register_native_handler(
        "ollama.session",
        [session_mgr](const crow::request &request)
        {
            std::string user_ip = ollama_session_mgr::extract_user_ip(request);

            crow::query_string qs(request.raw_url);
            char *ip_param = qs.get("ip");
            if (ip_param != nullptr && std::strlen(ip_param) > 0)
            {
                user_ip = std::string(ip_param);
            }

            ChatSession session;
            bool has_session = session_mgr->get_session(user_ip, session);

            crow::json::wvalue res;
            res["status"] = "ok";
            res["user_ip"] = user_ip;
            res["has_session"] = has_session;

            if (has_session)
            {
                res["session"] = session.to_json();
            }
            else
            {
                res["message"] = "No active session for IP " + user_ip;
            }

            crow::response response(200, res.dump());
            response.add_header("Content-Type", "application/json");
            return response;
        });

    server_router.register_native_handler(
        "ollama.set_session",
        [self, session_mgr](const crow::request &request)
        {
            std::string user_ip = ollama_session_mgr::extract_user_ip(request);
            auto body_json = crow::json::load(request.body);

            std::string agent;
            std::string system_prompt;
            std::string persona;

            if (body_json)
            {
                if (body_json.has("ip"))
                {
                    user_ip = body_json["ip"].s();
                }
                if (body_json.has("agent"))
                {
                    agent = body_json["agent"].s();
                }
                else if (body_json.has("preferred_agent"))
                {
                    agent = body_json["preferred_agent"].s();
                }
                else if (body_json.has("model"))
                {
                    agent = body_json["model"].s();
                }

                if (body_json.has("persona"))
                {
                    persona = body_json["persona"].s();
                    auto p = self->get_persona(persona);
                    if (p.has_value() && (!body_json.has("system_prompt") && !body_json.has("system")))
                    {
                        system_prompt = p->system_prompt;
                    }
                }

                if (body_json.has("system_prompt"))
                {
                    system_prompt = body_json["system_prompt"].s();
                }
                else if (body_json.has("system"))
                {
                    system_prompt = body_json["system"].s();
                }
            }

            session_mgr->update_session(user_ip, agent, system_prompt, persona);

            ChatSession session;
            session_mgr->get_session(user_ip, session);

            crow::json::wvalue res;
            res["status"] = "ok";
            res["user_ip"] = user_ip;
            res["session"] = session.to_json();

            crow::response response(200, res.dump());
            response.add_header("Content-Type", "application/json");
            return response;
        });
}

void mod_ollama::on_lua_init(lua_engine &lua_engine_instance)
{
    std::lock_guard<std::mutex> lock(lua_engine_instance.mutex());
    auto &lua_state = lua_engine_instance.state();

    sol::table ollama_table = lua_get_or_create_namespace(lua_state, "Ollama");

    ollama_table["status"] = [this](sol::this_state s) -> sol::table
    {
        sol::state_view state(s);
        sol::table result = state.create_table();
        std::string version, error;
        bool available = client_->is_available(version, error);

        result["status"] = available ? "ok" : "offline";
        result["available"] = available;
        result["version"] = version;
        result["host"] = host_;
        result["port"] = port_;
        result["default_model"] = default_model_;
        if (!available)
        {
            result["error"] = error;
        }
        return result;
    };

    ollama_table["agents"] = [this](sol::optional<bool> refresh,
                                    sol::this_state s) -> sol::table
    {
        sol::state_view state(s);
        sol::table result = state.create_table();
        bool force_refresh = refresh.value_or(false);
        auto agents = get_or_discover_agents(force_refresh);

        sol::table list = state.create_table();
        int idx = 1;
        for (const auto &a : agents)
        {
            sol::table item = state.create_table();
            item["name"] = a.name;
            item["model"] = a.model;
            item["family"] = a.family;
            item["parameter_size"] = a.parameter_size;
            item["format"] = a.format;
            item["quantization_level"] = a.quantization_level;
            item["size_bytes"] = a.size_bytes;
            item["is_running"] = a.is_running;
            list[idx++] = item;
        }

        result["agents"] = list;
        result["count"] = agents.size();
        return result;
    };

    ollama_table["chat"] =
        [this](sol::table params, sol::this_state s) -> sol::table
    {
        sol::state_view state(s);
        sol::table result = state.create_table();
        std::string user_ip =
            params.get_or(std::string("ip"), std::string("127.0.0.1"));
        std::string message =
            params.get_or(std::string("message"), std::string(""));
        std::string requested_agent =
            params.get_or(std::string("agent"), std::string(""));
        std::string req_system =
            params.get_or(std::string("system"), system_prompt_);
        sol::optional<bool> opt_clear = params["clear_history"];
        bool clear_first = opt_clear.value_or(false);

        if (clear_first)
        {
            session_mgr_->clear_session(user_ip);
        }

        auto session = session_mgr_->get_or_create_session(user_ip);
        std::string persona_sys;
        std::string target_agent =
            resolve_agent(requested_agent, session.selected_agent, &persona_sys);

        if (!persona_sys.empty())
        {
            req_system = persona_sys;
        }

        if (target_agent.empty())
        {
            result["status"] = "error";
            result["error"] = "No agent specified or available";
            return result;
        }

        auto history = session_mgr_->get_chat_history(user_ip);
        auto now = std::chrono::system_clock::now();
        int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                             now.time_since_epoch())
                             .count();

        ChatMessage u_msg;
        u_msg.role = "user";
        u_msg.content = message;
        u_msg.timestamp = now_ts;
        history.push_back(u_msg);

        auto chat_res = client_->chat(target_agent, history, req_system);
        if (!chat_res.success)
        {
            result["status"] = "error";
            result["error"] = chat_res.error;
            return result;
        }

        ChatMessage a_msg;
        a_msg.role = "assistant";
        a_msg.content = chat_res.response;
        a_msg.agent = target_agent;
        a_msg.timestamp = now_ts;
        history.push_back(a_msg);

        session_mgr_->save_chat_history(user_ip, history, target_agent);

        result["status"] = "ok";
        result["response"] = chat_res.response;
        result["agent"] = target_agent;
        result["user_ip"] = user_ip;
        return result;
    };

    ollama_table["embed"] =
        [this](const std::string &input_text, sol::optional<std::string> model,
               sol::this_state s) -> sol::table
    {
        sol::state_view state(s);
        sol::table result = state.create_table();
        std::string target_model =
            model.has_value() ? model.value() : resolve_agent("", "");

        auto embed_res = client_->embed(target_model, {input_text});
        result["status"] = embed_res.success ? "ok" : "error";
        result["model"] = target_model;
        if (!embed_res.success)
        {
            result["error"] = embed_res.error;
            return result;
        }

        if (!embed_res.embeddings.empty())
        {
            sol::table vec = state.create_table();
            int idx = 1;
            for (double d : embed_res.embeddings.front())
            {
                vec[idx++] = d;
            }
            result["embedding"] = vec;
            result["dimensions"] = embed_res.embeddings.front().size();
        }
        return result;
    };

    ollama_table["get_history"] =
        [this](const std::string &user_ip, sol::this_state s) -> sol::table
    {
        sol::state_view state(s);
        sol::table result = state.create_table();
        auto history = session_mgr_->get_chat_history(user_ip);

        int idx = 1;
        for (const auto &msg : history)
        {
            sol::table item = state.create_table();
            item["role"] = msg.role;
            item["content"] = msg.content;
            item["agent"] = msg.agent;
            item["timestamp"] = msg.timestamp;
            result[idx++] = item;
        }
        return result;
    };

    ollama_table["clear_history"] = [this](const std::string &user_ip) -> bool
    { return session_mgr_->clear_session(user_ip); };

    ollama_table["get_session"] =
        [this](const std::string &user_ip, sol::this_state s) -> sol::object
    {
        sol::state_view state(s);
        ChatSession session;
        if (!session_mgr_->get_session(user_ip, session))
        {
            return sol::nil;
        }
        sol::table res = state.create_table();
        res["user_ip"] = session.user_ip;
        res["session_id"] = session.session_id;
        res["created_at"] = session.created_at;
        res["last_active"] = session.last_active;
        res["selected_agent"] = session.selected_agent;
        res["message_count"] = session.message_count;
        res["total_characters"] = session.total_characters;
        return res;
    };

    LOG_INFO("ollama", "Bound 'Ollama' table into Lua state");
}

API_REGISTER_MODULE(api::mod_ollama)

} // namespace api
