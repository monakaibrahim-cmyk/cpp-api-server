#pragma once

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace api
{

/**
 * @brief Categorizes the network connection direction.
 */
enum class connection_direction
{
    /** @brief Incoming connection initiated by an external HTTP client. */
    incoming,

    /** @brief Outbound HTTP/HTTPS request dispatched by the server to a remote
       peer. */
    outgoing
};

/**
 * @brief Audit record representing the lifecycle and metrics of an individual
 * HTTP connection.
 *
 * @details Retained in an in-memory ring buffer inside @ref connection_tracker
 * to provide real-time telemetry, terminal dashboard visualizations, and
 * security audits.
 */
struct ConnectionRecord
{
    /** @brief Monotonically increasing connection identifier. */
    uint64_t id = 0;

    /** @brief Formatted local timestamp string when the connection was accepted
     * (e.g. "14:23:05.123"). */
    std::string timestamp;

    /** @brief Inbound client connection or outbound client request. */
    connection_direction direction = connection_direction::incoming;

    /** @brief Remote peer IP address (IPv4 or IPv6 string). */
    std::string remote_ip;

    /** @brief Remote peer port number. */
    uint16_t remote_port = 0;

    /** @brief HTTP method string (e.g. "GET", "POST", "PUT", "DELETE"). */
    std::string method;

    /** @brief Relative request URI path without protocol/host (e.g.
     * "/api/v1/users"). */
    std::string url;

    /** @brief Full request URI including scheme, host, port, and query string.
     */
    std::string full_url;

    /** @brief Returned HTTP status code (0 while request is in flight). */
    int status_code = 0;

    /** @brief Request lifecycle execution duration in milliseconds. */
    double duration_ms = 0.0;

    /** @brief Indicates whether the request is actively being processed on a
     * worker thread. */
    bool in_flight = true;
};

/// Backward compatibility alias
using connection_record = ConnectionRecord;

/**
 * @brief Point-in-time snapshot of server connection traffic and request rates.
 */
struct ConnectionStats
{
    /** @brief Number of concurrent requests currently in-flight. */
    size_t active_connections = 0;

    /** @brief Cumulative count of all requests processed since process boot. */
    size_t total_connections = 0;

    /** @brief Current rolling request throughput rate measured in requests per
     * second. */
    double requests_per_second = 0.0;

    /** @brief Cumulative hit frequency mapping partitioned by endpoint URI
     * path. */
    std::unordered_map<std::string, size_t> endpoint_hits;
};

/// Backward compatibility alias
using connection_stats = ConnectionStats;

/**
 * @brief Thread-safe HTTP connection and latency auditing monitor.
 *
 * @details Tracks active in-flight connections and preserves a bounded circular
 * history buffer of completed requests. Computes rolling requests-per-second
 * throughput metrics using a sliding-window time-bucket algorithm.
 *
 * Concurrency Model:
 * - Active and total connection counters use lock-free atomic integers (@c
 * std::atomic).
 * - History buffers and rate calculations are protected by an internal @c
 * std::mutex.
 *
 * Example C++ usage:
 * @code{.cpp}
 * auto tracker = std::make_shared<api::connection_tracker>(1000);
 *
 * // On request arrival
 * uint64_t conn_id = tracker->on_request_start("192.168.1.50", "GET",
 * "/api/items");
 *
 * // After generating response
 * tracker->on_request_end(conn_id, 200, 14.5);
 *
 * // Read telemetry
 * api::ConnectionStats stats = tracker->get_stats();
 * std::println("Active: {}, Rate: {} rps", stats.active_connections,
 * stats.requests_per_second);
 * @endcode
 */
class connection_tracker
{
  public:
    /**
     * @brief Constructs a connection tracker with the specified ring buffer
     * capacity.
     *
     * @param[in] history_capacity Maximum number of historical connection
     * records to retain in RAM.
     */
    explicit connection_tracker(size_t history_capacity = 500);

    /**
     * @brief Records the beginning of an incoming HTTP request.
     *
     * @details Increments active and total counters, allocates a sequential ID,
     * and adds an in-flight @ref ConnectionRecord to the history buffer.
     *
     * @param[in] remote_ip Client IP address.
     * @param[in] method HTTP request method (e.g. "GET", "POST").
     * @param[in] url Relative request URI path.
     * @param[in] full_url Complete URL including host and query string.
     * @return uint64_t Unique connection identifier to pass to @ref
     * on_request_end.
     */
    uint64_t on_request_start(const std::string &remote_ip,
                              const std::string &method, const std::string &url,
                              const std::string &full_url = "");

    /**
     * @brief Marks an in-flight request as completed and records its latency
     * and status.
     *
     * @param[in] id Connection identifier previously returned by @ref
     * on_request_start.
     * @param[in] status_code HTTP status code sent to the client (e.g. 200,
     * 404).
     * @param[in] duration_milliseconds Total processing elapsed time in
     * milliseconds.
     */
    void on_request_end(uint64_t id, int status_code,
                        double duration_milliseconds);

    /**
     * @brief Directly records an already-completed HTTP request into history.
     *
     * @details Used when requests are handled or overridden early before
     * standard lifecycle hooks.
     *
     * @param[in] remote_ip Client IP address.
     * @param[in] method HTTP method string.
     * @param[in] url Request URI path.
     * @param[in] full_url Complete URL string.
     * @param[in] status_code Final HTTP status code.
     * @param[in] duration_milliseconds Total elapsed time in milliseconds.
     */
    void record_completed_request(const std::string &remote_ip,
                                  const std::string &method,
                                  const std::string &url,
                                  const std::string &full_url, int status_code,
                                  double duration_milliseconds);

    /**
     * @brief Records the start of an outbound client connection made by the
     * server.
     *
     * @param[in] remote_ip Destination server IP address.
     * @param[in] remote_port Destination TCP port.
     * @param[in] method HTTP method string.
     * @param[in] target Destination endpoint path.
     * @param[in] full_url Full target URL.
     * @return uint64_t Assigned connection identifier.
     */
    uint64_t on_outgoing_start(const std::string &remote_ip,
                               uint16_t remote_port, const std::string &method,
                               const std::string &target,
                               const std::string &full_url = "");

    /**
     * @brief Records completion of an outbound client request.
     *
     * @param[in] id Connection identifier returned by @ref on_outgoing_start.
     * @param[in] status_code HTTP status code returned by remote server.
     * @param[in] duration_milliseconds Total round-trip latency in
     * milliseconds.
     */
    void on_outgoing_end(uint64_t id, int status_code,
                         double duration_milliseconds);

    /**
     * @brief Computes and returns a point-in-time statistics snapshot.
     *
     * @return ConnectionStats Current traffic telemetry.
     */
    ConnectionStats get_stats() const;

    /**
     * @brief Retrieves the most recently completed connection records.
     *
     * @param[in] maximum_count Maximum number of records to return (defaults to
     * 20).
     * @return std::vector<ConnectionRecord> Chronologically ordered list of
     * recent records.
     */
    std::vector<ConnectionRecord>
    get_recent_connections(size_t maximum_count = 20) const;

    /**
     * @brief Retrieves all requests currently marked as in-flight.
     *
     * @return std::vector<ConnectionRecord> List of currently active connection
     * records.
     */
    std::vector<ConnectionRecord> get_active_connections() const;

  private:
    void decrement_active();

    std::atomic<uint64_t> next_id_{1};
    std::atomic<size_t> active_{0};
    std::atomic<size_t> total_{0};

    mutable std::mutex mutex_;
    size_t history_capacity_;
    std::deque<ConnectionRecord> connections_;
    std::unordered_map<std::string, size_t> endpoint_hits_;
    mutable std::deque<std::chrono::steady_clock::time_point>
        request_timestamps_;
};

} // namespace api
