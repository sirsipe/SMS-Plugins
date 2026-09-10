#pragma once

namespace sms::ui {

inline constexpr int kNoInteractiveTarget = -1;

/** Tracks a pointer target and reports only visible hover transitions. */
class HoverState {
public:
    [[nodiscard]] bool update(const int target) noexcept
    {
        if (target_ == target)
            return false;
        target_ = target;
        return true;
    }

    [[nodiscard]] bool clear() noexcept
    {
        return update(kNoInteractiveTarget);
    }

    [[nodiscard]] int target() const noexcept { return target_; }

private:
    int target_ = kNoInteractiveTarget;
};

} // namespace sms::ui
