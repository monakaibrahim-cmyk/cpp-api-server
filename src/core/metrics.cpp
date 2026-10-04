#include <core/metrics.h>

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace api
{

static std::atomic<size_t> global_process_received_bytes{0};
static std::atomic<size_t> global_process_transmitted_bytes{0};

void metrics_collector::record_received_bytes(size_t bytes)
{
    global_process_received_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void metrics_collector::record_transmitted_bytes(size_t bytes)
{
    global_process_transmitted_bytes.fetch_add(bytes,
                                               std::memory_order_relaxed);
}

void metrics_collector::record_network_bytes(size_t received_bytes,
                                             size_t transmitted_bytes)
{
    if (received_bytes > 0)
    {
        global_process_received_bytes.fetch_add(received_bytes,
                                                std::memory_order_relaxed);
    }

    if (transmitted_bytes > 0)
    {
        global_process_transmitted_bytes.fetch_add(transmitted_bytes,
                                                   std::memory_order_relaxed);
    }
}

void metrics_collector::record_rx_bytes(size_t bytes)
{
    record_received_bytes(bytes);
}

void metrics_collector::record_tx_bytes(size_t bytes)
{
    record_transmitted_bytes(bytes);
}

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
    uint64_t current_cpu_time_nanoseconds = 0;
    bool time_retrieved = false;

#if defined(CLOCK_PROCESS_CPUTIME_ID)
    struct timespec process_time_specification;

    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &process_time_specification) ==
        0)
    {
        current_cpu_time_nanoseconds =
            static_cast<uint64_t>(process_time_specification.tv_sec) *
                1000000000ULL +
            static_cast<uint64_t>(process_time_specification.tv_nsec);
        time_retrieved = true;
    }
#endif

    if (!time_retrieved)
    {
#if defined(_WIN32) || defined(_WIN64)
        FILETIME creation_time, exit_time, kernel_time, user_time;

        if (GetProcessTimes(GetCurrentProcess(), &creation_time, &exit_time,
            &kernel_time, &user_time))
        {
            uint64_t u_time = ((uint64_t)user_time.dwHighDateTime << 32) |
                              user_time.dwLowDateTime;
            uint64_t k_time = ((uint64_t)kernel_time.dwHighDateTime << 32) |
                              kernel_time.dwLowDateTime;
            current_cpu_time_nanoseconds = (u_time + k_time) * 100ULL;
            time_retrieved = true;
        }
#else
        std::ifstream proc_stat_file("/proc/self/stat");

        if (proc_stat_file.is_open())
        {
            std::string file_content;

            if (std::getline(proc_stat_file, file_content))
            {
                auto closing_parenthesis_position = file_content.rfind(')');

                if (closing_parenthesis_position != std::string::npos &&
                    closing_parenthesis_position + 2 < file_content.size())
                {
                    std::istringstream stream(
                        file_content.substr(closing_parenthesis_position + 2));
                    std::string dummy_field;

                    for (int field_index = 0; field_index < 11; ++field_index)
                    {
                        stream >> dummy_field;
                    }

                    size_t user_time_ticks = 0;
                    size_t system_time_ticks = 0;

                    if (stream >> user_time_ticks >> system_time_ticks)
                    {
                        long ticks_per_second = sysconf(_SC_CLK_TCK);

                        if (ticks_per_second <= 0)
                        {
                            ticks_per_second = 100;
                        }

                        current_cpu_time_nanoseconds =
                            ((user_time_ticks + system_time_ticks) *
                             1000000000ULL) /
                            static_cast<uint64_t>(ticks_per_second);
                        time_retrieved = true;
                    }
                }
            }
        }
#endif
    }

    if (!time_retrieved)
    {
        return;
    }

    auto current_wall_time = std::chrono::steady_clock::now();

    if (previous_cpu_time_nanoseconds_ > 0 &&
        previous_sample_time_.time_since_epoch().count() > 0)
    {
        auto wall_time_delta_nanoseconds =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                current_wall_time - previous_sample_time_)
                .count();

        int64_t cpu_time_delta_nanoseconds = static_cast<int64_t>(
            current_cpu_time_nanoseconds - previous_cpu_time_nanoseconds_);

        if (wall_time_delta_nanoseconds > 0 && cpu_time_delta_nanoseconds >= 0)
        {
            unsigned int hardware_concurrency =
                std::thread::hardware_concurrency();

            if (hardware_concurrency == 0)
            {
                hardware_concurrency = 1;
            }

            double cpu_percentage =
                (static_cast<double>(cpu_time_delta_nanoseconds) /
                 (static_cast<double>(wall_time_delta_nanoseconds) *
                  static_cast<double>(hardware_concurrency))) *
                100.0;

            if (cpu_percentage < 0.0)
            {
                cpu_percentage = 0.0;
            }
            else if (cpu_percentage > 100.0)
            {
                cpu_percentage = 100.0;
            }

            snapshot.cpu_usage_percent = cpu_percentage;
        }
    }

    previous_cpu_time_nanoseconds_ = current_cpu_time_nanoseconds;
    previous_sample_time_ = current_wall_time;
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
    size_t current_received_bytes =
        global_process_received_bytes.load(std::memory_order_relaxed);
    size_t current_transmitted_bytes =
        global_process_transmitted_bytes.load(std::memory_order_relaxed);

    snapshot.net_rx_total_bytes = current_received_bytes;
    snapshot.net_tx_total_bytes = current_transmitted_bytes;

    if (has_sampled_network_)
    {
        snapshot.net_rx_bytes_per_sec =
            (current_received_bytes >= previous_received_bytes_)
                ? (current_received_bytes - previous_received_bytes_)
                : 0;
        snapshot.net_tx_bytes_per_sec =
            (current_transmitted_bytes >= previous_transmitted_bytes_)
                ? (current_transmitted_bytes - previous_transmitted_bytes_)
                : 0;
    }

    previous_received_bytes_ = current_received_bytes;
    previous_transmitted_bytes_ = current_transmitted_bytes;
    has_sampled_network_ = true;
}

} // namespace api
