#include "api/config.h"

#include <filesystem>
#include <print>

#include <sol/sol.hpp>

namespace api
{

ServerConfig load_config(const std::string &lua_configuration_path)
{
    ServerConfig configuration;

    if (!std::filesystem::exists(lua_configuration_path))
    {
        std::println(stderr, "[config] File not found: {} — using defaults",
                     lua_configuration_path);

        return configuration;
    }

    try
    {
        sol::state lua_state;

        lua_state.open_libraries(sol::lib::base, sol::lib::string,
                                 sol::lib::table, sol::lib::math);

        lua_state.script_file(lua_configuration_path);

        sol::optional<sol::table> config_table_optional = lua_state["config"];

        if (!config_table_optional)
        {
            std::println(stderr,
                         "[config] No 'config' table in {} — using defaults",
                         lua_configuration_path);

            return configuration;
        }

        sol::table config_table = config_table_optional.value();

        configuration.port =
            config_table.get_or<uint16_t>("port", configuration.port);
        configuration.threads =
            config_table.get_or<uint16_t>("threads", configuration.threads);
        configuration.worker_threads = config_table.get_or<uint16_t>(
            "worker_threads", configuration.worker_threads);
        configuration.log_level = config_table.get_or<std::string>(
            "log_level", configuration.log_level);
        configuration.log_dir =
            config_table.get_or<std::string>("log_dir", configuration.log_dir);
        configuration.dashboard_enabled = config_table.get_or<bool>(
            "dashboard_enabled", configuration.dashboard_enabled);
        configuration.max_body_size = config_table.get_or<size_t>(
            "max_body_size", configuration.max_body_size);
        configuration.scripts_dir = config_table.get_or<std::string>(
            "scripts_dir", configuration.scripts_dir);
        configuration.config_dir = config_table.get_or<std::string>(
            "config_dir", configuration.config_dir);
        configuration.cache_enabled = config_table.get_or<bool>(
            "cache_enabled", configuration.cache_enabled);
        configuration.cache_max_items = config_table.get_or<size_t>(
            "cache_max_items", configuration.cache_max_items);

        sol::optional<sol::table> origins = config_table["cors_origins"];

        if (origins)
        {
            configuration.cors_origins.clear();

            origins.value().for_each(
                [&](sol::object /*key*/, sol::object origin_value)
                {
                    configuration.cors_origins.push_back(
                        origin_value.as<std::string>());
                });
        }

        sol::optional<sol::table> channels = config_table["log_channels"];

        if (channels)
        {
            channels.value().for_each(
                [&](sol::object channel_key, sol::object channel_value)
                {
                    if (channel_key.is<std::string>() &&
                        channel_value.is<std::string>())
                    {
                        configuration
                            .log_channels[channel_key.as<std::string>()] =
                            channel_value.as<std::string>();
                    }
                });
        }

        sol::optional<sol::table> database_table_optional = config_table["db"];

        if (database_table_optional)
        {
            configuration.db_driver =
                database_table_optional.value().get_or<std::string>(
                    "driver", configuration.db_driver);
            configuration.db_connection =
                database_table_optional.value().get_or<std::string>(
                    "connection", configuration.db_connection);
        }

        configuration.db_driver = config_table.get_or<std::string>(
            "db_driver", configuration.db_driver);
        configuration.db_connection = config_table.get_or<std::string>(
            "db_connection", configuration.db_connection);
        configuration.htaccess_file = config_table.get_or<std::string>(
            "htaccess_file", configuration.htaccess_file);
    }
    catch (const sol::error &sol_exception)
    {
        std::println(stderr, "[config] Lua error: {} — using defaults",
                     sol_exception.what());
    }

    return configuration;
}

} // namespace api
