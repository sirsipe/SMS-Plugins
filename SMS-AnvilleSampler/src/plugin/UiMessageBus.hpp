#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

namespace midichopper::plugin {

// Control/UI-thread messages only. No method on this bus may run in the audio callback.
class UiMessageBus {
public:
    static constexpr std::size_t kCapacity = 64U;
    static constexpr std::size_t kKeyCapacity = 48U;
    static constexpr std::size_t kValueCapacity = 8192U;

    struct Message {
        std::array<char, kKeyCapacity> key{};
        std::array<char, kValueCapacity> value{};
    };

    [[nodiscard]] bool publish(const char* const key, const char* const value)
    {
        if (key == nullptr || value == nullptr ||
            std::strlen(key) >= kKeyCapacity ||
            std::strlen(value) >= kValueCapacity)
            return false;

        const std::lock_guard lock(mutex_);
        Message& message = messages_[(nextSequence_ - 1U) % kCapacity];
        std::strcpy(message.key.data(), key);
        std::strcpy(message.value.data(), value);
        ++nextSequence_;
        return true;
    }

    [[nodiscard]] std::uint64_t cursor() const
    {
        const std::lock_guard lock(mutex_);
        return nextSequence_;
    }

    [[nodiscard]] bool read(std::uint64_t& cursor, Message& message,
                            bool& skipped) const
    {
        const std::lock_guard lock(mutex_);
        const std::uint64_t oldest = nextSequence_ > kCapacity
            ? nextSequence_ - kCapacity : 1U;
        skipped = cursor < oldest;
        if (skipped)
            cursor = oldest;
        if (cursor >= nextSequence_)
            return false;
        message = messages_[(cursor - 1U) % kCapacity];
        ++cursor;
        return true;
    }

private:
    mutable std::mutex mutex_;
    std::array<Message, kCapacity> messages_{};
    std::uint64_t nextSequence_ = 1U;
};

} // namespace midichopper::plugin
