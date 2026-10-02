-- Route mappings
-- URL routing is controlled here in Lua, decoupled from C++ modules.
-- Syntax:
--   route("METHOD", "/path", "native_handler_name", [ { cache = ttl_seconds } ])
--   route("METHOD", "/path", function(req) return { status = 200, body = "...", content_type = "..." } end, [ { cache = ttl_seconds } ])

-- Core endpoints mapped to native handlers
route("GET", "/api/health", "core.health")
route("GET", "/api/info", "core.info", { cache = 60 })
route("GET", "/api/stats", "core.stats")
route("GET", "/api/metrics", "core.metrics")

-- Request body echo
route("POST", "/api/echo", function(req)
    return {
        status = 200,
        body = req.body,
        content_type = "application/json"
    }
end)
