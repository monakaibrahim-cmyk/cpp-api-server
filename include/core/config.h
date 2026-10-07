#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace api
{

/**
 * @brief Complete runtime server configuration state.
 *
 * @details Encapsulates all networking, concurrency, logging, caching,
 * security (CORS, Apache .htaccess emulation), and database connectivity
 * options. Typically populated once at application bootstrap by @ref
 * load_config from a Lua configuration file (such as @c config/server.lua) and
 * shared across subsystems.
 *
 * @note Thread Safety: Once initialized during server startup, instances of
 * this structure are treated as immutable and can be safely read concurrently
 * by multiple threads.
 *
 * Example Lua configuration file (@c config/server.lua):
 * @code{.lua}
 * config = {
 *     port = 8080,
 *     threads = 8,
 *     worker_threads = 4,
 *     log_level = "info",
 *     log_dir = "logs",
 *     dashboard_enabled = true,
 *     max_body_size = 10 * 1024 * 1024,
 *     scripts_dir = "scripts",
 *     config_dir = "config",
 *     cache_enabled = true,
 *     cache_max_items = 2000,
 *     cors_origins = { "https://example.com", "http://localhost:3000" },
 *     db_driver = "mysql",
 *     db_connection =
 * "host=127.0.0.1;port=3306;dbname=production;user=app;password=secret;",
 *     htaccess_file = "config/.htaccess"
 * }
 * @endcode
 *
 * Example C++ usage:
 * @code{.cpp}
 * api::ServerConfig configuration = api::load_config("config/server.lua");
 * if (configuration.port == 0)
 * {
 *     configuration.port = 8080;
 * }
 * api::init_logging(configuration);
 * @endcode
 */
struct ServerConfig
{
    /**
     * @brief TCP listening port for incoming HTTP connections.
     * @details Default: 8080. Valid range: 1 to 65535.
     */
    uint16_t port = 8080;

    /**
     * @brief Concurrency level for Crow HTTP request worker threads.
     * @details Default: 4. Determines the number of event loop threads
     * accepting and processing network I/O.
     */
    uint16_t threads = 4;

    /**
     * @brief Number of persistent background worker threads in the thread pool.
     * @details Default: 4. Used for asynchronous database operations,
     * long-running compute tasks, and deferred jobs submitted via @ref
     * thread_pool.
     */
    uint16_t worker_threads = 4;

    /**
     * @brief Global minimum log severity filter.
     * @details Default: "info". Supported values: "trace", "debug", "info",
     * "warn" ("warning"), "error", "fatal".
     */
    std::string log_level = "info";

    /**
     * @brief Filesystem directory where rotating log files are saved.
     * @details Default: "logs". Created automatically at startup if absent.
     */
    std::string log_dir = "logs";

    /**
     * @brief Enables the interactive FTXUI terminal user interface dashboard.
     * @details Default: true. Set to false when running in background daemon /
     * headless mode.
     */
    bool dashboard_enabled = true;

    /**
     * @brief Maximum permitted HTTP request body size in bytes.
     * @details Default: 10485760 (10 MB). Requests exceeding this limit receive
     * HTTP 413.
     */
    size_t max_body_size = 10 * 1024 * 1024;

    /**
     * @brief Root directory for Lua route scripts, models, and migrations.
     * @details Default: "scripts". Added to Lua package.path during engine
     * initialization.
     */
    std::string scripts_dir = "scripts";

    /**
     * @brief Directory path where auxiliary configuration files are located.
     * @details Default: "config".
     */
    std::string config_dir = "config";

    /**
     * @brief Master toggle for HTTP response caching.
     * @details Default: true. When disabled, cache queries always MISS and
     * writes are skipped.
     */
    bool cache_enabled = true;

    /**
     * @brief Maximum capacity of the in-memory response cache before FIFO/LRU
     * eviction.
     * @details Default: 1000 items.
     */
    size_t cache_max_items = 1000;

    /**
     * @brief Whitelist of allowed Cross-Origin Resource Sharing (CORS) origins.
     * @details Default: {"*"}. Preflight OPTIONS responses and CORS response
     * headers validate incoming Origin against this list.
     */
    std::vector<std::string> cors_origins = {"*"};

    /**
     * @brief Per-channel log severity overrides.
     * @details Maps channel names (e.g. "network", "orm", "routes") to specific
     * severity thresholds.
     */
    std::unordered_map<std::string, std::string> log_channels;

    /**
     * @brief Master toggle for Ollama AI integration.
     * @details Default: true.
     */
    bool ollama_enabled = true;

    /**
     * @brief Host address for the local Ollama daemon.
     * @details Default: "127.0.0.1".
     */
    std::string ollama_host = "127.0.0.1";

    /**
     * @brief Port number for the local Ollama daemon.
     * @details Default: 11434.
     */
    uint16_t ollama_port = 11434;

    /**
     * @brief Default model / agent to use when none is explicitly specified.
     * @details Default: "" (auto-selects first discovered agent from Ollama).
     */
    std::string ollama_default_model = "";

    /**
     * @brief Default system prompt applied to chat sessions.
     * @details Default: "You are a helpful AI assistant."
     */
    std::string ollama_system_prompt = "You are a helpful AI assistant.";

    /**
     * @brief Session and chat history time-to-live in seconds.
     * @details Default: 3600 (1 hour). Stored in LRU cache by user IP.
     */
    int ollama_session_ttl = 3600;

    /**
     * @brief Request timeout in seconds for Ollama HTTP calls.
     * @details Default: 120 seconds.
     */
    int ollama_timeout_seconds = 120;

    /**
     * @brief Auto-discovery flag for finding local agents via Ollama tags.
     * @details Default: true.
     */
    bool ollama_auto_discover = true;

    /**
     * @brief Maximum number of conversation turns retained in cache sliding window.
     * @details Default: 20 turns.
     */
    size_t ollama_max_history_turns = 20;

    /**
     * @brief Path to an Apache .htaccess rules file.
     * @details Default: "". If empty or not found on disk, the system evaluates
     * rewrite and access rules defined dynamically in Lua via @ref
     * htaccess_engine.
     */
    std::string htaccess_file = "";
};

/// Backward compatibility alias
using server_config = ServerConfig;

/**
 * @brief Loads and parses server configuration settings from a Lua script file.
 *
 * @details Reads the specified Lua script file in an isolated Sol2 state,
 * searches for a top-level global table named @c config, and extracts all
 * recognized configuration fields into a new @ref ServerConfig instance.
 * Unspecified fields retain their compiled default values.
 *
 * If the file cannot be accessed or contains syntax errors, an error is logged
 * to stderr, and a default-constructed @ref ServerConfig is returned without
 * terminating the process.
 *
 * @param[in] lua_configuration_path Filesystem path to the target Lua
 * configuration script.
 * @return ServerConfig Successfully populated configuration object, or default
 * settings on error.
 *
 * Example:
 * @code{.cpp}
 * api::ServerConfig server_config = api::load_config("config/server.lua");
 * std::println("Listening on port: {}", server_config.port);
 * @endcode
 */
ServerConfig load_config(const std::string &lua_configuration_path);

} // namespace api
