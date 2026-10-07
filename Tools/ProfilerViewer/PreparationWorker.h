#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace ce::profiler_viewer
{
    // Reader requests coalesce before dispatch. File opens and DX preparation
    // share one bounded queue, never the engine JobScheduler or the UI thread.
    class preparation_worker
    {
    public:
        preparation_worker() : worker_([this] { run(); }) {}
        ~preparation_worker() { drain(); }
        preparation_worker(const preparation_worker&) = delete;
        preparation_worker& operator=(const preparation_worker&) = delete;

        void submit(std::function<void()> work)
        {
            {
                std::lock_guard lock(mutex_);
                if (stopping_ || queue_.size() >= maximum_pending)
                {
                    // Destroying an unstarted reader callable reports rejected
                    // admission. Never execute overflow synchronously.
                    return;
                }
                queue_.push_back(std::move(work));
            }
            ready_.notify_one();
        }

        void drain()
        {
            std::deque<std::function<void()>> abandoned;
            {
                std::lock_guard lock(mutex_);
                stopping_ = true;
                abandoned.swap(queue_);
            }
            // Rejected admissions may update reader state. Destroy callbacks
            // outside the queue mutex and do not run a backlog during shutdown.
            abandoned.clear();
            ready_.notify_all();
            if (worker_.joinable())
            {
                worker_.join();
            }
        }

    private:
        void run()
        {
            for (;;)
            {
                std::function<void()> work;
                {
                    std::unique_lock lock(mutex_);
                    ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                    if (queue_.empty())
                    {
                        return;
                    }
                    work = std::move(queue_.front());
                    queue_.pop_front();
                }
                // Preparation functions translate failures into their immutable
                // result state. Preserve host lifetime if an unexpected one escapes.
                try
                {
                    work();
                }
                catch (...)
                {
                }
            }
        }

        static constexpr std::size_t maximum_pending = 8;
        std::mutex mutex_;
        std::condition_variable ready_;
        std::deque<std::function<void()>> queue_;
        bool stopping_ = false;
        std::thread worker_;
    };
}
