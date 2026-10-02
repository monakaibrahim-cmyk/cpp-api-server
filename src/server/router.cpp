#include "api/router.hpp"
#include "api/cache.hpp"
#include "api/logger.hpp"
#include "api/lua_engine.hpp"
#include "api/server.hpp"

#include <filesystem>

namespace api
{

router::router(api_server& server, lua_engine& lua)
    : server_(server)
    , lua_(lua)
{
    setup_lua_route_binding();
}

void router::register_native_handler(const std::string& name, native_handler_t handler)
{
    std::lock_guard<std::mutex> lock(mutex_);
    bool exists = native_handlers_.find(name) != native_handlers_.end();

    native_handlers_[name] = std::move(handler);

    if (exists)
    {
        LOG_INFO("routes", "Overrode native handler: " << name);
    }
    else
    {
        LOG_DEBUG("routes", "Registered native handler: " << name);
    }
}

void router::override_native_handler(const std::string& name, native_handler_t handler)
{
    register_native_handler(name, std::move(handler));
}

bool router::has_native_handler(const std::string& name) const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return native_handlers_.find(name) != native_handlers_.end();
}

void router::register_handler(const std::string& name, native_handler_t handler)
{
    register_native_handler(name, std::move(handler));
}

void router::override_handler(const std::string& name, native_handler_t handler)
{
    override_native_handler(name, std::move(handler));
}

bool router::has_handler(const std::string& name) const
{
    return has_native_handler(name);
}

crow::HTTPMethod router::parse_method(const std::string& method_str)
{
    if (method_str == "POST")
    {
        return crow::HTTPMethod::POST;
    }

    if (method_str == "PUT")
    {
        return crow::HTTPMethod::PUT;
    }

    if (method_str == "DELETE")
    {
        return crow::HTTPMethod::DELETE;
    }

    if (method_str == "PATCH")
    {
        return crow::HTTPMethod::PATCH;
    }

    if (method_str == "HEAD")
    {
        return crow::HTTPMethod::HEAD;
    }

    if (method_str == "OPTIONS")
    {
        return crow::HTTPMethod::OPTIONS;
    }

    return crow::HTTPMethod::GET;
}

void router::setup_lua_route_binding()
{
    if (binding_registered_)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(lua_.mutex());

    lua_.state().set_function(
        "route",
        [this](
            const std::string& method_str,
            const std::string& path,
            sol::object handler_obj,
            sol::optional<sol::table> options
        )
        {
            crow::HTTPMethod method = parse_method(method_str);
            int cache_ttl = 0;

            if (options && options.value().valid())
            {
                cache_ttl = options.value().get_or("cache", 0);

                if (cache_ttl <= 0)
                {
                    cache_ttl = options.value().get_or("cache_ttl", 0);
                }
            }

            if (handler_obj.is<std::string>())
            {
                std::string handler_name = handler_obj.as<std::string>();

                std::lock_guard<std::mutex> lk(mutex_);
                auto it = native_handlers_.find(handler_name);

                if (it == native_handlers_.end())
                {
                    LOG_ERROR("routes", "Handler '" << handler_name << "' not found for route: "
                              << method_str << " " << path);

                    return;
                }

                auto native_fn = it->second;

                server_.app().route_dynamic(path).methods(method)(
                    [native_fn, cache_ttl](const crow::request& req)
                    {
                        if (cache_ttl > 0 && req.method == crow::HTTPMethod::GET)
                        {
                            std::string cache_key = "GET:" + (req.raw_url.empty() ? req.url : req.raw_url);
                            cached_response cached;

                            if (s_cache_engine().get(cache_key, cached))
                            {
                                crow::response res(cached.status_code, cached.body);

                                res.add_header("Content-Type", cached.content_type);
                                res.add_header("X-Cache", "HIT");

                                return res;
                            }

                            auto res = native_fn(req);

                            if (res.code >= 200 && res.code < 300)
                            {
                                cached_response to_cache;

                                to_cache.status_code = res.code;
                                to_cache.body = res.body;

                                auto ct = res.get_header_value("Content-Type");

                                to_cache.content_type = ct.empty() ? "application/json" : ct;
                                s_cache_engine().set(cache_key, to_cache, std::chrono::seconds(cache_ttl));
                            }

                            res.add_header("X-Cache", "MISS");

                            return res;
                        }

                        return native_fn(req);
                    }
                );

                if (cache_ttl > 0)
                {
                    LOG_INFO("routes", "Mapped " << method_str << " " << path << " -> " << handler_name << " (cached " << cache_ttl << "s)");
                }
                else
                {
                    LOG_INFO("routes", "Mapped " << method_str << " " << path << " -> " << handler_name);
                }

                route_count_++;
            }
            else if (handler_obj.is<sol::protected_function>())
            {
                sol::protected_function lua_fn = handler_obj.as<sol::protected_function>();

                std::lock_guard<std::mutex> lk(mutex_);

                lua_handlers_.push_back(lua_fn);
                size_t handler_index = lua_handlers_.size() - 1;

                server_.app().route_dynamic(path).methods(method)(
                    [this, handler_index, cache_ttl](const crow::request& req)
                    {
                        if (cache_ttl > 0 && req.method == crow::HTTPMethod::GET)
                        {
                            std::string cache_key = "GET:" + (req.raw_url.empty() ? req.url : req.raw_url);
                            cached_response cached;

                            if (s_cache_engine().get(cache_key, cached))
                            {
                                crow::response res(cached.status_code, cached.body);

                                res.add_header("Content-Type", cached.content_type);
                                res.add_header("X-Cache", "HIT");

                                return res;
                            }
                        }

                        std::lock_guard<std::mutex> route_lock(lua_.mutex());

                        sol::table req_table = lua_.state().create_table();

                        req_table["body"] = req.body;
                        req_table["url"] = req.url;
                        req_table["raw_url"] = req.raw_url;
                        req_table["method"] = crow::method_name(req.method);
                        req_table["remote_addr"] = req.remote_ip_address;

                        auto result = lua_handlers_[handler_index](req_table);

                        if (!result.valid())
                        {
                            sol::error err = result;

                            LOG_ERROR("routes", "Lua route error: " << err.what());

                            return crow::response(500, "{\"error\":\"Internal Lua handler error\"}");
                        }

                        sol::table res_table = result.get<sol::table>();
                        int status = res_table.get_or("status", 200);
                        std::string body = res_table.get_or<std::string>("body", "");
                        std::string content_type = res_table.get_or<std::string>("content_type", "application/json");

                        if (cache_ttl > 0 && status >= 200 && status < 300)
                        {
                            std::string cache_key = "GET:" + (req.raw_url.empty() ? req.url : req.raw_url);
                            cached_response to_cache;

                            to_cache.status_code = status;
                            to_cache.body = body;
                            to_cache.content_type = content_type;

                            s_cache_engine().set(cache_key, to_cache, std::chrono::seconds(cache_ttl));
                        }

                        crow::response res(status, body);

                        res.add_header("Content-Type", content_type);

                        if (cache_ttl > 0)
                        {
                            res.add_header("X-Cache", "MISS");
                        }

                        return res;
                    }
                );

                if (cache_ttl > 0)
                {
                    LOG_INFO("routes", "Mapped " << method_str << " " << path << " -> [Lua] (cached " << cache_ttl << "s)");
                }
                else
                {
                    LOG_INFO("routes", "Mapped " << method_str << " " << path << " -> [Lua]");
                }

                route_count_++;
            }
            else
            {
                LOG_ERROR("routes", "Invalid handler type for route: " << path);
            }
        }
    );

    binding_registered_ = true;
}

void router::load_routes(const std::string& script_path)
{
    setup_lua_route_binding();
    lua_.load_file(script_path);
}

void router::load_routes_from_dir(const std::string& dir_path)
{
    namespace fs = std::filesystem;

    if (!fs::exists(dir_path) || !fs::is_directory(dir_path))
    {
        return;
    }

    setup_lua_route_binding();

    for (const auto& entry : fs::recursive_directory_iterator(dir_path))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".lua")
        {
            if (entry.path() == fs::path(dir_path) / "routes.lua")
            {
                continue;
            }

            LOG_INFO("routes", "Loading route script: " << entry.path().string());
            lua_.load_file(entry.path().string());
        }
    }
}

size_t router::route_count() const
{
    return route_count_;
}

size_t router::handler_count() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return native_handlers_.size();
}

} // namespace api
