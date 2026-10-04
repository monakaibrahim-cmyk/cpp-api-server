#include <core/cache.h>

#include <algorithm>
#include <mutex>

namespace api
{

cache_engine::cache_engine(size_t max_items) : max_items_(max_items) {}

cache_engine::~cache_engine() = default;

bool cache_engine::get(const std::string &key, CachedResponse &out_response)
{
    std::shared_lock<std::shared_mutex> read_lock(mutex_);
    auto iterator = store_.find(key);

    if (iterator == store_.end())
    {
        misses_.fetch_add(1, std::memory_order_relaxed);

        return false;
    }

    if (iterator->second.is_expired())
    {
        misses_.fetch_add(1, std::memory_order_relaxed);

        return false;
    }

    out_response = iterator->second;

    hits_.fetch_add(1, std::memory_order_relaxed);

    return true;
}

void cache_engine::set(const std::string &key, const CachedResponse &response,
                       std::chrono::seconds time_to_live_seconds)
{
    std::unique_lock<std::shared_mutex> write_lock(mutex_);

    if (store_.size() >= max_items_ && store_.find(key) == store_.end())
    {
        while (!lru_keys_.empty() && store_.size() >= max_items_)
        {
            std::string old_key = lru_keys_.front();

            lru_keys_.pop_front();

            auto iterator = store_.find(old_key);

            if (iterator != store_.end())
            {
                store_.erase(iterator);

                evictions_.fetch_add(1, std::memory_order_relaxed);

                break;
            }
        }
    }

    CachedResponse entry = response;

    entry.expires_at = std::chrono::steady_clock::now() + time_to_live_seconds;
    store_[key] = entry;

    lru_keys_.push_back(key);
}

void cache_engine::set(const std::string &key, int status_code,
                       const std::string &body, const std::string &content_type,
                       std::chrono::seconds time_to_live_seconds)
{
    CachedResponse response;

    response.status_code = status_code;
    response.body = body;
    response.content_type = content_type;

    set(key, response, time_to_live_seconds);
}

bool cache_engine::remove(const std::string &key)
{
    std::unique_lock<std::shared_mutex> write_lock(mutex_);

    return store_.erase(key) > 0;
}

void cache_engine::clear()
{
    std::unique_lock<std::shared_mutex> write_lock(mutex_);

    store_.clear();
    lru_keys_.clear();
}

CacheStats cache_engine::get_stats() const
{
    CacheStats statistics;

    statistics.hits = hits_.load(std::memory_order_relaxed);
    statistics.misses = misses_.load(std::memory_order_relaxed);
    statistics.evictions = evictions_.load(std::memory_order_relaxed);

    {
        std::shared_lock<std::shared_mutex> read_lock(mutex_);

        statistics.items = store_.size();
    }

    size_t total_requests = statistics.hits + statistics.misses;

    statistics.hit_ratio_percent =
        total_requests > 0 ? (static_cast<double>(statistics.hits) * 100.0 /
                              static_cast<double>(total_requests))
                           : 0.0;

    return statistics;
}

void cache_engine::cleanup_expired()
{
    std::unique_lock<std::shared_mutex> write_lock(mutex_);
    auto current_time = std::chrono::steady_clock::now();

    for (auto iterator = store_.begin(); iterator != store_.end();)
    {
        if (current_time >= iterator->second.expires_at)
        {
            iterator = store_.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }
}

void cache_engine::set_max_items(size_t max_items)
{
    std::unique_lock<std::shared_mutex> write_lock(mutex_);

    max_items_ = max_items;
}

cache_engine &s_cache_engine()
{
    static cache_engine instance;

    return instance;
}

} // namespace api
