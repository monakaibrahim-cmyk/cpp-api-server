#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <thread>

namespace api
{

/**
 * @brief Point-in-time snapshot of system hardware and host OS resource
 * consumption.
 *
 * @details Captured periodically by @ref metrics_collector to fuel real-time
 * terminal graphs, JSON health telemetry endpoints, and diagnostic logs.
 */
struct SystemSnapshot
{
    /** @brief Aggregate CPU utilization across all online processor cores (0.0%
     * to 100.0%). */
    double cpu_usage_percent = 0.0;

    /** @brief Resident Set Size (RSS) physical RAM consumption in bytes. */
    size_t memory_rss_bytes = 0;

    /** @brief Virtual memory address space allocation (VSIZE) in bytes. */
    size_t memory_vsize_bytes = 0;

    /** @brief Total operating system threads spawned by the current process. */
    size_t thread_count = 0;

    /** @brief Total number of allocated and open file descriptors. */
    size_t open_fds = 0;

    /** @brief Inbound network receive bandwidth throughput in bytes per second.
     */
    size_t net_rx_bytes_per_sec = 0;

    /** @brief Outbound network transmit bandwidth throughput in bytes per
     * second. */
    size_t net_tx_bytes_per_sec = 0;
};

/// Backward compatibility alias
using system_snapshot = SystemSnapshot;

/**
 * @brief Background telemetry collector monitoring CPU, memory, descriptor, and
 * network I/O.
 *
 * @details Operates an independent asynchronous background thread that samples
 * the Linux
 * @c /proc virtual filesystem (including @c /proc/stat, @c /proc/self/status,
 * @c /proc/self/fd, and @c /proc/net/dev) at 1-second intervals.
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

    size_t prev_cpu_idle_ = 0;
    size_t prev_cpu_total_ = 0;
    size_t prev_rx_bytes_ = 0;
    size_t prev_tx_bytes_ = 0;
};

} // namespace api
