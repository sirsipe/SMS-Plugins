#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace midichopper::plugin {

/** Per-instance VST3 UI work queue. Host durable state decodes and stages
 * updates synchronously; transient commands return to the UI promptly. */
class ControlWorker {
public:
    ControlWorker() : thread_([this] { run(); }) {}
    ~ControlWorker() { stop(); }

    [[nodiscard]] bool submit(std::function<void()> operation)
    {
        const std::lock_guard lock(mutex_);
        if (stopping_ || queue_.size() >= 64U)
            return false;
        queue_.push_back(std::move(operation));
        ready_.notify_one();
        return true;
    }

    void stop()
    {
        {
            const std::lock_guard lock(mutex_);
            stopping_ = true;
            queue_.clear();
        }
        ready_.notify_one();
        if (thread_.joinable())
            thread_.join();
    }

private:
    void run() noexcept
    {
        for (;;) {
            std::function<void()> operation;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (stopping_)
                    return;
                operation = std::move(queue_.front());
                queue_.pop_front();
            }
            try { operation(); } catch (...) { }
        }
    }

    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> queue_;
    bool stopping_ = false;
    std::thread thread_;
};

} // namespace midichopper::plugin
