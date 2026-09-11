#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <thread>
#include <utility>

namespace sms::audio {

/**
 * Lets non-real-time code access state normally owned by one audio callback.
 * The audio side only checks atomics and yields access at a block boundary.
 */
class RealtimeAccessGate {
public:
    void activate() noexcept
    {
        active_.store(true, std::memory_order_release);
    }

    void deactivate() noexcept
    {
        active_.store(false, std::memory_order_release);
        State expected = State::requested;
        static_cast<void>(state_.compare_exchange_strong(
            expected, State::paused, std::memory_order_acq_rel));
    }

    /** Call once at the start of the audio callback; true means skip protected state. */
    [[nodiscard]] bool audioShouldYield() noexcept
    {
        State state = state_.load(std::memory_order_acquire);
        if (state == State::idle)
            return false;
        if (state == State::requested) {
            if (state_.compare_exchange_strong(
                    state, State::paused, std::memory_order_acq_rel))
                return true;
        }
        return state == State::paused;
    }

    /** Run a control-thread callback while the audio side is yielding. */
    template <class Callback>
    [[nodiscard]] bool withPaused(Callback&& callback,
                                  const std::chrono::milliseconds timeout =
                                      std::chrono::milliseconds(500)) const
    {
        const std::lock_guard lock(controlMutex_);
        state_.store(State::requested, std::memory_order_release);
        if (!active_.load(std::memory_order_acquire)) {
            State expected = State::requested;
            static_cast<void>(state_.compare_exchange_strong(
                expected, State::paused, std::memory_order_acq_rel));
        }

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (state_.load(std::memory_order_acquire) != State::paused) {
            if (std::chrono::steady_clock::now() >= deadline) {
                State expected = State::requested;
                if (state_.compare_exchange_strong(
                        expected, State::idle, std::memory_order_acq_rel))
                    return false;
                if (expected == State::paused)
                    break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        try {
            std::forward<Callback>(callback)();
        } catch (...) {
            state_.store(State::idle, std::memory_order_release);
            return false;
        }
        state_.store(State::idle, std::memory_order_release);
        return true;
    }

private:
    enum class State : std::uint8_t { idle, requested, paused };
    static_assert(std::atomic<State>::is_always_lock_free,
                  "the audio-side access gate must be lock-free");

    mutable std::mutex controlMutex_;
    mutable std::atomic<State> state_{State::idle};
    std::atomic<bool> active_{false};
};

} // namespace sms::audio
