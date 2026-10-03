#include "api/connection_tracker.h"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace api
{

static std::string format_time_now()
{
    auto time_point = std::chrono::system_clock::now();
    auto time_t_value = std::chrono::system_clock::to_time_t(time_point);
    auto milliseconds_remainder =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            time_point.time_since_epoch()) %
        1000;
    std::tm time_buffer{};
    char formatted_buffer[32];

    localtime_r(&time_t_value, &time_buffer);
    std::snprintf(formatted_buffer, sizeof(formatted_buffer),
                  "%02d:%02d:%02d.%03d", time_buffer.tm_hour,
                  time_buffer.tm_min, time_buffer.tm_sec,
                  static_cast<int>(milliseconds_remainder.count()));

    return formatted_buffer;
}

connection_tracker::connection_tracker(size_t history_capacity)
    : history_capacity_(history_capacity)
{
}

uint64_t connection_tracker::on_request_start(const std::string &remote_ip,
                                              const std::string &method,
                                              const std::string &url,
                                              const std::string &full_url)
{
    uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);

    active_.fetch_add(1, std::memory_order_relaxed);
    total_.fetch_add(1, std::memory_order_relaxed);

    ConnectionRecord record;

    record.id = id;
    record.timestamp = format_time_now();
    record.direction = connection_direction::incoming;
    record.remote_ip = remote_ip.empty() ? "unknown" : remote_ip;
    record.method = method;
    record.url = url;
    record.full_url = full_url.empty() ? url : full_url;
    record.status_code = 0;
    record.duration_ms = 0.0;
    record.in_flight = true;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        connections_.push_back(record);

        while (connections_.size() > history_capacity_)
        {
            connections_.pop_front();
        }

        request_timestamps_.push_back(std::chrono::steady_clock::now());
    }

    return id;
}

void connection_tracker::decrement_active()
{
    size_t current = active_.load(std::memory_order_relaxed);

    while (current > 0)
    {
        if (active_.compare_exchange_weak(current, current - 1,
                                          std::memory_order_relaxed))
        {
            break;
        }
    }
}

void connection_tracker::on_request_end(uint64_t id, int status_code,
                                        double duration_milliseconds)
{
    if (id == 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto iterator = connections_.rbegin(); iterator != connections_.rend();
         ++iterator)
    {
        if (iterator->id == id)
        {
            if (iterator->in_flight)
            {
                iterator->status_code = status_code;
                iterator->duration_ms = duration_milliseconds;
                iterator->in_flight = false;
                endpoint_hits_[iterator->url]++;

                decrement_active();
            }

            break;
        }
    }
}

void connection_tracker::record_completed_request(const std::string &remote_ip,
                                                  const std::string &method,
                                                  const std::string &url,
                                                  const std::string &full_url,
                                                  int status_code,
                                                  double duration_milliseconds)
{
    uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);

    total_.fetch_add(1, std::memory_order_relaxed);

    ConnectionRecord record;

    record.id = id;
    record.timestamp = format_time_now();
    record.direction = connection_direction::incoming;
    record.remote_ip = remote_ip.empty() ? "unknown" : remote_ip;
    record.method = method;
    record.url = url;
    record.full_url = full_url.empty() ? url : full_url;
    record.status_code = status_code;
    record.duration_ms = duration_milliseconds;
    record.in_flight = false;

    std::lock_guard<std::mutex> lock(mutex_);

    connections_.push_back(record);

    while (connections_.size() > history_capacity_)
    {
        connections_.pop_front();
    }

    request_timestamps_.push_back(std::chrono::steady_clock::now());
    endpoint_hits_[url]++;
}

uint64_t connection_tracker::on_outgoing_start(const std::string &remote_ip,
                                               uint16_t remote_port,
                                               const std::string &method,
                                               const std::string &target,
                                               const std::string &full_url)
{
    uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);

    active_.fetch_add(1, std::memory_order_relaxed);

    ConnectionRecord record;

    record.id = id;
    record.timestamp = format_time_now();
    record.direction = connection_direction::outgoing;
    record.remote_ip = remote_ip;
    record.remote_port = remote_port;
    record.method = method;
    record.url = target;
    record.full_url = full_url.empty() ? target : full_url;
    record.status_code = 0;
    record.duration_ms = 0.0;
    record.in_flight = true;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        connections_.push_back(record);

        while (connections_.size() > history_capacity_)
        {
            connections_.pop_front();
        }
    }

    return id;
}

void connection_tracker::on_outgoing_end(uint64_t id, int status_code,
                                         double duration_milliseconds)
{
    if (id == 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto iterator = connections_.rbegin(); iterator != connections_.rend();
         ++iterator)
    {
        if (iterator->id == id)
        {
            if (iterator->in_flight)
            {
                iterator->status_code = status_code;
                iterator->duration_ms = duration_milliseconds;
                iterator->in_flight = false;

                decrement_active();
            }

            break;
        }
    }
}

ConnectionStats connection_tracker::get_stats() const
{
    ConnectionStats statistics;

    statistics.active_connections = active_.load(std::memory_order_relaxed);
    statistics.total_connections = total_.load(std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();

    while (!request_timestamps_.empty() &&
           std::chrono::duration_cast<std::chrono::seconds>(
               now - request_timestamps_.front())
                   .count() > 60)
    {
        request_timestamps_.pop_front();
    }

    statistics.requests_per_second = request_timestamps_.size() / 60.0;
    statistics.endpoint_hits = endpoint_hits_;

    return statistics;
}

std::vector<ConnectionRecord>
connection_tracker::get_recent_connections(size_t maximum_count) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ConnectionRecord> result;
    size_t count = std::min(maximum_count, connections_.size());

    result.reserve(count);

    auto start_iterator = connections_.end() - count;

    result.assign(start_iterator, connections_.end());

    return result;
}

std::vector<ConnectionRecord> connection_tracker::get_active_connections() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ConnectionRecord> result;

    for (const auto &record : connections_)
    {
        if (record.in_flight)
        {
            result.push_back(record);
        }
    }

    return result;
}

} // namespace api
