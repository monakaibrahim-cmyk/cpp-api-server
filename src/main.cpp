#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <print>
#include <sstream>
#include <string>
#include <thread>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <core/cache.h>
#include <core/config.h>
#include <server/connection_tracker.h>
#include <dashboard/dashboard.h>
#include <core/logger.h>
#include <scripting/lua_engine.h>
#include <core/metrics.h>
#include <server/middleware.h>
#include <server/router.h>
#include <scripting/script_mgr.h>
#include <server/server.h>
#include <core/service_registry.h>
#include <core/thread_pool.h>

#include <globals.h>

static std::atomic<bool> g_shutdown{false};

static void signal_handler(int /*signal_number*/) { g_shutdown.store(true); }

int main(int argc, char *argv[])
{
    std::string config_path = CONFIG_PATH;
    int port_override = -1;
    bool no_dashboard = false;
#if defined(_WIN32) || defined(_WIN64)
    bool background_child = false;
#endif
    bool run_background = false;

    for (int index = 1; index < argc; ++index)
    {
        std::string argument = argv[index];

        if (argument == "--config" && index + 1 < argc)
        {
            config_path = argv[++index];
        }
        else if (argument == "--port" && index + 1 < argc)
        {
            port_override = std::stoi(argv[++index]);
        }
        else if (argument == "--headless")
        {
            run_background = true;
            no_dashboard = true;
        }
#if defined(_WIN32) || defined(_WIN64)
        else if (argument == "--background-child")
        {
            background_child = true;
            run_background = true;
            no_dashboard = true;
        }
#endif
        else if (argument == "--no-dashboard")
        {
            no_dashboard = true;
        }
        else if (argument == "--help")
        {
            std::println(
                "Usage: API-cli [options]\n"
                "  --config <path>               Configuration file (default: "
                "config/server.lua)\n"
                "  --port <N>                    Override listening port\n"
                "  --headless                    Run program in background "
                "(daemon mode)\n"
                "  --no-dashboard                Run in foreground without "
                "terminal UI\n"
                "  --help                        Display this help");

            return EXIT_SUCCESS;
        }
    }

    auto configuration = api::load_config(config_path);

    if (port_override > 0)
    {
        configuration.port = static_cast<uint16_t>(port_override);
    }

    if (no_dashboard)
    {
        configuration.dashboard_enabled = false;
    }

#if defined(_WIN32) || defined(_WIN64)
    if (run_background && !background_child)
    {
        char executable[MAX_PATH];

        if (GetModuleFileNameA(nullptr, executable, MAX_PATH) == 0)
        {
            std::println(stderr, "Failed to determine executable path: {}",
                GetLastError());

            return EXIT_FAILURE;
        }
        std::string parameter = "\"" + std::string(executable) + "\"";

        for (int index = 1; index < argc; ++index)
        {
            std::string argument = argv[index];

            if (argument == "--headless")
            {
                parameter += " --background-child";
            }
            else
            {
                parameter += " \"";
                parameter += argument;
                parameter += "\"";
            }
        }

        STARTUPINFO startup_info{};
        startup_info.cb = sizeof(startup_info);

        PROCESS_INFORMATION process_info{};

        if (!CreateProcessA(nullptr, parameter.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &startup_info, &process_info))
        {
            std::println(stderr, "Failed to start background process: {}", GetLastError());

            return EXIT_FAILURE;
        }

        std::println("API-cli running in background (PID: {}, Port: {})", process_info.dwProcessId, configuration.port);

        CloseHandle(process_info.hThread);
        CloseHandle(process_info.hProcess);

        return EXIT_SUCCESS;
    }
#else
    if (run_background)
    {
        pid_t process_id = fork();

        if (process_id < 0)
        {
            std::println(stderr, "Failed to fork background process: {}",
                         std::strerror(errno));

            return EXIT_FAILURE;
        }

        if (process_id > 0)
        {
            std::println("API-cli running in background (PID: {}, Port: {})",
                         process_id, configuration.port);

            return EXIT_SUCCESS;
        }

        setsid();

        int dev_null_file_descriptor = open("/dev/null", O_RDWR);

        if (dev_null_file_descriptor >= 0)
        {
            dup2(dev_null_file_descriptor, STDIN_FILENO);
            dup2(dev_null_file_descriptor, STDOUT_FILENO);
            dup2(dev_null_file_descriptor, STDERR_FILENO);

            if (dev_null_file_descriptor > STDERR_FILENO)
            {
                close(dev_null_file_descriptor);
            }
        }
    }
#endif

    api::init_logging(configuration);

    LOG_INFO("server", "Starting API-cli");

    if (configuration.cache_enabled)
    {
        api::s_cache_engine().set_max_items(configuration.cache_max_items);
        api::s_services().register_service<api::cache_engine>(
            "cache", std::shared_ptr<api::cache_engine>(
                         &api::s_cache_engine(), [](api::cache_engine *) {}));
    }

    api::s_thread_pool().start(configuration.worker_threads);

    auto &script_manager = api::s_script_mgr();

    script_manager.initialize();
    script_manager.on_config_load(configuration);

    auto tracker = std::make_shared<api::connection_tracker>();

    api::RequestLogger::set_tracker(tracker);
    api::CorsHandler::set_allowed_origins(configuration.cors_origins);

    api::metrics_collector metrics;

    metrics.start();

    api::lua_engine lua_engine_instance;

    lua_engine_instance.setup_package_path(configuration.scripts_dir);
    lua_engine_instance.bind_core_api(&metrics, tracker.get(), &configuration);
    script_manager.on_lua_init(lua_engine_instance);

    api::api_server server(configuration);
    api::router server_router(server, lua_engine_instance);

    server_router.register_native_handler(
        "core.health", [](const crow::request & /*request*/)
        { return crow::response(200, "{\"status\":\"ok\"}"); });

    server_router.register_native_handler(
        "core.info",
        [](const crow::request & /*request*/)
        {
            return crow::response(
                200, "{\"name\":\"" APP_NAME "\",\"version\":\"" VERSION_STRING "\"}");
        });

    server_router.register_native_handler(
        "core.stats",
        [tracker](const crow::request & /*request*/)
        {
            auto tracker_stats = tracker->get_stats();
            crow::json::wvalue response_json;

            response_json["active_connections"] =
                tracker_stats.active_connections;
            response_json["total_connections"] =
                tracker_stats.total_connections;
            response_json["total_rx_bytes"] =
                tracker_stats.total_received_bytes;
            response_json["total_tx_bytes"] =
                tracker_stats.total_transmitted_bytes;
            response_json["requests_per_second"] =
                tracker_stats.requests_per_second;
            response_json["worker_threads"] =
                api::s_thread_pool().thread_count();
            response_json["active_tasks"] = api::s_thread_pool().active_tasks();
            response_json["pending_tasks"] =
                api::s_thread_pool().pending_tasks();
            response_json["completed_tasks"] =
                api::s_thread_pool().completed_tasks();

            return crow::response(200, response_json);
        });

    server_router.register_native_handler(
        "core.metrics",
        [&metrics](const crow::request & /*request*/)
        {
            auto snapshot = metrics.get_snapshot();
            auto cache_statistics = api::s_cache_engine().get_stats();
            crow::json::wvalue response_json;

            response_json["cpu_usage_percent"] = snapshot.cpu_usage_percent;
            response_json["memory_rss_bytes"] = snapshot.memory_rss_bytes;
            response_json["memory_vsize_bytes"] = snapshot.memory_vsize_bytes;
            response_json["thread_count"] = snapshot.thread_count;
            response_json["open_fds"] = snapshot.open_fds;
            response_json["net_rx_bytes_per_sec"] =
                snapshot.net_rx_bytes_per_sec;
            response_json["net_tx_bytes_per_sec"] =
                snapshot.net_tx_bytes_per_sec;
            response_json["net_rx_total_bytes"] = snapshot.net_rx_total_bytes;
            response_json["net_tx_total_bytes"] = snapshot.net_tx_total_bytes;
            response_json["cache_hits"] = cache_statistics.hits;
            response_json["cache_misses"] = cache_statistics.misses;
            response_json["cache_items"] = cache_statistics.items;
            response_json["cache_evictions"] = cache_statistics.evictions;
            response_json["cache_hit_ratio_percent"] =
                cache_statistics.hit_ratio_percent;

            return crow::response(200, response_json);
        });

    script_manager.on_handlers_register(server_router);

    try
    {
        server_router.load_routes(configuration.scripts_dir + "/routes.lua");
    }
    catch (const std::exception &exception)
    {
        LOG_ERROR("routes", "Failed to load routes.lua: " << exception.what());
    }

    server_router.load_routes_from_dir(configuration.scripts_dir);

    LOG_INFO("server",
             "Registered " << server_router.route_count() << " route(s)");

    server.start();

    LOG_INFO("server", "Server listening on port "
                           << configuration.port << " with "
                           << configuration.threads
                           << " HTTP worker thread(s) and "
                           << configuration.worker_threads
                           << " background task thread(s)");

    script_manager.on_server_startup(server);

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    if (configuration.dashboard_enabled)
    {
        api::dashboard terminal_dashboard(metrics, *tracker,
                                          api::get_log_ring_buffer(),
                                          &lua_engine_instance);

        terminal_dashboard.run();
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

    script_manager.on_server_shutdown();
    server.stop();
    metrics.stop();
    api::s_thread_pool().stop();

    LOG_INFO("server", "Shutdown complete.");

    return EXIT_SUCCESS;
}
