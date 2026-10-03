#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <crow.h>

namespace api
{

class connection_tracker;

/**
 * @brief Crow HTTP server middleware component measuring latency, auditing
 * connections, and invoking script hooks.
 *
 * @details Implements the Crow middleware concept. In @ref before_handle, it
 * records the request start timestamp, assigns a connection ID via @ref
 * connection_tracker, and allows @ref script_mgr plugins to override or
 * intercept requests. In @ref after_handle, it records completion status,
 * computes execution latency, and emits structured network logs.
 *
 * Example C++ usage:
 * @code{.cpp}
 * auto tracker = std::make_shared<api::connection_tracker>();
 * api::RequestLogger::set_tracker(tracker);
 * @endcode
 */
struct RequestLogger
{
    /**
     * @brief Per-request contextual state maintained throughout the HTTP
     * processing lifecycle.
     */
    struct Context
    {
        /** @brief Monotonic steady clock timestamp recorded upon request
         * arrival. */
        std::chrono::steady_clock::time_point start_time;

        /** @brief Active connection tracking identifier assigned by @ref
         * connection_tracker. */
        uint64_t connection_id = 0;
    };

    /// Backward compatibility alias
    using context = Context;

    /**
     * @brief Intercepts request before handler routing.
     *
     * @param[in,out] request Incoming Crow HTTP request.
     * @param[in,out] response Outgoing Crow HTTP response.
     * @param[in,out] middleware_context Per-request context storage.
     */
    void before_handle(crow::request &request, crow::response &response,
                       Context &middleware_context);

    /**
     * @brief Post-processes request after handler completes.
     *
     * @param[in] request Handled Crow HTTP request.
     * @param[in,out] response Outgoing Crow HTTP response.
     * @param[in,out] middleware_context Per-request context storage.
     */
    void after_handle(crow::request &request, crow::response &response,
                      Context &middleware_context);

    /**
     * @brief Configures the connection tracker singleton used for connection
     * auditing.
     *
     * @param[in] tracker_instance Shared pointer to active connection_tracker.
     */
    static void
    set_tracker(std::shared_ptr<connection_tracker> tracker_instance);

  private:
    static std::shared_ptr<connection_tracker> static_tracker_;
};

/// Backward compatibility alias
using request_logger = RequestLogger;

/**
 * @brief Crow HTTP server middleware component enforcing Cross-Origin Resource
 * Sharing (CORS) rules.
 *
 * @details Automatically handles browser preflight requests by intercepting
 * HTTP OPTIONS calls and attaching appropriate Access-Control-Allow-* headers.
 * Also appends CORS headers to regular responses matching configured allowed
 * origins.
 *
 * Example C++ usage:
 * @code{.cpp}
 * api::CorsHandler::set_allowed_origins({"https://app.example.com",
 * "http://localhost:3000"});
 * @endcode
 */
struct CorsHandler
{
    /**
     * @brief Empty per-request context state satisfying Crow middleware
     * requirements.
     */
    struct Context
    {
    };

    /// Backward compatibility alias
    using context = Context;

    /**
     * @brief Intercepts OPTIONS preflight requests and returns early with 204
     * No Content.
     *
     * @param[in] request Incoming request.
     * @param[in,out] response Outgoing response.
     * @param[in,out] middleware_context Per-request context.
     */
    void before_handle(crow::request &request, crow::response &response,
                       Context &middleware_context);

    /**
     * @brief Attaches Access-Control-Allow-Origin response headers to outgoing
     * responses.
     *
     * @param[in] request Handled request.
     * @param[in,out] response Outgoing response.
     * @param[in,out] middleware_context Per-request context.
     */
    void after_handle(crow::request &request, crow::response &response,
                      Context &middleware_context);

    /**
     * @brief Sets allowed CORS origin domain strings.
     *
     * @param[in] allowed_origins List of authorized origin URLs or wildcard
     * "*".
     */
    static void set_allowed_origins(std::vector<std::string> allowed_origins);

  private:
    static std::vector<std::string> static_allowed_origins_;
};

/// Backward compatibility alias
using cors_handler = CorsHandler;

/** @brief Type definition for Crow application configured with API middleware
 * pipeline. */
using api_app_t = crow::App<RequestLogger, CorsHandler>;

} // namespace api
