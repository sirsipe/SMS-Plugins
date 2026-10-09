#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <type_traits>

namespace sms::audio {

/** Latest-value control-to-audio handoff with three owned slots.
 * publish() serializes control writers; tryConsume() has one audio reader.
 * Unread values may be superseded. Audio never locks, allocates, waits or
 * retries: it exchanges a slot and copies a complete bounded value.
 * Do not reset/destroy the mailbox while either side is using it.
 */
template<class Value>
class RealtimeLatestMailbox {
    static_assert(std::is_trivially_copyable_v<Value> &&
                  std::is_nothrow_copy_assignable_v<Value>);
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
public:
    void publish(const Value& value)
    {
        const std::lock_guard lock(writerMutex_);
        slots_[writeSlot_] = value;
        // Release publishes completed data; acquire takes ownership of the
        // previous middle slot after the reader has stopped using it.
        writeSlot_ = middle_.exchange(writeSlot_ | kUnread, std::memory_order_acq_rel) & kSlotMask;
    }

    [[nodiscard]] bool tryConsume(Value& value) noexcept
    {
        if ((middle_.load(std::memory_order_acquire) & kUnread) == 0U)
            return false;
        // Only this reader clears kUnread, so the exchanged value is unread
        // even if another publication arrives after the preceding load.
        readSlot_ = middle_.exchange(readSlot_, std::memory_order_acq_rel) & kSlotMask;
        value = slots_[readSlot_];
        return true;
    }

private:
    static constexpr std::uint32_t kUnread = 4U;
    static constexpr std::uint32_t kSlotMask = 3U;
    std::array<Value, 3> slots_{};
    std::atomic<std::uint32_t> middle_{1U};
    std::uint32_t readSlot_ = 0U;  // audio-owned
    std::uint32_t writeSlot_ = 2U; // serialized control writers
    std::mutex writerMutex_;
};

} // namespace sms::audio
