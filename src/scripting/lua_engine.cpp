#include "api/lua_engine.h"
#include "api/cache.h"
#include "api/config.h"
#include "api/connection_tracker.h"
#include "api/htaccess.h"
#include "api/logger.h"
#include "api/metrics.h"
#include "api/script_mgr.h"
#include "api/thread_pool.h"

#include <filesystem>
#include <iomanip>
#include <sstream>

#include <crow/json.h>

namespace api
{

lua_engine::lua_engine() : start_time_(std::chrono::steady_clock::now())
{
    lua_.open_libraries(sol::lib::base, sol::lib::string, sol::lib::table,
                        sol::lib::math, sol::lib::io, sol::lib::os,
                        sol::lib::package);

    lua_.set_function("log_info",
                      [](sol::variadic_args arguments)
                      {
                          if (arguments.size() >= 2)
                          {
                              LOG_INFO(arguments[0].as<std::string>(),
                                       arguments[1].as<std::string>());
                          }
                          else if (arguments.size() == 1)
                          {
                              LOG_INFO("scripts",
                                       arguments[0].as<std::string>());
                          }
                      });

    lua_.set_function("log_warn",
                      [](sol::variadic_args arguments)
                      {
                          if (arguments.size() >= 2)
                          {
                              LOG_WARN(arguments[0].as<std::string>(),
                                       arguments[1].as<std::string>());
                          }
                          else if (arguments.size() == 1)
                          {
                              LOG_WARN("scripts",
                                       arguments[0].as<std::string>());
                          }
                      });

    lua_.set_function("log_error",
                      [](sol::variadic_args arguments)
                      {
                          if (arguments.size() >= 2)
                          {
                              LOG_ERROR(arguments[0].as<std::string>(),
                                        arguments[1].as<std::string>());
                          }
                          else if (arguments.size() == 1)
                          {
                              LOG_ERROR("scripts",
                                        arguments[0].as<std::string>());
                          }
                      });
}

lua_engine::~lua_engine() = default;

void lua_engine::setup_package_path(const std::string &base_directory)
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::string base_path = base_directory;

    if (!base_path.empty() && base_path.back() != '/')
    {
        base_path += '/';
    }

    std::string extra_paths = ";" + base_path + "?.lua;" + base_path +
                              "?/init.lua;" + base_path + "*/*.lua;" +
                              base_path + "*/*/init.lua";

    namespace filesystem = std::filesystem;

    if (filesystem::exists(base_directory) &&
        filesystem::is_directory(base_directory))
    {
        for (const auto &entry :
             filesystem::recursive_directory_iterator(base_directory))
        {
            if (entry.is_directory())
            {
                std::string directory_path = entry.path().string();

                extra_paths += ";" + directory_path + "/?.lua;" +
                               directory_path + "/?/init.lua";
            }
        }
    }

    if (filesystem::exists("modules") && filesystem::is_directory("modules"))
    {
        extra_paths += ";modules/?/lua/?.lua;modules/?/lua/?/init.lua";
        extra_paths +=
            ";modules/mod_orm/lua/?.lua;modules/mod_orm/lua/?/init.lua";
    }

    std::string script =
        "package.path = package.path .. [[" + extra_paths + "]]";

    lua_.script(script);
}

void lua_engine::bind_core_api(metrics_collector *metrics_collector_instance,
                               connection_tracker *connection_tracker_instance,
                               const ServerConfig *server_configuration)
{
    std::lock_guard<std::mutex> lock(mutex_);

    lua_.new_usertype<ServerConfig>(
        "server_config", sol::no_constructor, "port", &ServerConfig::port,
        "threads", &ServerConfig::threads, "worker_threads",
        &ServerConfig::worker_threads, "log_level", &ServerConfig::log_level,
        "log_dir", &ServerConfig::log_dir, "dashboard_enabled",
        &ServerConfig::dashboard_enabled, "max_body_size",
        &ServerConfig::max_body_size, "scripts_dir", &ServerConfig::scripts_dir,
        "config_dir", &ServerConfig::config_dir, "cache_enabled",
        &ServerConfig::cache_enabled, "cache_max_items",
        &ServerConfig::cache_max_items, "db_driver", &ServerConfig::db_driver,
        "db_connection", &ServerConfig::db_connection, "htaccess_file",
        &ServerConfig::htaccess_file);

    lua_.new_usertype<ConnectionStats>(
        "connection_stats", sol::constructors<ConnectionStats()>(),
        "active_connections", &ConnectionStats::active_connections,
        "total_connections", &ConnectionStats::total_connections,
        "requests_per_second", &ConnectionStats::requests_per_second);

    lua_.new_usertype<SystemSnapshot>(
        "system_snapshot", sol::constructors<SystemSnapshot()>(),
        "cpu_usage_percent", &SystemSnapshot::cpu_usage_percent,
        "memory_rss_bytes", &SystemSnapshot::memory_rss_bytes,
        "memory_vsize_bytes", &SystemSnapshot::memory_vsize_bytes,
        "thread_count", &SystemSnapshot::thread_count, "open_fds",
        &SystemSnapshot::open_fds, "net_rx_bytes_per_sec",
        &SystemSnapshot::net_rx_bytes_per_sec, "net_tx_bytes_per_sec",
        &SystemSnapshot::net_tx_bytes_per_sec);

    lua_.new_usertype<CachedResponse>(
        "cached_response", sol::constructors<CachedResponse()>(), "status_code",
        &CachedResponse::status_code, "body", &CachedResponse::body,
        "content_type", &CachedResponse::content_type, "is_expired",
        &CachedResponse::is_expired);

    lua_.new_usertype<CacheStats>(
        "cache_stats", sol::constructors<CacheStats()>(), "hits",
        &CacheStats::hits, "misses", &CacheStats::misses, "items",
        &CacheStats::items, "evictions", &CacheStats::evictions,
        "hit_ratio_percent", &CacheStats::hit_ratio_percent);

    lua_.new_usertype<cache_engine>(
        "cache_engine", sol::no_constructor, "clear", &cache_engine::clear,
        "remove", &cache_engine::remove, "cleanup_expired",
        &cache_engine::cleanup_expired, "set_max_items",
        &cache_engine::set_max_items, "get_stats", &cache_engine::get_stats);

    lua_.new_usertype<connection_tracker>("connection_tracker",
                                          sol::no_constructor, "get_stats",
                                          &connection_tracker::get_stats);

    lua_.new_usertype<metrics_collector>("metrics_collector",
                                         sol::no_constructor, "get_snapshot",
                                         &metrics_collector::get_snapshot);

    lua_.new_usertype<thread_pool>(
        "thread_pool", sol::no_constructor, "thread_count",
        &thread_pool::thread_count, "active_tasks", &thread_pool::active_tasks,
        "pending_tasks", &thread_pool::pending_tasks, "completed_tasks",
        &thread_pool::completed_tasks, "is_running", &thread_pool::is_running);

    sol::table api_table = lua_.create_named_table("api");

    api_table["version"] = "1.0.0";
    api_table["threads"] = &s_thread_pool();
    api_table["cache_engine"] = &s_cache_engine();

    if (connection_tracker_instance)
    {
        api_table["tracker"] = connection_tracker_instance;
    }

    if (metrics_collector_instance)
    {
        api_table["metrics"] = metrics_collector_instance;
    }

    if (server_configuration)
    {
        api_table["config"] = server_configuration;
    }

    api_table["uptime"] = [this]() -> double
    {
        auto current_time = std::chrono::steady_clock::now();

        return std::chrono::duration<double>(current_time - start_time_)
            .count();
    };

    api_table["format_bytes"] = [](size_t bytes_count) -> std::string
    {
        const char *units[] = {"B", "KB", "MB", "GB", "TB"};
        double byte_value = static_cast<double>(bytes_count);
        int unit_index = 0;

        while (byte_value >= 1024.0 && unit_index < 4)
        {
            byte_value /= 1024.0;
            ++unit_index;
        }

        std::ostringstream output_string_stream;

        output_string_stream.precision(1);
        output_string_stream << std::fixed << byte_value << " "
                             << units[unit_index];

        return output_string_stream.str();
    };

    if (connection_tracker_instance)
    {
        api_table["get_stats"] = [this,
                                  connection_tracker_instance]() -> sol::table
        {
            auto tracker_stats = connection_tracker_instance->get_stats();
            sol::table result_table = lua_.create_table();

            result_table["active_connections"] =
                tracker_stats.active_connections;
            result_table["total_connections"] = tracker_stats.total_connections;
            result_table["requests_per_second"] =
                tracker_stats.requests_per_second;

            return result_table;
        };
    }

    if (metrics_collector_instance)
    {
        api_table["get_metrics"] = [this,
                                    metrics_collector_instance]() -> sol::table
        {
            auto snapshot = metrics_collector_instance->get_snapshot();
            sol::table result_table = lua_.create_table();

            result_table["cpu_percent"] = snapshot.cpu_usage_percent;
            result_table["memory_rss_bytes"] = snapshot.memory_rss_bytes;
            result_table["memory_vsize_bytes"] = snapshot.memory_vsize_bytes;
            result_table["thread_count"] = snapshot.thread_count;
            result_table["open_fds"] = snapshot.open_fds;
            result_table["net_rx_bytes_sec"] = snapshot.net_rx_bytes_per_sec;
            result_table["net_tx_bytes_sec"] = snapshot.net_tx_bytes_per_sec;

            return result_table;
        };
    }

    sol::table cache_table = lua_.create_table();

    cache_table["get"] = [this](const std::string &key) -> sol::object
    {
        CachedResponse cached_response_result;

        if (s_cache_engine().get(key, cached_response_result))
        {
            return sol::make_object(lua_.lua_state(),
                                    cached_response_result.body);
        }

        return sol::nil;
    };

    cache_table["set"] = [](const std::string &key, const std::string &value,
                            sol::optional<int> time_to_live_seconds)
    {
        int time_to_live = time_to_live_seconds.value_or(60);

        s_cache_engine().set(key, 200, value, "text/plain",
                             std::chrono::seconds(time_to_live));
    };

    cache_table["del"] = [](const std::string &key) -> bool
    { return s_cache_engine().remove(key); };

    cache_table["clear"] = []() { s_cache_engine().clear(); };

    cache_table["stats"] = [this]() -> sol::table
    {
        auto statistics = s_cache_engine().get_stats();
        sol::table result_table = lua_.create_table();

        result_table["hits"] = statistics.hits;
        result_table["misses"] = statistics.misses;
        result_table["items"] = statistics.items;
        result_table["evictions"] = statistics.evictions;
        result_table["hit_ratio_percent"] = statistics.hit_ratio_percent;

        return result_table;
    };

    api_table["cache"] = cache_table;

    sol::table log_table = lua_.create_named_table("log");

    log_table["info"] = [](sol::variadic_args arguments)
    {
        if (arguments.size() >= 2)
        {
            LOG_INFO(arguments[0].as<std::string>(),
                     arguments[1].as<std::string>());
        }
        else if (arguments.size() == 1)
        {
            LOG_INFO("scripts", arguments[0].as<std::string>());
        }
    };

    log_table["warn"] = [](sol::variadic_args arguments)
    {
        if (arguments.size() >= 2)
        {
            LOG_WARN(arguments[0].as<std::string>(),
                     arguments[1].as<std::string>());
        }
        else if (arguments.size() == 1)
        {
            LOG_WARN("scripts", arguments[0].as<std::string>());
        }
    };

    log_table["error"] = [](sol::variadic_args arguments)
    {
        if (arguments.size() >= 2)
        {
            LOG_ERROR(arguments[0].as<std::string>(),
                      arguments[1].as<std::string>());
        }
        else if (arguments.size() == 1)
        {
            LOG_ERROR("scripts", arguments[0].as<std::string>());
        }
    };

    log_table["debug"] = [](sol::variadic_args arguments)
    {
        if (arguments.size() >= 2)
        {
            LOG_DEBUG(arguments[0].as<std::string>(),
                      arguments[1].as<std::string>());
        }
        else if (arguments.size() == 1)
        {
            LOG_DEBUG("scripts", arguments[0].as<std::string>());
        }
    };

    sol::table modules_table = lua_.create_named_table("modules");

    modules_table["list"] = [this]() -> sol::table
    {
        sol::table list = lua_.create_table();
        auto module_scripts = s_script_mgr().get_module_scripts();

        for (size_t index = 0; index < module_scripts.size(); ++index)
        {
            sol::table item = lua_.create_table();

            item["name"] = module_scripts[index]->get_name();
            item["version"] = module_scripts[index]->get_version();
            item["description"] = module_scripts[index]->get_description();
            list[index + 1] = item;
        }

        return list;
    };

    modules_table["is_loaded"] = [](const std::string &name) -> bool
    {
        auto module_scripts = s_script_mgr().get_module_scripts();

        for (const auto *module_script : module_scripts)
        {
            if (module_script->get_name() == name)
            {
                return true;
            }
        }

        return false;
    };

    sol::table json_table = lua_.create_named_table("json");

    json_table["decode"] = [this](const std::string &json_string) -> sol::object
    {
        auto parsed_json = crow::json::load(json_string);

        if (!parsed_json)
        {
            return sol::make_object(lua_.lua_state(), sol::nil);
        }

        return json_to_lua(lua_, parsed_json);
    };

    json_table["encode"] = [](sol::object value) -> std::string
    { return lua_to_json(value); };

    s_htaccess().bind_lua(lua_);
}

sol::object lua_engine::json_to_lua(sol::state_view state,
                                    const crow::json::rvalue &value)
{
    switch (value.t())
    {
    case crow::json::type::Null:
        return sol::make_object(state, sol::nil);
    case crow::json::type::False:
        return sol::make_object(state, false);
    case crow::json::type::True:
        return sol::make_object(state, true);
    case crow::json::type::Number:
        if (value.nt() == crow::json::num_type::Floating_point ||
            value.nt() == crow::json::num_type::Double_precision_floating_point)
        {
            return sol::make_object(state, value.d());
        }
        return sol::make_object(state, value.i());
    case crow::json::type::String:
        return sol::make_object(state, std::string(value.s()));
    case crow::json::type::List:
    {
        sol::table array_table = state.create_table();
        size_t element_index = 1;

        for (const auto &child_item : value)
        {
            array_table[element_index++] = json_to_lua(state, child_item);
        }

        return array_table;
    }
    case crow::json::type::Object:
    {
        sol::table object_table = state.create_table();

        for (const auto &key_name : value.keys())
        {
            object_table[key_name] = json_to_lua(state, value[key_name]);
        }

        return object_table;
    }
    default:
        break;
    }

    return sol::make_object(state, sol::nil);
}

std::string lua_engine::lua_to_json(const sol::object &value)
{
    if (!value.valid() || value.is<sol::nil_t>())
    {
        return "null";
    }

    if (value.is<bool>())
    {
        return value.as<bool>() ? "true" : "false";
    }

    if (value.is<int64_t>())
    {
        return std::to_string(value.as<int64_t>());
    }

    if (value.is<double>())
    {
        std::ostringstream stream;

        stream << value.as<double>();
        return stream.str();
    }

    if (value.is<std::string>())
    {
        crow::json::wvalue string_value(value.as<std::string>());

        return string_value.dump();
    }

    if (value.is<sol::table>())
    {
        sol::table table = value.as<sol::table>();
        size_t array_length = table.size();
        bool is_array = true;

        if (array_length == 0)
        {
            bool has_keys = false;

            table.for_each([&has_keys](sol::object /*key*/, sol::object /*val*/)
                           { has_keys = true; });

            if (!has_keys)
            {
                return "{}";
            }

            is_array = false;
        }
        else
        {
            table.for_each(
                [&is_array](sol::object key, sol::object /*val*/)
                {
                    if (!key.is<int64_t>())
                    {
                        is_array = false;
                    }
                });
        }

        if (is_array)
        {
            std::string serialized = "[";

            for (size_t index = 1; index <= array_length; ++index)
            {
                if (index > 1)
                {
                    serialized += ",";
                }

                serialized += lua_to_json(table[index]);
            }

            serialized += "]";
            return serialized;
        }
        else
        {
            std::string serialized = "{";
            bool first_entry = true;

            table.for_each(
                [&serialized, &first_entry](sol::object key, sol::object val)
                {
                    if (!first_entry)
                    {
                        serialized += ",";
                    }

                    first_entry = false;

                    std::string key_string;

                    if (key.is<std::string>())
                    {
                        key_string = key.as<std::string>();
                    }
                    else
                    {
                        key_string = std::to_string(key.as<int64_t>());
                    }

                    crow::json::wvalue key_json(key_string);

                    serialized += key_json.dump() + ":" + lua_to_json(val);
                });

            serialized += "}";
            return serialized;
        }
    }

    return "null";
}

void lua_engine::load_file(const std::string &script_path)
{
    std::lock_guard<std::mutex> lock(mutex_);

    try
    {
        lua_.script_file(script_path);
    }
    catch (const sol::error &exception)
    {
        LOG_ERROR("scripts",
                  "Lua error in " << script_path << ": " << exception.what());
    }
}

sol::state &lua_engine::state() { return lua_; }

std::mutex &lua_engine::mutex() { return mutex_; }

} // namespace api
