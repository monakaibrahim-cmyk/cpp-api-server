#include "api/cache.hpp"

#include <algorithm>
#include <mutex>

namespace api
{

cache_engine::cache_engine(size_t max_items)
    : max_items_(max_items)
{
}

cache_engine::~cache_engine() = default;

bool cache_engine::get(const std::string& key, cached_response& out_res)
{
    std::shared_lock<std::shared_mutex> rlock(mutex_);
    auto it = store_.find(key);

    if (it == store_.end())
    {
        misses_.fetch_add(1, std::memory_order_relaxed);

        return false;
    }

    if (it->second.is_expired())
    {
        misses_.fetch_add(1, std::memory_order_relaxed);

        return false;
    }

    out_res = it->second;

    hits_.fetch_add(1, std::memory_order_relaxed);

    return true;
}

void cache_engine::set(
    const std::string& key,
    const cached_response& res,
    std::chrono::seconds ttl)
{
    std::unique_lock<std::shared_mutex> wlock(mutex_);

    if (store_.size() >= max_items_ && store_.find(key) == store_.end())
    {
        while (!lru_keys_.empty() && store_.size() >= max_items_)
        {
            std::string old_key = lru_keys_.front();

            lru_keys_.pop_front();

            auto it = store_.find(old_key);

            if (it != store_.end())
            {
                store_.erase(it);

                evictions_.fetch_add(1, std::memory_order_relaxed);

                break;
            }
        }
    }

    cached_response entry = res;

    entry.expires_at = std::chrono::steady_clock::now() + ttl;
    store_[key] = entry;

    lru_keys_.push_back(key);
}

void cache_engine::set(
    const std::string& key,
    int status,
    const std::string& body,
    const std::string& content_type,
    std::chrono::seconds ttl)
{
    cached_response res;

    res.status_code = status;
    res.body = body;
    res.content_type = content_type;

    set(key, res, ttl);
}

bool cache_engine::remove(const std::string& key)
{
    std::unique_lock<std::shared_mutex> wlock(mutex_);

    return store_.erase(key) > 0;
}

void cache_engine::clear()
{
    std::unique_lock<std::shared_mutex> wlock(mutex_);

    store_.clear();
    lru_keys_.clear();
}

cache_stats cache_engine::get_stats() const
{
    cache_stats st;

    st.hits = hits_.load(std::memory_order_relaxed);
    st.misses = misses_.load(std::memory_order_relaxed);
    st.evictions = evictions_.load(std::memory_order_relaxed);

    {
        std::shared_lock<std::shared_mutex> rlock(mutex_);

        st.items = store_.size();
    }

    size_t total = st.hits + st.misses;

    st.hit_ratio_percent = total > 0
        ? (static_cast<double>(st.hits) * 100.0 / static_cast<double>(total))
        : 0.0;

    return st;
}

void cache_engine::cleanup_expired()
{
    std::unique_lock<std::shared_mutex> wlock(mutex_);
    auto now = std::chrono::steady_clock::now();

    for (auto it = store_.begin(); it != store_.end();)
    {
        if (now >= it->second.expires_at)
        {
            it = store_.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void cache_engine::set_max_items(size_t max_items)
{
    std::unique_lock<std::shared_mutex> wlock(mutex_);

    max_items_ = max_items;
}

cache_engine& s_cache_engine()
{
    static cache_engine instance;

    return instance;
}

} // namespace api
