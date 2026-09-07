#pragma once

#include "UI/Geometry.hpp"

#include <algorithm>

namespace sms::ui {

/** Supported bank layouts, ordered to match a zero-based host parameter. */
enum class PadArrangement : int {
    fourByFour = 0,
    threeByFour = 1,
    fourByTwo = 2,
};

class BankedPadLayout {
public:
    explicit constexpr BankedPadLayout(const int parameterValue) noexcept
        : arrangement_(static_cast<PadArrangement>(std::clamp(parameterValue, 0, 2))) {}

    [[nodiscard]] constexpr int visiblePadCount() const noexcept
    {
        switch (arrangement_) {
        case PadArrangement::threeByFour: return 12;
        case PadArrangement::fourByTwo: return 8;
        default: return 16;
        }
    }

    [[nodiscard]] constexpr int columns() const noexcept
    {
        return arrangement_ == PadArrangement::threeByFour ? 3 : 4;
    }

    [[nodiscard]] constexpr int rows() const noexcept
    {
        return visiblePadCount() / columns();
    }

    [[nodiscard]] constexpr int visualIndex(const int localPad) const noexcept
    {
        const int rowFromBottom = localPad / columns();
        return (rows() - rowFromBottom - 1) * columns() + localPad % columns();
    }

    [[nodiscard]] constexpr int localIndex(const int visualIndex) const noexcept
    {
        if (visualIndex < 0 || visualIndex >= visiblePadCount())
            return -1;
        const int rowFromTop = visualIndex / columns();
        return (rows() - rowFromTop - 1) * columns() + visualIndex % columns();
    }

    [[nodiscard]] constexpr PadGridLayout grid(const Rect bounds, const float gap) const noexcept
    {
        return {bounds, columns(), rows(), gap};
    }

private:
    PadArrangement arrangement_;
};

} // namespace sms::ui
