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

/**
 * @brief Thread pool executing asynchronous background jobs and database
 * operations.
 *
 * Implements a FIFO work-stealing style task queue serviced by a dedicated pool
 * of worker threads. Designed to offload CPU-bound calculations, disk I/O, and
 * asynchronous database operations away from the HTTP network listener loop.
 *
 * Supports two distinct execution patterns:
 * - Fire-and-forget dispatching via @ref post() for background maintenance or
 * metrics.
 * - Value-returning asynchronous invocations via @ref submit() yielding @c
 * std::future<T>.
 *
 * @par Asynchronous Task Dispatch Example
 * @code{.cpp}
 * api::thread_pool pool(4);
 *
 * // Fire-and-forget task
 * pool.post([]()
 * {
 *     // Background audit logging
 * });
 *
 * // Future-returning task with arguments
 * std::future<int> calculation_future = pool.submit(
 *     [](int base, int multiplier)
 *     {
 *         return base * multiplier;
 *     },
 *     42,
 *     10
 * );
 *
 * int result = calculation_future.get(); // Blocks until worker finishes (420)
 * pool.stop();
 * @endcode
 *
 * @thread_safety All public methods are safe to invoke concurrently across
 * multiple threads. Worker queue access is synchronized via @c std::mutex and
 * task arrival is signaled via
 * @c std::condition_variable.
 *
 * @headerfile api/thread_pool.h
 */
class thread_pool
{
  public:
    /**
     * @brief Constructs a @ref thread_pool, optionally launching worker threads
     * immediately.
     *
     * @param thread_count Number of worker threads to start immediately (if 0,
     * pool remains idle until @ref start() is called).
     */
    explicit thread_pool(size_t thread_count = 0);

    /**
     * @brief Destructor stopping all worker threads and joining their
     * executions.
     *
     * Invocations of @ref stop() are performed automatically upon destruction
     * to prevent detached or dangling threads.
     */
    ~thread_pool();

    /**
     * @brief Deleted copy constructor.
     */
    thread_pool(const thread_pool &) = delete;

    /**
     * @brief Deleted copy assignment operator.
     */
    thread_pool &operator=(const thread_pool &) = delete;

    /**
     * @brief Initializes and launches worker threads if the pool is not already
     * running.
     *
     * Spawns @p thread_count worker threads executing the internal processing
     * loop. If worker threads are already running, this method is a safe no-op.
     *
     * @param thread_count Number of worker threads to spawn.
     */
    void start(size_t thread_count);

    /**
     * @brief Signals all worker threads to stop and joins their execution.
     *
     * Sets the internal stop flag, wakes all sleeping workers via condition
     * variable, and joins all underlying @c std::thread objects. If the pool is
     * already stopped, this call returns immediately.
     */
    void stop();

    /**
     * @brief Enqueues a fire-and-forget task callback to the worker queue.
     *
     * Pushes @p task to the FIFO queue and signals an available worker thread.
     * If the pool has been signaled to stop, the task is discarded safely.
     *
     * @param task Void callback invocable without arguments.
     */
    void post(std::function<void()> task);

    /**
     * @brief Submits an invocable task returning a @c std::future for
     * asynchronous result retrieval.
     *
     * Wraps the invocable in a @c std::packaged_task and enqueues it for worker
     * execution. Throws @c std::runtime_error if called on a stopped thread
     * pool.
     *
     * @tparam Callable Function, functor, or lambda type to execute.
     * @tparam Arguments Variadic argument types forwarded to the invocable.
     * @param function_target Invocable target to execute on a worker thread.
     * @param arguments Arguments forwarded into @p function_target.
     * @return @c std::future holding the return value of @p function_target.
     * @throws std::runtime_error if the thread pool has been stopped.
     */
    template <typename Callable, typename... Arguments>
    auto submit(Callable &&function_target, Arguments &&...arguments)
        -> std::future<
            typename std::invoke_result<Callable, Arguments...>::type>
    {
        using return_type =
            typename std::invoke_result<Callable, Arguments...>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            [func = std::forward<Callable>(function_target),
             ... captured_arguments =
                 std::forward<Arguments>(arguments)]() mutable
            {
                return std::invoke(std::move(func),
                                   std::move(captured_arguments)...);
            });

        std::future<return_type> future_result = task->get_future();

        {
            std::lock_guard<std::mutex> lock(queue_mutex_);

            if (stop_.load(std::memory_order_relaxed))
            {
                throw std::runtime_error(
                    "Cannot submit task to stopped thread_pool");
            }

            tasks_.emplace([task]() { (*task)(); });
        }

        condition_variable_.notify_one();

        return future_result;
    }

    /**
     * @brief Returns the total number of configured worker threads.
     *
     * @return Worker thread count.
     */
    size_t thread_count() const noexcept;

    /**
     * @brief Returns the count of tasks currently executing on worker threads.
     *
     * @return Number of active in-flight worker tasks.
     */
    size_t active_tasks() const noexcept;

    /**
     * @brief Returns the count of tasks waiting in the queue for an available
     * worker thread.
     *
     * @return Number of pending queued tasks.
     */
    size_t pending_tasks() const noexcept;

    /**
     * @brief Returns the lifetime count of successfully completed tasks.
     *
     * @return Total count of tasks processed since pool initialization.
     */
    size_t completed_tasks() const noexcept;

    /**
     * @brief Returns whether the thread pool workers are currently active.
     *
     * @return @c true if workers are running and stop flag is not set; @c false
     * otherwise.
     */
    bool is_running() const noexcept;

  private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;

    mutable std::mutex queue_mutex_;
    std::condition_variable condition_variable_;

    std::atomic<bool> stop_{false};
    std::atomic<size_t> active_tasks_{0};
    std::atomic<size_t> completed_tasks_{0};
};

/**
 * @brief Returns the global singleton thread pool instance.
 *
 * @return Reference to the @ref thread_pool singleton.
 */
thread_pool &s_thread_pool();

} // namespace api
