#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <print>
#include <string>
#include <thread>
#include <unistd.h>

#include "api/cache.hpp"
#include "api/config.hpp"
#include "api/connection_tracker.hpp"
#include "api/dashboard.hpp"
#include "api/logger.hpp"
#include "api/lua_engine.hpp"
#include "api/metrics.hpp"
#include "api/middleware.hpp"
#include "api/router.hpp"
#include "api/script_mgr.hpp"
#include "api/server.hpp"
#include "api/service_registry.hpp"
#include "api/thread_pool.hpp"

static std::atomic<bool> g_shutdown{false};

static void signal_handler(int /*sig*/)
{
    g_shutdown.store(true);
}

int main(int argc, char* argv[])
{
    std::string config_path = "config/server.lua";
    int port_override = -1;
    bool no_dashboard = false;
    bool run_background = false;

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];

        if (arg == "--config" && i + 1 < argc)
        {
            config_path = argv[++i];
        }
        else if (arg == "--port" && i + 1 < argc)
        {
            port_override = std::stoi(argv[++i]);
        }
        else if (arg == "--headless")
        {
            run_background = true;
            no_dashboard = true;
        }
        else if (arg == "--no-dashboard")
        {
            no_dashboard = true;
        }
        else if (arg == "--help")
        {
            std::println(
                "Usage: API-cli [options]\n"
                "  --config <path>  Configuration file (default: config/server.lua)\n"
                "  --port <N>       Override listening port\n"
                "  --headless       Run program in background (daemon mode)\n"
                "  --no-dashboard   Run in foreground without terminal UI\n"
                "  --help           Display this help"
            );

            return EXIT_SUCCESS;
        }
    }

    auto cfg = api::load_config(config_path);

    if (port_override > 0)
    {
        cfg.port = static_cast<uint16_t>(port_override);
    }

    if (no_dashboard)
    {
        cfg.dashboard_enabled = false;
    }

    if (run_background)
    {
        pid_t pid = fork();

        if (pid < 0)
        {
            std::println(stderr, "Failed to fork background process: {}", std::strerror(errno));

            return EXIT_FAILURE;
        }

        if (pid > 0)
        {
            std::println("API-cli running in background (PID: {}, Port: {})", pid, cfg.port);

            return EXIT_SUCCESS;
        }

        setsid();

        int dev_null = open("/dev/null", O_RDWR);

        if (dev_null >= 0)
        {
            dup2(dev_null, STDIN_FILENO);
            dup2(dev_null, STDOUT_FILENO);
            dup2(dev_null, STDERR_FILENO);

            if (dev_null > STDERR_FILENO)
            {
                close(dev_null);
            }
        }
    }

    api::init_logging(cfg);

    LOG_INFO("server", "Starting API-cli");

    if (cfg.cache_enabled)
    {
        api::s_cache_engine().set_max_items(cfg.cache_max_items);
        api::s_services().register_service<api::cache_engine>(
            "cache",
            std::shared_ptr<api::cache_engine>(
                &api::s_cache_engine(),
                [](api::cache_engine*)
                {
                }
            )
        );
    }

    api::s_thread_pool().start(cfg.worker_threads);

    auto& sm = api::s_script_mgr();

    sm.initialize();
    sm.on_config_load(cfg);

    auto tracker = std::make_shared<api::connection_tracker>();

    api::request_logger::set_tracker(tracker);
    api::cors_handler::set_allowed_origins(cfg.cors_origins);

    api::metrics_collector metrics;

    metrics.start();

    api::lua_engine lua_eng;

    lua_eng.setup_package_path(cfg.scripts_dir);
    lua_eng.bind_core_api(&metrics, tracker.get());
    sm.on_lua_init(lua_eng);

    api::api_server server(cfg);
    api::router rtr(server, lua_eng);

    rtr.register_native_handler(
        "core.health",
        [](const crow::request& /*req*/)
        {
            return crow::response(200, "{\"status\":\"ok\"}");
        }
    );

    rtr.register_native_handler(
        "core.info",
        [](const crow::request& /*req*/)
        {
            return crow::response(200, "{\"name\":\"API-cli\",\"version\":\"1.0.0\"}");
        }
    );

    rtr.register_native_handler(
        "core.stats",
        [tracker](const crow::request& /*req*/)
        {
            auto stats = tracker->get_stats();
            crow::json::wvalue res;

            res["active_connections"] = stats.active_connections;
            res["total_connections"] = stats.total_connections;
            res["requests_per_second"] = stats.requests_per_second;
            res["worker_threads"] = api::s_thread_pool().thread_count();
            res["active_tasks"] = api::s_thread_pool().active_tasks();
            res["pending_tasks"] = api::s_thread_pool().pending_tasks();
            res["completed_tasks"] = api::s_thread_pool().completed_tasks();

            return crow::response(200, res);
        }
    );

    rtr.register_native_handler(
        "core.metrics",
        [&metrics](const crow::request& /*req*/)
        {
            auto snap = metrics.get_snapshot();
            auto cstats = api::s_cache_engine().get_stats();
            crow::json::wvalue res;

            res["cpu_usage_percent"] = snap.cpu_usage_percent;
            res["memory_rss_bytes"] = snap.memory_rss_bytes;
            res["memory_vsize_bytes"] = snap.memory_vsize_bytes;
            res["thread_count"] = snap.thread_count;
            res["open_fds"] = snap.open_fds;
            res["net_rx_bytes_per_sec"] = snap.net_rx_bytes_per_sec;
            res["net_tx_bytes_per_sec"] = snap.net_tx_bytes_per_sec;
            res["cache_hits"] = cstats.hits;
            res["cache_misses"] = cstats.misses;
            res["cache_items"] = cstats.items;
            res["cache_evictions"] = cstats.evictions;
            res["cache_hit_ratio_percent"] = cstats.hit_ratio_percent;

            return crow::response(200, res);
        }
    );

    sm.on_handlers_register(rtr);

    try
    {
        rtr.load_routes(cfg.scripts_dir + "/routes.lua");
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("routes", "Failed to load routes.lua: " << e.what());
    }

    rtr.load_routes_from_dir(cfg.scripts_dir);

    LOG_INFO("server", "Registered " << rtr.route_count() << " route(s)");

    server.start();

    LOG_INFO(
        "server",
        "Server listening on port " << cfg.port
        << " with " << cfg.threads << " HTTP worker thread(s) and "
        << cfg.worker_threads << " background task thread(s)"
    );

    sm.on_server_startup(server);

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (cfg.dashboard_enabled)
    {
        api::dashboard dash(metrics, *tracker, api::get_log_ring_buffer(), &lua_eng);

        dash.run();
    }
    else
    {
        LOG_INFO("server", "Running in headless mode (Ctrl-C to stop)");

        while (!g_shutdown.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }

    LOG_INFO("server", "Shutting down...");

    sm.on_server_shutdown();
    server.stop();
    metrics.stop();
    api::s_thread_pool().stop();

    LOG_INFO("server", "Shutdown complete.");

    return EXIT_SUCCESS;
}
