#include "api/metrics.hpp"

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

metrics_collector::~metrics_collector()
{
    stop();
}

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

system_snapshot metrics_collector::get_snapshot() const
{
    std::lock_guard<std::mutex> lk(mutex_);

    return current_;
}

void metrics_collector::collect_loop()
{
    while (running_.load())
    {
        system_snapshot snap;

        read_cpu(snap);
        read_memory(snap);
        read_thread_count(snap);
        read_open_fds(snap);
        read_network(snap);

        {
            std::lock_guard<std::mutex> lk(mutex_);

            current_ = snap;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void metrics_collector::read_cpu(system_snapshot& snap)
{
    std::ifstream f("/proc/stat");

    if (!f.is_open())
    {
        return;
    }

    std::string line;

    if (!std::getline(f, line))
    {
        return;
    }

    std::istringstream iss(line);
    std::string label;

    iss >> label;

    size_t total = 0;
    size_t idle = 0;
    size_t val = 0;

    for (int i = 0; iss >> val; ++i)
    {
        total += val;

        if (i == 3)
        {
            idle = val;
        }
        else if (i == 4)
        {
            idle += val;
        }
    }

    if (prev_cpu_total_ > 0)
    {
        auto total_delta = total - prev_cpu_total_;
        auto idle_delta = idle - prev_cpu_idle_;

        if (total_delta > 0)
        {
            snap.cpu_usage_percent =
                100.0 * (1.0 - static_cast<double>(idle_delta) / static_cast<double>(total_delta));
        }
    }

    prev_cpu_total_ = total;
    prev_cpu_idle_ = idle;
}

void metrics_collector::read_memory(system_snapshot& snap)
{
    std::ifstream f("/proc/self/status");

    if (!f.is_open())
    {
        return;
    }

    std::string line;

    while (std::getline(f, line))
    {
        if (line.starts_with("VmRSS:"))
        {
            std::istringstream iss(line.substr(6));
            size_t kb = 0;

            iss >> kb;
            snap.memory_rss_bytes = kb * 1024;
        }
        else if (line.starts_with("VmSize:"))
        {
            std::istringstream iss(line.substr(7));
            size_t kb = 0;

            iss >> kb;
            snap.memory_vsize_bytes = kb * 1024;
        }
    }
}

void metrics_collector::read_thread_count(system_snapshot& snap)
{
    std::ifstream f("/proc/self/status");

    if (!f.is_open())
    {
        return;
    }

    std::string line;

    while (std::getline(f, line))
    {
        if (line.starts_with("Threads:"))
        {
            std::istringstream iss(line.substr(8));
            size_t n = 0;

            iss >> n;
            snap.thread_count = n;

            return;
        }
    }
}

void metrics_collector::read_open_fds(system_snapshot& snap)
{
    namespace fs = std::filesystem;

    try
    {
        size_t count = 0;

        for (auto& _ : fs::directory_iterator("/proc/self/fd"))
        {
            (void)_;
            ++count;
        }

        snap.open_fds = count;
    }
    catch (...)
    {
    }
}

void metrics_collector::read_network(system_snapshot& snap)
{
    std::ifstream f("/proc/net/dev");

    if (!f.is_open())
    {
        return;
    }

    std::string line;

    std::getline(f, line);
    std::getline(f, line);

    size_t total_rx = 0;
    size_t total_tx = 0;

    while (std::getline(f, line))
    {
        std::istringstream iss(line);
        std::string iface;

        iss >> iface;

        if (!iface.empty() && iface.back() == ':')
        {
            iface.pop_back();
        }

        if (iface == "lo")
        {
            continue;
        }

        size_t rx_bytes = 0;

        iss >> rx_bytes;

        size_t skip = 0;

        for (int i = 0; i < 7; ++i)
        {
            iss >> skip;
        }

        size_t tx_bytes = 0;

        iss >> tx_bytes;

        total_rx += rx_bytes;
        total_tx += tx_bytes;
    }

    if (prev_rx_bytes_ > 0 || prev_tx_bytes_ > 0)
    {
        snap.net_rx_bytes_per_sec =
            (total_rx >= prev_rx_bytes_) ? (total_rx - prev_rx_bytes_) : 0;
        snap.net_tx_bytes_per_sec =
            (total_tx >= prev_tx_bytes_) ? (total_tx - prev_tx_bytes_) : 0;
    }

    prev_rx_bytes_ = total_rx;
    prev_tx_bytes_ = total_tx;
}

} // namespace api
