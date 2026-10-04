#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <thread>

namespace api
{

/**
 * @brief Point-in-time snapshot of process-level hardware and resource
 * consumption.
 *
 * @details Captured periodically by @ref metrics_collector to fuel real-time
 * terminal graphs, JSON health telemetry endpoints, and diagnostic logs.
 */
struct SystemSnapshot
{
    /** @brief Process CPU utilization percentage across online processor cores
     * (0.0% to 100.0%). */
    double cpu_usage_percent = 0.0;

    /** @brief Resident Set Size (RSS) physical RAM consumption in bytes. */
    size_t memory_rss_bytes = 0;

    /** @brief Virtual memory address space allocation (VSIZE) in bytes. */
    size_t memory_vsize_bytes = 0;

    /** @brief Total operating system threads spawned by the current process. */
    size_t thread_count = 0;

    /** @brief Total number of allocated and open file descriptors. */
    size_t open_fds = 0;

    /** @brief Process inbound network receive bandwidth throughput in bytes
     * per second. */
    size_t net_rx_bytes_per_sec = 0;

    /** @brief Process outbound network transmit bandwidth throughput in bytes
     * per second. */
    size_t net_tx_bytes_per_sec = 0;

    /** @brief Cumulative inbound network bytes received by the process. */
    size_t net_rx_total_bytes = 0;

    /** @brief Cumulative outbound network bytes transmitted by the process. */
    size_t net_tx_total_bytes = 0;
};

/// Backward compatibility alias
using system_snapshot = SystemSnapshot;

/**
 * @brief Background telemetry collector monitoring process-level CPU, memory,
 * descriptor, and network I/O.
 *
 * @details Operates an independent asynchronous background thread that samples
 * process-isolated resource consumption at 1-second intervals.
 *
 * Example C++ usage:
 * @code{.cpp}
 * api::metrics_collector metrics;
 * metrics.start();
 *
 * // Query latest snapshot
 * api::SystemSnapshot snapshot = metrics.get_snapshot();
 * std::println("CPU: {:.1f}%, RSS: {} MB", snapshot.cpu_usage_percent,
 * snapshot.memory_rss_bytes / (1024 * 1024));
 *
 * metrics.stop();
 * @endcode
 */
class metrics_collector
{
  public:
    /**
     * @brief Constructs a metrics collector with idle background thread.
     */
    metrics_collector();

    /**
     * @brief Destructor ensuring background sampling thread is stopped and
     * joined.
     */
    ~metrics_collector();

    /**
     * @brief Launches the asynchronous background sampling thread.
     */
    void start();

    /**
     * @brief Signals the background sampling thread to terminate and joins it.
     */
    void stop();

    /**
     * @brief Retrieves a thread-safe copy of the latest sampled resource
     * snapshot.
     *
     * @return SystemSnapshot Most recent resource utilization metrics.
     */
    SystemSnapshot get_snapshot() const;

    /**
     * @brief Records process-level incoming network bytes.
     *
     * @param[in] bytes Inbound network bytes received.
     */
    static void record_received_bytes(size_t bytes);

    /**
     * @brief Records process-level outgoing network bytes.
     *
     * @param[in] bytes Outbound network bytes transmitted.
     */
    static void record_transmitted_bytes(size_t bytes);

    /**
     * @brief Records process-level bidirectional network bytes.
     *
     * @param[in] received_bytes Inbound network bytes received.
     * @param[in] transmitted_bytes Outbound network bytes transmitted.
     */
    static void record_network_bytes(size_t received_bytes,
                                     size_t transmitted_bytes);

    /** @brief Convenience alias for @ref record_received_bytes. */
    static void record_rx_bytes(size_t bytes);

    /** @brief Convenience alias for @ref record_transmitted_bytes. */
    static void record_tx_bytes(size_t bytes);

  private:
    void collect_loop();

    void read_cpu(SystemSnapshot &snapshot);
    void read_memory(SystemSnapshot &snapshot);
    void read_thread_count(SystemSnapshot &snapshot);
    void read_open_fds(SystemSnapshot &snapshot);
    void read_network(SystemSnapshot &snapshot);

    std::thread worker_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    SystemSnapshot current_;

    uint64_t previous_cpu_time_nanoseconds_ = 0;
    std::chrono::steady_clock::time_point previous_sample_time_;
    size_t previous_received_bytes_ = 0;
    size_t previous_transmitted_bytes_ = 0;
    bool has_sampled_network_ = false;
};

} // namespace api
