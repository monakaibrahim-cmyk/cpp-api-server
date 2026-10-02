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
}
