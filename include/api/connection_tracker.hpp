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

enum class connection_direction
{
    incoming,
    outgoing
};

struct connection_record
{
    uint64_t id = 0;
    std::string timestamp;
    connection_direction direction = connection_direction::incoming;
    std::string remote_ip;
    uint16_t remote_port = 0;
    std::string method;
    std::string url;
    std::string full_url;
    int status_code = 0;
    double duration_ms = 0.0;
    bool in_flight = true;
};

struct connection_stats
{
    size_t active_connections = 0;
    size_t total_connections = 0;
    double requests_per_second = 0.0;
    std::unordered_map<std::string, size_t> endpoint_hits;
};

class connection_tracker
{
public:
    explicit connection_tracker(size_t history_capacity = 500);

    uint64_t on_request_start(const std::string& remote_ip,
                              const std::string& method,
                              const std::string& url,
                              const std::string& full_url = "");

    void on_request_end(uint64_t id, int status_code, double duration_ms);

    void record_completed_request(const std::string& remote_ip,
                                  const std::string& method,
                                  const std::string& url,
                                  const std::string& full_url,
                                  int status_code,
                                  double duration_ms);

    uint64_t on_outgoing_start(const std::string& remote_ip,
                               uint16_t remote_port,
                               const std::string& method,
                               const std::string& target,
                               const std::string& full_url = "");

    void on_outgoing_end(uint64_t id, int status_code, double duration_ms);

    connection_stats get_stats() const;
    std::vector<connection_record> get_recent_connections(size_t limit = 20) const;
    std::vector<connection_record> get_active_connections() const;

private:
    void decrement_active();

    std::atomic<uint64_t> next_id_{1};
    std::atomic<size_t> active_{0};
    std::atomic<size_t> total_{0};

    mutable std::mutex mutex_;
    size_t history_capacity_;
    std::deque<connection_record> connections_;
    std::unordered_map<std::string, size_t> endpoint_hits_;
    mutable std::deque<std::chrono::steady_clock::time_point> request_timestamps_;
};

} // namespace api
