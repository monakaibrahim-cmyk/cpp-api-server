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

/**
 * @brief Represents a cached HTTP response payload along with its metadata and
 * expiration time.
 */
struct CachedResponse
{
    /** @brief HTTP response status code (e.g., 200, 404). */
    int status_code = 200;

    /** @brief Serialized HTTP response body payload. */
    std::string body;

    /** @brief MIME content type header (default: "application/json"). */
    std::string content_type = "application/json";

    /** @brief Custom HTTP headers associated with the cached response. */
    std::vector<std::pair<std::string, std::string>> headers;

    /** @brief Exact steady clock time point when this cache item expires. */
    std::chrono::steady_clock::time_point expires_at;

    /**
     * @brief Checks whether the cached response has exceeded its expiration
     * time.
     *
     * @return true If current time is greater than or equal to @ref expires_at.
     * @return false If the cached response is still fresh and valid.
     */
    bool is_expired() const
    {
        return std::chrono::steady_clock::now() >= expires_at;
    }
};

/// Backward compatibility alias
using cached_response = CachedResponse;

/**
 * @brief Operational statistics and telemetry counters for the in-memory cache
 * engine.
 */
struct CacheStats
{
    /** @brief Total count of successful cache hits. */
    size_t hits = 0;

    /** @brief Total count of cache lookups resulting in miss or expired state.
     */
    size_t misses = 0;

    /** @brief Current total number of distinct entries stored in cache memory.
     */
    size_t items = 0;

    /** @brief Total number of items evicted due to exceeding capacity. */
    size_t evictions = 0;

    /** @brief Computed cache hit ratio percentage in the range 0.0 to 100.0. */
    double hit_ratio_percent = 0.0;
};

/// Backward compatibility alias
using cache_stats = CacheStats;

/**
 * @brief High-performance concurrent LRU in-memory HTTP response cache engine.
 *
 * @details Implements a thread-safe in-memory cache guarded by a shared
 * reader-writer mutex (@c std::shared_mutex). Multiple reader threads can
 * simultaneously query cached responses without contention, while cache
 * insertions, deletions, and LRU evictions acquire exclusive write locks.
 *
 * @headerfile api/cache.h
 */
class cache_engine
{
  public:
    /**
     * @brief Constructs cache engine instance with maximum capacity limit.
     *
     * @param[in] max_items Maximum number of cached items retained before
     * evicting LRU entries.
     */
    explicit cache_engine(size_t max_items = 1000);

    /**
     * @brief Destructor releasing cache store resources.
     */
    ~cache_engine();

    /**
     * @brief Retrieves a cached response by key if present and not yet expired.
     *
     * @param[in] key Cache lookup key.
     * @param[out] out_response Destination struct populated with response data.
     * @return true If a valid, unexpired entry was located and returned.
     * @return false On cache miss or if the entry has expired.
     */
    bool get(const std::string &key, CachedResponse &out_response);

    /**
     * @brief Stores a pre-built @ref CachedResponse with an explicit
     * time-to-live.
     *
     * @param[in] key Unique cache key.
     * @param[in] response The response object containing payload and header
     * data.
     * @param[in] time_to_live_seconds Duration in seconds before expiration.
     */
    void set(const std::string &key, const CachedResponse &response,
             std::chrono::seconds time_to_live_seconds);

    /**
     * @brief Convenience overload storing a response directly from individual
     * attributes.
     *
     * @param[in] key Unique cache key.
     * @param[in] status_code HTTP status code.
     * @param[in] body Serialized response body string.
     * @param[in] content_type HTTP Content-Type header value.
     * @param[in] time_to_live_seconds Duration in seconds before expiration.
     */
    void set(const std::string &key, int status_code, const std::string &body,
             const std::string &content_type,
             std::chrono::seconds time_to_live_seconds);

    /**
     * @brief Explicitly invalidates and removes an entry from the cache.
     *
     * @param[in] key Cache key to purge.
     * @return true If the item existed and was removed; false if key was
     * absent.
     */
    bool remove(const std::string &key);

    /**
     * @brief Purges all stored cache entries and resets the LRU tracking queue.
     */
    void clear();

    /**
     * @brief Retrieves a thread-safe snapshot of cumulative cache performance
     * metrics.
     *
     * @return CacheStats Metrics object containing hits, misses, evictions,
     * item count, and hit ratio.
     */
    CacheStats get_stats() const;

    /**
     * @brief Sweeps the cache and removes all entries whose TTL has elapsed.
     */
    void cleanup_expired();

    /**
     * @brief Dynamically adjusts maximum item capacity.
     *
     * @param[in] max_items New maximum item count limit.
     */
    void set_max_items(size_t max_items);

  private:
    size_t max_items_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, CachedResponse> store_;
    std::deque<std::string> lru_keys_;

    std::atomic<size_t> hits_{0};
    std::atomic<size_t> misses_{0};
    std::atomic<size_t> evictions_{0};
};

/**
 * @brief Returns the global singleton instance of the response cache engine.
 *
 * @return cache_engine& Reference to the process-wide cache engine.
 */
cache_engine &s_cache_engine();

} // namespace api
