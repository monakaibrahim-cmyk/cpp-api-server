#include "api/config.hpp"

#include <filesystem>
#include <print>

#include <sol/sol.hpp>

namespace api
{

server_config load_config(const std::string& lua_config_path)
{
    server_config cfg;

    if (!std::filesystem::exists(lua_config_path))
    {
        std::println(stderr, "[config] File not found: {} — using defaults", lua_config_path);

        return cfg;
    }

    try
    {
        sol::state lua;

        lua.open_libraries(
            sol::lib::base,
            sol::lib::string,
            sol::lib::table,
            sol::lib::math);

        lua.script_file(lua_config_path);

        sol::optional<sol::table> tbl = lua["config"];

        if (!tbl)
        {
            std::println(stderr, "[config] No 'config' table in {} — using defaults", lua_config_path);

            return cfg;
        }

        sol::table t = tbl.value();

        cfg.port = t.get_or<uint16_t>("port", cfg.port);
        cfg.threads = t.get_or<uint16_t>("threads", cfg.threads);
        cfg.worker_threads = t.get_or<uint16_t>("worker_threads", cfg.worker_threads);
        cfg.log_level = t.get_or<std::string>("log_level", cfg.log_level);
        cfg.log_dir = t.get_or<std::string>("log_dir", cfg.log_dir);
        cfg.dashboard_enabled = t.get_or<bool>("dashboard_enabled", cfg.dashboard_enabled);
        cfg.max_body_size = t.get_or<size_t>("max_body_size", cfg.max_body_size);
        cfg.scripts_dir = t.get_or<std::string>("scripts_dir", cfg.scripts_dir);
        cfg.config_dir = t.get_or<std::string>("config_dir", cfg.config_dir);
        cfg.cache_enabled = t.get_or<bool>("cache_enabled", cfg.cache_enabled);
        cfg.cache_max_items = t.get_or<size_t>("cache_max_items", cfg.cache_max_items);

        sol::optional<sol::table> origins = t["cors_origins"];

        if (origins)
        {
            cfg.cors_origins.clear();

            origins.value().for_each(
                [&](sol::object /*key*/, sol::object val)
                {
                    cfg.cors_origins.push_back(val.as<std::string>());
                });
        }

        sol::optional<sol::table> channels = t["log_channels"];

        if (channels)
        {
            channels.value().for_each(
                [&](sol::object key, sol::object val)
                {
                    if (key.is<std::string>() && val.is<std::string>())
                    {
                        cfg.log_channels[key.as<std::string>()] = val.as<std::string>();
                    }
                });
        }
    }
    catch (const sol::error& e)
    {
        std::println(stderr, "[config] Lua error: {} — using defaults", e.what());
    }

    return cfg;
}

} // namespace api
