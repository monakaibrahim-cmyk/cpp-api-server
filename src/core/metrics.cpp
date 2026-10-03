#include "api/metrics.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

namespace api
{

metrics_collector::metrics_collector() = default;

metrics_collector::~metrics_collector() { stop(); }

void metrics_collector::start()
{
    if (running_.load())
    {
        return;
    }

    running_ = true;
    worker_ = std::thread(&metrics_collector::collect_loop, this);
}

void metrics_collector::stop()
{
    running_ = false;

    if (worker_.joinable())
    {
        worker_.join();
    }
}

SystemSnapshot metrics_collector::get_snapshot() const
{
    std::lock_guard<std::mutex> lock(mutex_);

    return current_;
}

void metrics_collector::collect_loop()
{
    while (running_.load())
    {
        SystemSnapshot snapshot;

        read_cpu(snapshot);
        read_memory(snapshot);
        read_thread_count(snapshot);
        read_open_fds(snapshot);
        read_network(snapshot);

        {
            std::lock_guard<std::mutex> lock(mutex_);

            current_ = snapshot;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void metrics_collector::read_cpu(SystemSnapshot &snapshot)
{
    std::ifstream proc_stat_file("/proc/stat");

    if (!proc_stat_file.is_open())
    {
        return;
    }

    std::string line;

    if (!std::getline(proc_stat_file, line))
    {
        return;
    }

    std::istringstream line_stream(line);
    std::string label;

    line_stream >> label;

    size_t total = 0;
    size_t idle = 0;
    size_t field_value = 0;

    for (int index = 0; line_stream >> field_value; ++index)
    {
        total += field_value;

        if (index == 3)
        {
            idle = field_value;
        }
        else if (index == 4)
        {
            idle += field_value;
        }
    }

    if (prev_cpu_total_ > 0)
    {
        auto total_delta = total - prev_cpu_total_;
        auto idle_delta = idle - prev_cpu_idle_;

        if (total_delta > 0)
        {
            snapshot.cpu_usage_percent =
                100.0 * (1.0 - static_cast<double>(idle_delta) /
                                   static_cast<double>(total_delta));
        }
    }

    prev_cpu_total_ = total;
    prev_cpu_idle_ = idle;
}

void metrics_collector::read_memory(SystemSnapshot &snapshot)
{
    std::ifstream proc_status_file("/proc/self/status");

    if (!proc_status_file.is_open())
    {
        return;
    }

    std::string line;

    while (std::getline(proc_status_file, line))
    {
        if (line.starts_with("VmRSS:"))
        {
            std::istringstream line_stream(line.substr(6));
            size_t kilobytes = 0;

            line_stream >> kilobytes;
            snapshot.memory_rss_bytes = kilobytes * 1024;
        }
        else if (line.starts_with("VmSize:"))
        {
            std::istringstream line_stream(line.substr(7));
            size_t kilobytes = 0;

            line_stream >> kilobytes;
            snapshot.memory_vsize_bytes = kilobytes * 1024;
        }
    }
}

void metrics_collector::read_thread_count(SystemSnapshot &snapshot)
{
    std::ifstream proc_status_file("/proc/self/status");

    if (!proc_status_file.is_open())
    {
        return;
    }

    std::string line;

    while (std::getline(proc_status_file, line))
    {
        if (line.starts_with("Threads:"))
        {
            std::istringstream line_stream(line.substr(8));
            size_t thread_count = 0;

            line_stream >> thread_count;
            snapshot.thread_count = thread_count;

            return;
        }
    }
}

void metrics_collector::read_open_fds(SystemSnapshot &snapshot)
{
    namespace fs = std::filesystem;

    try
    {
        size_t count = 0;

        for (auto &entry : fs::directory_iterator("/proc/self/fd"))
        {
            (void)entry;
            ++count;
        }

        snapshot.open_fds = count;
    }
    catch (...)
    {
    }
}

void metrics_collector::read_network(SystemSnapshot &snapshot)
{
    std::ifstream proc_net_file("/proc/net/dev");

    if (!proc_net_file.is_open())
    {
        return;
    }

    std::string line;

    std::getline(proc_net_file, line);
    std::getline(proc_net_file, line);

    size_t total_rx = 0;
    size_t total_tx = 0;

    while (std::getline(proc_net_file, line))
    {
        std::istringstream line_stream(line);
        std::string interface_name;

        line_stream >> interface_name;

        if (!interface_name.empty() && interface_name.back() == ':')
        {
            interface_name.pop_back();
        }

        if (interface_name == "lo")
        {
            continue;
        }

        size_t rx_bytes = 0;

        line_stream >> rx_bytes;

        size_t skip_value = 0;

        for (int i = 0; i < 7; ++i)
        {
            line_stream >> skip_value;
        }

        size_t tx_bytes = 0;

        line_stream >> tx_bytes;

        total_rx += rx_bytes;
        total_tx += tx_bytes;
    }

    if (prev_rx_bytes_ > 0 || prev_tx_bytes_ > 0)
    {
        snapshot.net_rx_bytes_per_sec =
            (total_rx >= prev_rx_bytes_) ? (total_rx - prev_rx_bytes_) : 0;
        snapshot.net_tx_bytes_per_sec =
            (total_tx >= prev_tx_bytes_) ? (total_tx - prev_tx_bytes_) : 0;
    }

    prev_rx_bytes_ = total_rx;
    prev_tx_bytes_ = total_tx;
}

} // namespace api
