#include "api/thread_pool.hpp"
#include "api/logger.hpp"

namespace api
{

thread_pool::thread_pool(size_t thread_count)
{
    if (thread_count > 0)
    {
        start(thread_count);
    }
}

thread_pool::~thread_pool()
{
    stop();
}

void thread_pool::start(size_t thread_count)
{
    std::lock_guard<std::mutex> lock(queue_mutex_);

    if (!workers_.empty())
    {
        return;
    }

    stop_.store(false, std::memory_order_relaxed);
    workers_.reserve(thread_count);

    for (size_t i = 0; i < thread_count; ++i)
    {
        workers_.emplace_back(&thread_pool::worker_loop, this);
    }

    LOG_INFO("server", "Started background worker thread pool with "
             << thread_count << " worker threads");
}

void thread_pool::stop()
{
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);

        if (stop_.load(std::memory_order_relaxed) && workers_.empty())
        {
            return;
        }

        stop_.store(true, std::memory_order_relaxed);
    }

    cv_.notify_all();

    for (std::thread& worker : workers_)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }

    workers_.clear();
}

void thread_pool::post(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);

        if (stop_.load(std::memory_order_relaxed))
        {
            return;
        }

        tasks_.push(std::move(task));
    }

    cv_.notify_one();
}

void thread_pool::worker_loop()
{
    while (true)
    {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            cv_.wait(
                lock,
                [this]()
                {
                    return stop_.load(std::memory_order_relaxed) || !tasks_.empty();
                });

            if (stop_.load(std::memory_order_relaxed) && tasks_.empty())
            {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();

            active_tasks_.fetch_add(1, std::memory_order_relaxed);
        }

        try
        {
            task();
        }
        catch (const std::exception& e)
        {
            LOG_ERROR("server", "Unhandled exception in background worker task: " << e.what());
        }
        catch (...)
        {
            LOG_ERROR("server", "Unknown exception in background worker task");
        }

        active_tasks_.fetch_sub(1, std::memory_order_relaxed);
        completed_tasks_.fetch_add(1, std::memory_order_relaxed);
    }
}

size_t thread_pool::thread_count() const noexcept
{
    std::lock_guard<std::mutex> lock(queue_mutex_);

    return workers_.size();
}

size_t thread_pool::active_tasks() const noexcept
{
    return active_tasks_.load(std::memory_order_relaxed);
}

size_t thread_pool::pending_tasks() const noexcept
{
    std::lock_guard<std::mutex> lock(queue_mutex_);

    return tasks_.size();
}

size_t thread_pool::completed_tasks() const noexcept
{
    return completed_tasks_.load(std::memory_order_relaxed);
}

bool thread_pool::is_running() const noexcept
{
    return !stop_.load(std::memory_order_relaxed) && !workers_.empty();
}

thread_pool& s_thread_pool()
{
    static thread_pool s_pool;

    return s_pool;
}

} // namespace api
