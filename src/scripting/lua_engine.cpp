#include "api/lua_engine.hpp"
#include "api/cache.hpp"
#include "api/connection_tracker.hpp"
#include "api/logger.hpp"
#include "api/metrics.hpp"
#include "api/script_mgr.hpp"

#include <filesystem>
#include <iomanip>
#include <sstream>

namespace api
{

lua_engine::lua_engine()
    : start_time_(std::chrono::steady_clock::now())
{
    lua_.open_libraries(
        sol::lib::base,
        sol::lib::string,
        sol::lib::table,
        sol::lib::math,
        sol::lib::io,
        sol::lib::os,
        sol::lib::package
    );

    lua_.set_function(
        "log_info",
        [](sol::variadic_args args)
        {
            if (args.size() >= 2)
            {
                LOG_INFO(args[0].as<std::string>(), args[1].as<std::string>());
            }
            else if (args.size() == 1)
            {
                LOG_INFO("scripts", args[0].as<std::string>());
            }
        }
    );

    lua_.set_function(
        "log_warn",
        [](sol::variadic_args args)
        {
            if (args.size() >= 2)
            {
                LOG_WARN(args[0].as<std::string>(), args[1].as<std::string>());
            }
            else if (args.size() == 1)
            {
                LOG_WARN("scripts", args[0].as<std::string>());
            }
        }
    );

    lua_.set_function(
        "log_error",
        [](sol::variadic_args args)
        {
            if (args.size() >= 2)
            {
                LOG_ERROR(args[0].as<std::string>(), args[1].as<std::string>());
            }
            else if (args.size() == 1)
            {
                LOG_ERROR("scripts", args[0].as<std::string>());
            }
        }
    );
}

lua_engine::~lua_engine() = default;

void lua_engine::setup_package_path(const std::string& base_dir)
{
    std::lock_guard<std::mutex> lock(mutex_);

    std::string base = base_dir;

    if (!base.empty() && base.back() != '/')
    {
        base += '/';
    }

    std::string extra_paths = ";" + base + "?.lua;" + base + "?/init.lua;"
                            + base + "*/*.lua;" + base + "*/*/init.lua";

    namespace fs = std::filesystem;

    if (fs::exists(base_dir) && fs::is_directory(base_dir))
    {
        for (const auto& entry : fs::recursive_directory_iterator(base_dir))
        {
            if (entry.is_directory())
            {
                std::string dir_path = entry.path().string();

                extra_paths += ";" + dir_path + "/?.lua;" + dir_path + "/?/init.lua";
            }
        }
    }

    std::string script = "package.path = package.path .. [[" + extra_paths + "]]";

    lua_.script(script);
}

void lua_engine::bind_core_api(metrics_collector* metrics, connection_tracker* tracker)
{
    std::lock_guard<std::mutex> lock(mutex_);

    sol::table api_tbl = lua_.create_named_table("api");

    api_tbl["version"] = "1.0.0";

    api_tbl["uptime"] = [this]() -> double
    {
        auto now = std::chrono::steady_clock::now();

        return std::chrono::duration<double>(now - start_time_).count();
    };

    api_tbl["format_bytes"] = [](size_t bytes) -> std::string
    {
        const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        double val = static_cast<double>(bytes);
        int u = 0;

        while (val >= 1024.0 && u < 4)
        {
            val /= 1024.0;
            ++u;
        }

        std::ostringstream oss;

        oss.precision(1);
        oss << std::fixed << val << " " << units[u];

        return oss.str();
    };

    if (tracker)
    {
        api_tbl["get_stats"] = [this, tracker]() -> sol::table
        {
            auto stats = tracker->get_stats();
            sol::table t = lua_.create_table();

            t["active_connections"] = stats.active_connections;
            t["total_connections"] = stats.total_connections;
            t["requests_per_second"] = stats.requests_per_second;

            return t;
        };
    }

    if (metrics)
    {
        api_tbl["get_metrics"] = [this, metrics]() -> sol::table
        {
            auto snap = metrics->get_snapshot();
            sol::table t = lua_.create_table();

            t["cpu_percent"] = snap.cpu_usage_percent;
            t["memory_rss_bytes"] = snap.memory_rss_bytes;
            t["memory_vsize_bytes"] = snap.memory_vsize_bytes;
            t["thread_count"] = snap.thread_count;
            t["open_fds"] = snap.open_fds;
            t["net_rx_bytes_sec"] = snap.net_rx_bytes_per_sec;
            t["net_tx_bytes_sec"] = snap.net_tx_bytes_per_sec;

            return t;
        };
    }

    sol::table cache_tbl = lua_.create_table();

    cache_tbl["get"] = [this](const std::string& key) -> sol::object
    {
        cached_response res;

        if (s_cache_engine().get(key, res))
        {
            return sol::make_object(lua_.lua_state(), res.body);
        }

        return sol::nil;
    };

    cache_tbl["set"] = [](
        const std::string& key,
        const std::string& val,
        sol::optional<int> ttl_sec
    )
    {
        int ttl = ttl_sec.value_or(60);

        s_cache_engine().set(key, 200, val, "text/plain", std::chrono::seconds(ttl));
    };

    cache_tbl["del"] = [](const std::string& key) -> bool
    {
        return s_cache_engine().remove(key);
    };

    cache_tbl["clear"] = []()
    {
        s_cache_engine().clear();
    };

    cache_tbl["stats"] = [this]() -> sol::table
    {
        auto st = s_cache_engine().get_stats();
        sol::table t = lua_.create_table();

        t["hits"] = st.hits;
        t["misses"] = st.misses;
        t["items"] = st.items;
        t["evictions"] = st.evictions;
        t["hit_ratio_percent"] = st.hit_ratio_percent;

        return t;
    };

    api_tbl["cache"] = cache_tbl;

    sol::table log_tbl = lua_.create_named_table("log");

    log_tbl["info"] = [](sol::variadic_args args)
    {
        if (args.size() >= 2)
        {
            LOG_INFO(args[0].as<std::string>(), args[1].as<std::string>());
        }
        else if (args.size() == 1)
        {
            LOG_INFO("scripts", args[0].as<std::string>());
        }
    };

    log_tbl["warn"] = [](sol::variadic_args args)
    {
        if (args.size() >= 2)
        {
            LOG_WARN(args[0].as<std::string>(), args[1].as<std::string>());
        }
        else if (args.size() == 1)
        {
            LOG_WARN("scripts", args[0].as<std::string>());
        }
    };

    log_tbl["error"] = [](sol::variadic_args args)
    {
        if (args.size() >= 2)
        {
            LOG_ERROR(args[0].as<std::string>(), args[1].as<std::string>());
        }
        else if (args.size() == 1)
        {
            LOG_ERROR("scripts", args[0].as<std::string>());
        }
    };

    log_tbl["debug"] = [](sol::variadic_args args)
    {
        if (args.size() >= 2)
        {
            LOG_DEBUG(args[0].as<std::string>(), args[1].as<std::string>());
        }
        else if (args.size() == 1)
        {
            LOG_DEBUG("scripts", args[0].as<std::string>());
        }
    };

    sol::table mods_tbl = lua_.create_named_table("modules");

    mods_tbl["list"] = [this]() -> sol::table
    {
        sol::table list = lua_.create_table();
        auto mods = s_script_mgr().get_module_scripts();

        for (size_t i = 0; i < mods.size(); ++i)
        {
            sol::table item = lua_.create_table();

            item["name"] = mods[i]->get_name();
            item["version"] = mods[i]->get_version();
            item["description"] = mods[i]->get_description();
            list[i + 1] = item;
        }

        return list;
    };

    mods_tbl["is_loaded"] = [](const std::string& name) -> bool
    {
        auto mods = s_script_mgr().get_module_scripts();

        for (const auto* m : mods)
        {
            if (m->get_name() == name)
            {
                return true;
            }
        }

        return false;
    };
}

void lua_engine::load_file(const std::string& path)
{
    std::lock_guard<std::mutex> lock(mutex_);

    try
    {
        lua_.script_file(path);
    }
    catch (const sol::error& e)
    {
        LOG_ERROR("scripts", "Lua error in " << path << ": " << e.what());
    }
}

sol::state& lua_engine::state()
{
    return lua_;
}

std::mutex& lua_engine::mutex()
{
    return mutex_;
}

} // namespace api
