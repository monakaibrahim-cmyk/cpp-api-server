#include "api/connection_tracker.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace api
{

static std::string format_time_now()
{
    auto tp = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(tp);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()) % 1000;
    std::tm tm_buf{};
    char buf[32];

    localtime_r(&tt, &tm_buf);
    std::snprintf(
        buf,
        sizeof(buf),
        "%02d:%02d:%02d.%03d",
        tm_buf.tm_hour,
        tm_buf.tm_min,
        tm_buf.tm_sec,
        static_cast<int>(ms.count())
    );

    return buf;
}

connection_tracker::connection_tracker(size_t history_capacity)
    : history_capacity_(history_capacity)
{
}

uint64_t connection_tracker::on_request_start(
    const std::string& remote_ip,
    const std::string& method,
    const std::string& url,
    const std::string& full_url
)
{
    uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);

    active_.fetch_add(1, std::memory_order_relaxed);
    total_.fetch_add(1, std::memory_order_relaxed);

    connection_record rec;

    rec.id = id;
    rec.timestamp = format_time_now();
    rec.direction = connection_direction::incoming;
    rec.remote_ip = remote_ip.empty() ? "unknown" : remote_ip;
    rec.method = method;
    rec.url = url;
    rec.full_url = full_url.empty() ? url : full_url;
    rec.status_code = 0;
    rec.duration_ms = 0.0;
    rec.in_flight = true;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        connections_.push_back(rec);

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
        if (active_.compare_exchange_weak(current, current - 1, std::memory_order_relaxed))
        {
            break;
        }
    }
}

void connection_tracker::on_request_end(
    uint64_t id,
    int status_code,
    double duration_ms
)
{
    if (id == 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto it = connections_.rbegin(); it != connections_.rend(); ++it)
    {
        if (it->id == id)
        {
            if (it->in_flight)
            {
                it->status_code = status_code;
                it->duration_ms = duration_ms;
                it->in_flight = false;
                endpoint_hits_[it->url]++;

                decrement_active();
            }

            break;
        }
    }
}

void connection_tracker::record_completed_request(
    const std::string& remote_ip,
    const std::string& method,
    const std::string& url,
    const std::string& full_url,
    int status_code,
    double duration_ms
)
{
    uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);

    total_.fetch_add(1, std::memory_order_relaxed);

    connection_record rec;

    rec.id = id;
    rec.timestamp = format_time_now();
    rec.direction = connection_direction::incoming;
    rec.remote_ip = remote_ip.empty() ? "unknown" : remote_ip;
    rec.method = method;
    rec.url = url;
    rec.full_url = full_url.empty() ? url : full_url;
    rec.status_code = status_code;
    rec.duration_ms = duration_ms;
    rec.in_flight = false;

    std::lock_guard<std::mutex> lock(mutex_);

    connections_.push_back(rec);

    while (connections_.size() > history_capacity_)
    {
        connections_.pop_front();
    }

    request_timestamps_.push_back(std::chrono::steady_clock::now());
    endpoint_hits_[url]++;
}

uint64_t connection_tracker::on_outgoing_start(
    const std::string& remote_ip,
    uint16_t remote_port,
    const std::string& method,
    const std::string& target,
    const std::string& full_url
)
{
    uint64_t id = next_id_.fetch_add(1, std::memory_order_relaxed);

    active_.fetch_add(1, std::memory_order_relaxed);

    connection_record rec;

    rec.id = id;
    rec.timestamp = format_time_now();
    rec.direction = connection_direction::outgoing;
    rec.remote_ip = remote_ip;
    rec.remote_port = remote_port;
    rec.method = method;
    rec.url = target;
    rec.full_url = full_url.empty() ? target : full_url;
    rec.status_code = 0;
    rec.duration_ms = 0.0;
    rec.in_flight = true;

    {
        std::lock_guard<std::mutex> lock(mutex_);

        connections_.push_back(rec);

        while (connections_.size() > history_capacity_)
        {
            connections_.pop_front();
        }
    }

    return id;
}

void connection_tracker::on_outgoing_end(
    uint64_t id,
    int status_code,
    double duration_ms
)
{
    if (id == 0)
    {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto it = connections_.rbegin(); it != connections_.rend(); ++it)
    {
        if (it->id == id)
        {
            if (it->in_flight)
            {
                it->status_code = status_code;
                it->duration_ms = duration_ms;
                it->in_flight = false;

                decrement_active();
            }

            break;
        }
    }
}

connection_stats connection_tracker::get_stats() const
{
    connection_stats stats;

    stats.active_connections = active_.load(std::memory_order_relaxed);
    stats.total_connections = total_.load(std::memory_order_relaxed);

    std::lock_guard<std::mutex> lock(mutex_);
    auto now = std::chrono::steady_clock::now();

    while (!request_timestamps_.empty() &&
           std::chrono::duration_cast<std::chrono::seconds>(now - request_timestamps_.front()).count() > 60)
    {
        request_timestamps_.pop_front();
    }

    stats.requests_per_second = request_timestamps_.size() / 60.0;
    stats.endpoint_hits = endpoint_hits_;

    return stats;
}

std::vector<connection_record> connection_tracker::get_recent_connections(size_t limit) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<connection_record> result;
    size_t count = std::min(limit, connections_.size());

    result.reserve(count);

    auto start_it = connections_.end() - count;

    result.assign(start_it, connections_.end());

    return result;
}

std::vector<connection_record> connection_tracker::get_active_connections() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<connection_record> result;

    for (const auto& rec : connections_)
    {
        if (rec.in_flight)
        {
            result.push_back(rec);
        }
    }

    return result;
}

} // namespace api
