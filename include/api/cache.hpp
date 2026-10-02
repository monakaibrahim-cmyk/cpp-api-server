#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace api
{

struct cached_response
{
    int status_code = 200;
    std::string body;
    std::string content_type = "application/json";
    std::vector<std::pair<std::string, std::string>> headers;
    std::chrono::steady_clock::time_point expires_at;

    bool is_expired() const
    {
        return std::chrono::steady_clock::now() >= expires_at;
    }
};

struct cache_stats
{
    size_t hits = 0;
    size_t misses = 0;
    size_t items = 0;
    size_t evictions = 0;
    double hit_ratio_percent = 0.0;
};

class cache_engine
{
public:
    explicit cache_engine(size_t max_items = 1000);
    ~cache_engine();

    bool get(const std::string& key, cached_response& out_res);
    void set(const std::string& key, const cached_response& res, std::chrono::seconds ttl);
    void set(const std::string& key, int status, const std::string& body,
             const std::string& content_type, std::chrono::seconds ttl);

    bool remove(const std::string& key);
    void clear();

    cache_stats get_stats() const;
    void cleanup_expired();
    void set_max_items(size_t max_items);

private:
    size_t max_items_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, cached_response> store_;
    std::deque<std::string> lru_keys_;

    std::atomic<size_t> hits_{0};
    std::atomic<size_t> misses_{0};
    std::atomic<size_t> evictions_{0};
};

cache_engine& s_cache_engine();

} // namespace api
