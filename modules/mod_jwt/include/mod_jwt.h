#pragma once

#include <memory>

#include "api/module.h"
#include "jwt_engine.h"

namespace api
{

struct ServerConfig;
class lua_engine;
class router;

/**
 * @brief Dynamic C++ authentication and tokenization module managing JWT
 * lifecycles.
 *
 * @details Implements a modular C++ extension (@ref module) that registers the
 * native @ref jwt_engine into @ref service_registry, loads asymmetric
 * PEM keypairs from configured certificate files, registers native RESTful
 * authentication inspection endpoints into @ref router, exposes high-level
 * tokenization bindings to the Lua runtime, and cleans up cryptographic handles
 * on shutdown.
 *
 * @par Service Registry Lookup Example
 * @code{.cpp}
 * auto jwt_service = api::s_services().get_service<api::jwt_engine>("jwt");
 * if (jwt_service && jwt_service->has_public_key())
 * {
 *     auto result = jwt_service->verify(token);
 * }
 * @endcode
 *
 * @par Lua Route Integration Example
 * @code{.lua}
 * -- Token signing in route handler:
 * route("POST", "/api/auth/token", function(req)
 *     local token = JWT.sign({ sub = "123", role = "user" }, { ttl = 3600 })
 *     return { status = 200, body = '{"token":"' .. token .. '"}' }
 * end)
 *
 * -- Token verification in route handler:
 * route("GET", "/api/v1/profile", function(req)
 *     local token = JWT.extract_bearer(req.headers["authorization"] or "")
 *     local result, error_msg = JWT.verify(token)
 *     if not result then
 *         return { status = 401, body = '{"error":"' .. (error_msg or
 * "Unauthorized") .. '"}' } end return { status = 200, body = '{"user_id":"'
 * .. result.sub .. '"}' } end)
 * @endcode
 *
 * @headerfile mod_jwt.h
 */
class mod_jwt : public module
{
  public:
    /**
     * @brief Constructs @ref mod_jwt module descriptor with metadata.
     */
    mod_jwt();

    /**
     * @brief Virtual destructor for clean module teardown.
     */
    ~mod_jwt() override = default;

    /**
     * @brief Lifecycle hook called when server configuration is loaded.
     *
     * @param configuration Mutable server configuration.
     */
    void on_config_load(ServerConfig &configuration) override;

    /**
     * @brief Lifecycle hook called during server startup to initialize
     * cryptography and load PEM keys.
     *
     * @param configuration Active server configuration.
     */
    void on_init(const ServerConfig &configuration) override;

    /**
     * @brief Lifecycle hook binding the C++ tokenization engine to the Lua
     * environment.
     *
     * @param lua_engine_instance Reference to initializing Lua engine.
     */
    void on_lua_init(lua_engine &lua_engine_instance) override;

    /**
     * @brief Lifecycle hook registering native C++ authentication endpoints
     * with the router.
     *
     * @param server_router Reference to active router instance.
     */
    void on_handlers_register(router &server_router) override;

    /**
     * @brief Lifecycle hook called during server shutdown to release
     * cryptographic keys.
     */
    void on_server_shutdown() override;

    /**
     * @brief Accesses the internal @ref jwt_engine instance managed by this
     * module.
     *
     * @return jwt_engine& Reference to token engine.
     */
    jwt_engine &get_engine();

  private:
    std::shared_ptr<jwt_engine> engine_;
    JwtConfig jwt_configuration_;
};

} // namespace api
