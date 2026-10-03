#pragma once

#include <atomic>
#include <thread>

#include "api/config.h"
#include "api/middleware.h"

namespace api
{

/**
 * @brief High-performance asynchronous HTTP server managing the Crow
 * application lifecycle.
 *
 * Encapsulates the underlying Crow HTTP engine configured with middleware
 * layers
 * (@ref CorsHandler and @ref RequestLogger).
 * Manages thread allocation, network port binding, background event loop
 * execution, and graceful shutdown coordination.
 *
 * @par Server Lifecycle Example
 * @code{.cpp}
 * api::ServerConfig configuration;
 * configuration.port = 8080;
 * configuration.threads = 8;
 *
 * api::api_server server(configuration);
 *
 * // Configure route endpoints before starting the server
 * CROW_ROUTE(server.app(), "/api/v1/health")
 * ([](const crow::request& request)
 * {
 *     return crow::response(200, "OK");
 * });
 *
 * // Start asynchronous listening loop on worker thread
 * server.start();
 *
 * // ... Serve incoming traffic ...
 *
 * // Graceful shutdown terminates event loop and joins worker thread
 * server.stop();
 * @endcode
 *
 * @thread_safety Route handler registration on @ref app() must be performed
 * prior to calling @ref start(). Starting and stopping the server is safe to
 * invoke across threads and protected by atomic state flags.
 *
 * @headerfile api/server.h
 */
class api_server
{
  public:
    /**
     * @brief Constructs an @ref api_server instance with the specified
     * configuration.
     *
     * @param configuration Active server configuration containing listening
     * port and concurrency settings.
     */
    explicit api_server(const ServerConfig &configuration);

    /**
     * @brief Destructor ensuring the server thread is stopped and joined
     * gracefully.
     *
     * If the background worker thread is still running upon destruction, @ref
     * stop() is invoked automatically to prevent dangling threads or process
     * termination.
     */
    ~api_server();

    /**
     * @brief Deleted copy constructor to prevent multiple servers sharing
     * worker threads.
     */
    api_server(const api_server &) = delete;

    /**
     * @brief Deleted copy assignment operator.
     */
    api_server &operator=(const api_server &) = delete;

    /**
     * @brief Starts the HTTP server on a dedicated background worker thread.
     *
     * Binds the configured listening port and launches Crow's internal Asio
     * event loop. If the server is already active, this method returns
     * immediately without side effects.
     */
    void start();

    /**
     * @brief Stops the HTTP server and joins the background event loop thread
     * gracefully.
     *
     * Signals Crow to stop accepting new requests, drains in-flight requests,
     * and joins the underlying background execution thread. If the server is
     * not running, this method is a safe no-op.
     */
    void stop();

    /**
     * @brief Checks whether the HTTP server is currently running and accepting
     * traffic.
     *
     * @return @c true if the background server thread is actively running; @c
     * false otherwise.
     */
    bool is_running() const;

    /**
     * @brief Returns a mutable reference to the underlying Crow application
     * instance.
     *
     * Allows router setup, route endpoint registration, and middleware
     * configuration.
     *
     * @return Mutable reference to the configured @ref api_app_t instance.
     */
    api_app_t &app();

  private:
    ServerConfig configuration_;
    api_app_t app_;
    std::thread server_thread_;
    std::atomic<bool> running_{false};
};

} // namespace api
