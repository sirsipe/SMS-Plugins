#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>

namespace sms::audio {

/** Serialized control callers hand a bounded, non-throwing operation to the
 * next audio block. Neither servicing nor completing a command locks or wakes
 * an OS primitive. Only the waiting control caller sleeps. */
class RealtimeCommandDispatcher {
public:
    using Operation = void (*)(void*) noexcept;

    void activate()
    {
        const std::lock_guard lock(controlMutex_);
        active_.store(true, std::memory_order_release);
    }

    // The host must have stopped callbacks before calling deactivate.
    void deactivate() noexcept { active_.store(false, std::memory_order_release); }

    // As with deactivate, the owner must first stop audio callbacks. Waiting
    // commands can finish inline; future commands fail before touching state.
    void shutdown() noexcept
    {
        stopped_.store(true, std::memory_order_release);
        active_.store(false, std::memory_order_release);
    }

    void service() noexcept
    {
        State expected = State::pending;
        if (!state_.compare_exchange_strong(expected, State::running,
                                             std::memory_order_acq_rel))
            return;
        operation_(context_);
        state_.store(State::done, std::memory_order_release);
    }

    void dispatch(Operation operation, void* context) const
    {
        const std::lock_guard lock(controlMutex_);
        if (stopped_.load(std::memory_order_acquire))
            throw std::runtime_error("Sampler is shutting down");
        if (!active_.load(std::memory_order_acquire)) {
            operation(context);
            return;
        }
        operation_ = operation;
        context_ = context;
        state_.store(State::pending, std::memory_order_release);
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(2);
        for (;;) {
            if (state_.load(std::memory_order_acquire) == State::done)
                break;
            if (!active_.load(std::memory_order_acquire)) {
                State expected = State::pending;
                if (state_.compare_exchange_strong(expected, State::running,
                                                   std::memory_order_acq_rel)) {
                    operation(context);
                    state_.store(State::done, std::memory_order_release);
                    break;
                }
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                State expected = State::pending;
                if (state_.compare_exchange_strong(expected, State::idle,
                                                   std::memory_order_acq_rel))
                    throw std::runtime_error("Host is not processing sampler audio");
                // A claimed command must finish before its stack context dies.
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        state_.store(State::idle, std::memory_order_release);
    }

    template <class Callback>
    void invoke(Callback&& callback) const
    {
        static_assert(std::is_nothrow_invocable_v<Callback&>,
                      "audio commands must not throw");
        using Callable = std::remove_reference_t<Callback>;
        dispatch([](void* opaque) noexcept {
            (*static_cast<Callable*>(opaque))();
        }, &callback);
    }

    static void dispatchFromEngine(void* dispatcher, Operation operation,
                                   void* context)
    {
        static_cast<RealtimeCommandDispatcher*>(dispatcher)->dispatch(operation, context);
    }

private:
    enum class State : std::uint8_t { idle, pending, running, done };
    static_assert(std::atomic<State>::is_always_lock_free);
    static_assert(std::atomic<bool>::is_always_lock_free);
    mutable std::mutex controlMutex_;
    mutable std::atomic<State> state_{State::idle};
    std::atomic<bool> active_{false};
    std::atomic<bool> stopped_{false};
    mutable Operation operation_ = nullptr;
    mutable void* context_ = nullptr;
};

} // namespace sms::audio
