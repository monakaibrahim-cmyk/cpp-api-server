#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace api
{

class thread_pool
{
public:
    explicit thread_pool(size_t thread_count = 0);
    ~thread_pool();

    thread_pool(const thread_pool&) = delete;
    thread_pool& operator=(const thread_pool&) = delete;

    void start(size_t thread_count);
    void stop();

    void post(std::function<void()> task);

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<typename std::invoke_result<F, Args...>::type>
    {
        using return_type = typename std::invoke_result<F, Args...>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            [func = std::forward<F>(f),
             ... captured_args = std::forward<Args>(args)]() mutable
            {
                return std::invoke(std::move(func), std::move(captured_args)...);
            });

        std::future<return_type> res = task->get_future();

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);

            if (stop_.load(std::memory_order_relaxed))
            {
                throw std::runtime_error("Cannot submit task to stopped thread_pool");
            }

            tasks_.emplace([task]()
            {
                (*task)();
            });
        }

        cv_.notify_one();

        return res;
    }

    size_t thread_count() const noexcept;
    size_t active_tasks() const noexcept;
    size_t pending_tasks() const noexcept;
    size_t completed_tasks() const noexcept;
    bool is_running() const noexcept;

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;

    mutable std::mutex queue_mutex_;
    std::condition_variable cv_;

    std::atomic<bool> stop_{false};
    std::atomic<size_t> active_tasks_{0};
    std::atomic<size_t> completed_tasks_{0};
};

thread_pool& s_thread_pool();

} // namespace api
