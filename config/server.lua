-- Server configuration
-- Modify these values to customize the API server behavior.

config = {
    port = 8080,
    threads = 4,
    worker_threads = 4,
    log_level = "info",
    log_dir = "logs",
    dashboard_enabled = true,
    max_body_size = 10 * 1024 * 1024, -- 10 MB
    scripts_dir = "scripts",
    config_dir = "config",
    cors_origins = { "*" },

    -- Channel log filters:
    log_channels = {
        server  = "info",
        network = "info",
        modules = "info",
        scripts = "info",
        metrics = "debug",
    },

    -- Database configuration (Scaffold ORM)
    -- Pluggable template scaffold: developers can add their chosen driver in C++
    -- (e.g. MySQL, PostgreSQL, SQLite, etc.) and configure it here.
    db = {
        driver = "",
        connection = "",
    },

    -- Optional .htaccess file (secondary option to Lua scripting rules).
    -- If provided by the user, rules are loaded from this file;
    -- otherwise, rules and instructions defined directly in Lua scripting are used.
    htaccess_file = "", -- e.g. "config/.htaccess" or "" (default: use Lua script rules)
}
