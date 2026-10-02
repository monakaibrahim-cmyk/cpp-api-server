#pragma once

#include <atomic>
#include <cstddef>
#include <mutex>
#include <thread>

namespace api
{

struct system_snapshot
{
    double cpu_usage_percent = 0.0;
    size_t memory_rss_bytes = 0;
    size_t memory_vsize_bytes = 0;
    size_t thread_count = 0;
    size_t open_fds = 0;
    size_t net_rx_bytes_per_sec = 0;
    size_t net_tx_bytes_per_sec = 0;
};

class metrics_collector
{
public:
    metrics_collector();
    ~metrics_collector();

    void start();
    void stop();
    system_snapshot get_snapshot() const;

private:
    void collect_loop();

    void read_cpu(system_snapshot& snap);
    void read_memory(system_snapshot& snap);
    void read_thread_count(system_snapshot& snap);
    void read_open_fds(system_snapshot& snap);
    void read_network(system_snapshot& snap);

    std::thread worker_;
    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    system_snapshot current_;

    size_t prev_cpu_idle_ = 0;
    size_t prev_cpu_total_ = 0;
    size_t prev_rx_bytes_ = 0;
    size_t prev_tx_bytes_ = 0;
};

} // namespace api
