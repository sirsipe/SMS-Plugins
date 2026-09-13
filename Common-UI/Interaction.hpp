#pragma once

namespace sms::ui {

inline constexpr int kNoInteractiveTargetType = -1;

/** Framework-neutral identity for one interactive item in a rendered view. */
struct InteractiveTarget {
    int type = kNoInteractiveTargetType;
    int index = -1;

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return type != kNoInteractiveTargetType;
    }

    [[nodiscard]] constexpr bool is(const int expectedType,
                                    const int expectedIndex = -1) const noexcept
    {
        return type == expectedType && (expectedIndex < 0 || index == expectedIndex);
    }

    friend constexpr bool operator==(const InteractiveTarget&, const InteractiveTarget&) = default;
};

inline constexpr InteractiveTarget kNoInteractiveTarget{};

/** Tracks a pointer target and reports only visible hover transitions. */
class HoverState {
public:
    [[nodiscard]] bool update(const InteractiveTarget target) noexcept
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

    [[nodiscard]] InteractiveTarget target() const noexcept { return target_; }

private:
    InteractiveTarget target_ = kNoInteractiveTarget;
};

} // namespace sms::ui
