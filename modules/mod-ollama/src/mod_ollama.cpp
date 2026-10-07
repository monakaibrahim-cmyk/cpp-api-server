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
    : module("mod_ollama", "1.0.0",
             "Local Ollama AI model integration with auto-discovery, IP-based "
             "cached sessions, and chat history")
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
}

void mod_ollama::on_init(const ServerConfig &configuration)
{
    on_config_load(const_cast<ServerConfig &>(configuration));

    client_ = std::make_shared<ollama_client>(host_, port_, timeout_seconds_);
    session_mgr_ = std::make_shared<ollama_session_mgr>(session_ttl_);

    LOG_INFO("ollama", "Initialized mod_ollama [Target: http://"
                           << host_ << ":" << port_ << ", Session TTL: "
                           << session_ttl_ << "s]");
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
                                      const std::string &session_agent)
{
    if (!requested_agent.empty())
    {
        return requested_agent;
    }
    if (!session_agent.empty())
    {
        return session_agent;
    }
    if (!default_model_.empty())
    {
        return default_model_;
    }

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
    // Handler: ollama.agents (Auto-find agents / models)
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

            crow::json::wvalue res;
            std::vector<crow::json::wvalue> agents_array;
            agents_array.reserve(agents.size());

            for (const auto &agent : agents)
            {
                agents_array.push_back(agent.to_json());
            }

            res["status"] = "ok";
            res["count"] = agents.size();
            res["default_agent"] =
                self->default_model_.empty()
                    ? (agents.empty() ? "" : agents.front().name)
                    : self->default_model_;
            res["agents"] = std::move(agents_array);

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
            // Determine client IP
            std::string user_ip = ollama_session_mgr::extract_user_ip(request);

            // Parse request body JSON
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

            // Allow client IP override if specified in query or json for testing
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

            // Retrieve or initialize session for this user IP via cache
            auto session = session_mgr->get_or_create_session(user_ip);

            // Resolve target agent/model
            std::string target_agent =
                self->resolve_agent(requested_agent, session.selected_agent);

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

            // Retrieve cached chat history for this user IP
            auto history = session_mgr->get_chat_history(user_ip);

            // Append current user message
            auto now = std::chrono::system_clock::now();
            int64_t now_ts = std::chrono::duration_cast<std::chrono::seconds>(
                                 now.time_since_epoch())
                                 .count();

            ChatMessage user_msg;
            user_msg.role = "user";
            user_msg.content = message_text;
            user_msg.timestamp = now_ts;
            history.push_back(user_msg);

            // Call Ollama /api/chat with full conversation history
            auto chat_result =
                client->chat(target_agent, history, requested_system);

            if (!chat_result.success)
            {
                // Remove the uncompleted user message from history on failure
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

            // Append assistant response to history
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

            // Cache updated chat history and refresh session for this IP
            session_mgr->save_chat_history(user_ip, history, target_agent);

            // Retrieve refreshed session
            session_mgr->get_session(user_ip, session);

            // Build response
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
            for (const auto &item : history)
            {
                history_array.push_back(item.to_json());
            }
            res["chat_history"] = std::move(history_array);

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
            for (const auto &item : history)
            {
                history_array.push_back(item.to_json());
            }
            res["chat_history"] = std::move(history_array);
            res["message_count"] = history.size();

            crow::response response(200, res.dump());
            response.add_header("Content-Type", "application/json");
            return response;
        });

    // ─────────────────────────────────────────────────────────────────────────
    // Handler: ollama.clear_history (Clear cached session & chat history)
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
    // Handler: ollama.session (Retrieve session info for user IP)
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
        std::string target_agent =
            resolve_agent(requested_agent, session.selected_agent);

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
        return res;
    };

    LOG_INFO("ollama", "Bound 'Ollama' table into Lua state");
}

API_REGISTER_MODULE(api::mod_ollama)

} // namespace api
