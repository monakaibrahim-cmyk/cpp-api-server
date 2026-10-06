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
#include <core/orm.h>
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

    std::string make_model_name;
    std::string make_migration_name;
    std::string table_override;
    std::string db_scaffold_target;
    std::string models_dir = MODEL_DIRECTORY;
    std::string migrations_dir = MIGRATION_DIRECTORY;
    bool list_db_tables = false;

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
        else if (argument == "--make:model" && index + 1 < argc)
        {
            make_model_name = argv[++index];
        }
        else if (argument == "--make:migration" && index + 1 < argc)
        {
            make_migration_name = argv[++index];
        }
        else if (argument == "--table" && index + 1 < argc)
        {
            table_override = argv[++index];
        }
        else if (argument == "--db:scaffold" && index + 1 < argc)
        {
            db_scaffold_target = argv[++index];
        }
        else if (argument == "--db:tables")
        {
            list_db_tables = true;
        }
        else if (argument == "--models-dir" && index + 1 < argc)
        {
            models_dir = argv[++index];
        }
        else if (argument == "--migrations-dir" && index + 1 < argc)
        {
            migrations_dir = argv[++index];
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
                "  --make:model <Name>           Generate a new Eloquent model "
                "in Lua\n"
                "  --make:migration <Name>       Generate a new database "
                "migration in Lua\n"
                "  --table <table_name>          Explicit database table name "
                "for model/migration\n"
                "  --db:scaffold <table|all>     Scaffold model(s) and "
                "migration(s) from database\n"
                "  --db:tables                   List all discovered database "
                "tables\n"
                "  --models-dir <dir>            Target directory for models "
                "(default: scripts/models)\n"
                "  --migrations-dir <dir>        Target directory for "
                "migrations (default: scripts/migrations)\n"
                "  --help                        Display this help");

            return EXIT_SUCCESS;
        }
    }

    auto configuration = api::load_config(config_path);

    bool is_cli_command = !make_model_name.empty() ||
                          !make_migration_name.empty() ||
                          !db_scaffold_target.empty() || list_db_tables;

    if (is_cli_command)
    {
        api::init_logging(configuration);

        auto &script_manager = api::s_script_mgr();

        script_manager.initialize();
        script_manager.on_config_load(configuration);

        if (list_db_tables)
        {
            auto tables = api::s_orm().get_tables();

            if (tables.empty())
            {
                std::println("No database tables discovered (driver: {}).",
                             api::s_orm().driver_name());
            }
            else
            {
                std::println("Discovered database tables ({}):", tables.size());

                for (const auto &table_name : tables)
                {
                    std::println("  - {}", table_name);
                }
            }

            return EXIT_SUCCESS;
        }

        if (!db_scaffold_target.empty())
        {
            if (db_scaffold_target == "all")
            {
                size_t scaffolded_count = api::s_orm().scaffold_all_tables(
                    models_dir, migrations_dir);

                std::println("Successfully scaffolded {} database table(s) "
                             "into '{}' and '{}'.",
                             scaffolded_count, models_dir, migrations_dir);
            }
            else
            {
                bool is_successful = api::s_orm().scaffold_table_files(
                    db_scaffold_target, models_dir, migrations_dir);

                if (is_successful)
                {
                    std::println("Successfully scaffolded table '{}' into '{}' "
                                 "and '{}'.",
                                 db_scaffold_target, models_dir,
                                 migrations_dir);
                }
                else
                {
                    std::println(stderr, "Failed to scaffold table '{}'.",
                                 db_scaffold_target);

                    return EXIT_FAILURE;
                }
            }

            return EXIT_SUCCESS;
        }

        if (!make_model_name.empty())
        {
            std::string table_name =
                !table_override.empty()
                    ? table_override
                    : api::class_to_table_name(make_model_name);
            std::string filename =
                api::table_to_model_filename(table_name) + ".lua";
            std::filesystem::path target_directory(models_dir);
            std::error_code error_code;

            if (!std::filesystem::exists(target_directory, error_code))
            {
                std::filesystem::create_directories(target_directory,
                                                    error_code);
            }

            std::filesystem::path target_path = target_directory / filename;

            auto schema = api::s_orm().describe_table(table_name);
            std::string generated_code =
                api::s_orm().generate_model_code(table_name, schema);

            std::ofstream file_stream(target_path);

            if (!file_stream.is_open())
            {
                std::println(stderr,
                             "Error: Could not open file '{}' for writing.",
                             target_path.string());

                return EXIT_FAILURE;
            }

            file_stream << generated_code;
            file_stream.close();

            std::println("Model created successfully: {}",
                         target_path.string());

            return EXIT_SUCCESS;
        }

        if (!make_migration_name.empty())
        {
            std::string table_name = table_override;

            if (table_name.empty())
            {
                std::string name_lower = make_migration_name;

                std::transform(
                    name_lower.begin(), name_lower.end(), name_lower.begin(),
                    [](unsigned char character)
                    { return static_cast<char>(std::tolower(character)); });

                if (name_lower.starts_with("create_") &&
                    name_lower.ends_with("_table") && name_lower.size() > 13)
                {
                    table_name = name_lower.substr(7, name_lower.size() - 13);
                }
                else if (name_lower.starts_with("create_") &&
                         name_lower.size() > 7)
                {
                    table_name = name_lower.substr(7);
                }
                else
                {
                    table_name = api::class_to_table_name(make_migration_name);
                }
            }

            auto current_time = std::chrono::system_clock::now();
            std::time_t time_t_value =
                std::chrono::system_clock::to_time_t(current_time);
            std::tm time_structure{};

            __localtime_(&time_t_value, &time_structure);

            std::ostringstream prefix_stream;

            prefix_stream << std::put_time(&time_structure, "%Y_%m_%d_%H%M%S");

            std::filesystem::path target_directory(migrations_dir);
            std::error_code error_code;

            if (!std::filesystem::exists(target_directory, error_code))
            {
                std::filesystem::create_directories(target_directory,
                                                    error_code);
            }

            std::string migration_filename = make_migration_name;

            if (!migration_filename.ends_with(".lua"))
            {
                migration_filename =
                    prefix_stream.str() + "_" + migration_filename + ".lua";
            }

            std::filesystem::path target_path =
                target_directory / migration_filename;

            auto schema = api::s_orm().describe_table(table_name);
            std::string generated_code =
                api::s_orm().generate_migration_code(table_name, schema);

            std::ofstream file_stream(target_path);

            if (!file_stream.is_open())
            {
                std::println(stderr,
                             "Error: Could not open file '{}' for writing.",
                             target_path.string());

                return EXIT_FAILURE;
            }

            file_stream << generated_code;
            file_stream.close();

            std::println("Migration created successfully: {}",
                         target_path.string());

            return EXIT_SUCCESS;
        }
    }

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
