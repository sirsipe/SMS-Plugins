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
    class AudioAccess {
    public:
        AudioAccess(const AudioAccess&) = delete;
        AudioAccess& operator=(const AudioAccess&) = delete;

        ~AudioAccess()
        {
            if (ownsState_)
                gate_.finishAudioAccess();
        }

        /** True means another thread owns or is waiting for the protected state. */
        [[nodiscard]] bool shouldYield() const noexcept { return !ownsState_; }

    private:
        friend class RealtimeAccessGate;

        explicit AudioAccess(RealtimeAccessGate& gate, const bool ownsState) noexcept
            : gate_(gate), ownsState_(ownsState) {}

        RealtimeAccessGate& gate_;
        const bool ownsState_;
    };

    /** Hold for the complete audio callback while touching protected state. */
    [[nodiscard]] AudioAccess audioAccess() noexcept
    {
        State expected = State::idle;
        const bool acquired = state_.compare_exchange_strong(
            expected, State::audioActive, std::memory_order_acq_rel);
        return AudioAccess(*this, acquired);
    }

    /** Run a control-thread callback while the audio side is yielding. */
    template <class Callback>
    [[nodiscard]] bool withPaused(Callback&& callback,
                                  const std::chrono::milliseconds timeout =
                                      std::chrono::milliseconds(500)) const
    {
        const std::lock_guard lock(controlMutex_);
        State state = state_.load(std::memory_order_acquire);
        for (;;) {
            if (state == State::idle) {
                if (state_.compare_exchange_weak(
                        state, State::controlActive, std::memory_order_acq_rel))
                    break;
                continue;
            }
            if (state == State::audioActive) {
                if (state_.compare_exchange_weak(
                        state, State::pauseRequested, std::memory_order_acq_rel))
                    break;
                continue;
            }
            break;
        }

        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (state_.load(std::memory_order_acquire) != State::controlActive) {
            if (std::chrono::steady_clock::now() >= deadline) {
                State expected = State::pauseRequested;
                if (state_.compare_exchange_strong(
                        expected, State::audioActive, std::memory_order_acq_rel))
                    return false;
                if (expected == State::controlActive)
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
    enum class State : std::uint8_t { idle, audioActive, pauseRequested, controlActive };
    static_assert(std::atomic<State>::is_always_lock_free,
                  "the audio-side access gate must be lock-free");

    void finishAudioAccess() noexcept
    {
        State state = state_.load(std::memory_order_acquire);
        for (;;) {
            if (state == State::audioActive) {
                if (state_.compare_exchange_weak(
                        state, State::idle, std::memory_order_acq_rel))
                    return;
                continue;
            }
            if (state == State::pauseRequested) {
                if (state_.compare_exchange_weak(
                        state, State::controlActive, std::memory_order_acq_rel))
                    return;
                continue;
            }
            return;
        }
    }

    mutable std::mutex controlMutex_;
    mutable std::atomic<State> state_{State::idle};
};

} // namespace sms::audio
