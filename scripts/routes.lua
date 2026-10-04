-- Route mappings
-- URL routing is controlled here in Lua, decoupled from C++ modules.
-- Syntax:
--   route("METHOD", "/path", "native_handler_name", [ { cache = ttl_seconds } ])
--   route("METHOD", "/path", function(req) return { status = 200, body = "...", content_type = "..." } end, [ { cache = ttl_seconds } ])

-- ═══════════════════════════════════════════════════════════════════════════
-- Security & Routing Policy Hierarchy
-- .htaccess is a secondary option to Lua scripting set rules:
--   1. If the user provides an .htaccess file (e.g. config/.htaccess or configured
--      in server.lua via htaccess_file), the file directives are loaded.
--   2. If NOT provided, rules and instructions defined directly in Lua scripting
--      are used as the default.
-- ═══════════════════════════════════════════════════════════════════════════

local htaccess_file = (api and api.config and api.config.htaccess_file and api.config.htaccess_file ~= "")
    and api.config.htaccess_file
    or "config/.htaccess"

if htaccess and htaccess.file_exists and htaccess.file_exists(htaccess_file) then
    htaccess.load_file(htaccess_file)
    htaccess.set_source("htaccess_file")
    log_info("htaccess", "Loaded user-provided .htaccess file: " .. htaccess_file)
else
    -- Default: Use rules and instructions defined directly in Lua scripting
    if htaccess then
        htaccess.clear()
        htaccess.set_source("lua_script")

        -- Security response headers defined in Lua
        htaccess.header("X-Content-Type-Options", "nosniff")
        htaccess.header("X-Frame-Options", "DENY")
        htaccess.header("X-XSS-Protection", "1; mode=block")

        -- IP access control defined in Lua
        htaccess.set_order("allow,deny")
        htaccess.allow("all")

        -- Custom error documents defined in Lua
        htaccess.set_error_document(403, '{"error":"Forbidden by Lua security policy","status":403}')
        htaccess.set_error_document(404, '{"error":"Resource not found","status":404}')
        htaccess.set_error_document(410, '{"error":"Resource is permanently gone","status":410}')

        -- Activate rule evaluation
        htaccess.set_enabled(true)

        log_info("htaccess", "No .htaccess file provided; using rules and instructions from Lua scripting")
    end
end

-- Core endpoints mapped to native handlers
route("GET", "/favicon.ico", function(req)
    return {
        status = 204,
        body = req.body,
        content_type = "application/json"
    }
end)

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

-- Database Scaffold ORM introspection endpoint
route("GET", "/api/db/status", function(req)
    local is_conn = false
    local drv_name = "none"
    local driver_list = {}

    if DB then
        if DB.is_connected then
            is_conn = DB.is_connected()
        end
        if DB.driver_name then
            drv_name = DB.driver_name()
        end
        if DB.drivers then
            driver_list = DB.drivers()
        end
    end

    local drivers_json = {}
    for _, d in ipairs(driver_list) do
        table.insert(drivers_json, string.format("%q", d))
    end

    local body = string.format(
        '{"status":"ok","connected":%s,"active_driver":%q,"registered_drivers":[%s]}',
        is_conn and "true" or "false",
        drv_name,
        table.concat(drivers_json, ",")
    )

    return {
        status = 200,
        body = body,
        content_type = "application/json"
    }
end)

-- Eloquent ORM & Artisan introspection endpoint
route("GET", "/api/db/artisan", function(req)
    local tables = Artisan and Artisan.tables() or {}
    local tables_json = {}

    for _, t in ipairs(tables) do
        table.insert(tables_json, string.format("%q", t))
    end

    local body = string.format(
        '{"tables":[%s],"artisan_available":%s}',
        table.concat(tables_json, ","),
        Artisan ~= nil and "true" or "false"
    )

    return {
        status = 200,
        body = body,
        content_type = "application/json"
    }
end)

-- ═══════════════════════════════════════════════════════════════════════════
-- C++ Native Authentication Endpoints (mod_jwt)
-- ═══════════════════════════════════════════════════════════════════════════
route("GET", "/api/auth/status", "auth.status")
route("GET", "/api/auth/public_key", "auth.public_key")
route("GET", "/api/auth/verify_bearer", "auth.verify_bearer")

-- Token Issuance Endpoint (C++ Tokenization Engine)
route("POST", "/api/auth/token", function(req)
    if not JWT then
        return {
            status = 503,
            body = '{"error":"JWT module unavailable"}',
            content_type = "application/json"
        }
    end

    local sub_id = (req.json and req.json.sub) or "user_1001"
    local user_role = (req.json and req.json.role) or "member"
    local user_email = (req.json and req.json.email) or "user@example.com"

    local token = JWT.sign({
        sub = sub_id,
        role = user_role,
        email = user_email
    }, { ttl = 3600 })

    return {
        status = 200,
        body = json.encode({
            access_token = token,
            token_type = "Bearer",
            expires_in = 3600,
            algorithm = JWT.algorithm()
        }),
        content_type = "application/json"
    }
end)

-- ═══════════════════════════════════════════════════════════════════════════
-- RESTful Resource Endpoints with Path Parameters (:id) & Query Strings
-- ═══════════════════════════════════════════════════════════════════════════
route("GET", "/api/v1/users/:id", function(req)
    local user_id = req.params and req.params.id or "unknown"
    local view_mode = req.query and req.query.view or "summary"

    return {
        status = 200,
        body = json.encode({
            user_id = user_id,
            view = view_mode,
            url = req.url,
            method = req.method
        }),
        content_type = "application/json"
    }
end)

-- Protected REST Endpoint requiring Bearer Token
route("GET", "/api/v1/profile", function(req)
    if not JWT then
        return { status = 503, body = '{"error":"JWT module not loaded"}' }
    end

    local token = JWT.extract_bearer(req)
    if not token or token == "" then
        return {
            status = 401,
            body = '{"error":"Authorization header missing or invalid Bearer format","status":401}',
            content_type = "application/json"
        }
    end

    local claims, error_message = JWT.verify(token)
    if not claims then
        return {
            status = 401,
            body = json.encode({ error = error_message or "Unauthorized", status = 401 }),
            content_type = "application/json"
        }
    end

    return {
        status = 200,
        body = json.encode({
            authenticated = true,
            user_id = claims.sub,
            role = claims.role,
            email = claims.email
        }),
        content_type = "application/json"
    }
end)

