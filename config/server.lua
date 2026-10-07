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

    -- Ollama Local AI Model Configuration
    ollama = {
        enabled = true,
        host = "127.0.0.1",
        port = 11434,
        default_model = "",          -- If empty, auto-selected from discovered agents
        system_prompt = "You are a helpful AI assistant.",
        session_ttl = 3600,           -- Session and chat history TTL in cache (seconds)
        timeout_seconds = 120,        -- Timeout for Ollama LLM response generation
        auto_discover_agents = true,  -- Auto-query Ollama to discover local installed agents
    },

    -- Optional .htaccess file (secondary option to Lua scripting rules).
    -- If provided by the user, rules are loaded from this file;
    -- otherwise, rules and instructions defined directly in Lua scripting are used.
    htaccess_file = "", -- e.g. "config/.htaccess" or "" (default: use Lua script rules)
}
