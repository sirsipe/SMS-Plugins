#pragma once

#include "UI/Geometry.hpp"

#include <algorithm>

namespace sms::ui {

struct ContextMenuItemView {
    const char* label = "";
    bool enabled = true;
    bool dangerous = false;
};

/** Framework-neutral placement and hit testing for a single-column menu. */
class ContextMenuGeometry {
public:
    ContextMenuGeometry() = default;

    ContextMenuGeometry(const Point anchor, const int itemCount,
                        const Rect canvas, const float requestedWidth = 172.0f,
                        const float itemHeight = 32.0f,
                        const float padding = 5.0f) noexcept
        : itemCount_(std::max(itemCount, 0)), itemHeight_(std::max(itemHeight, 0.0f)),
          padding_(std::max(padding, 0.0f))
    {
        const float width = std::min(std::max(requestedWidth, 0.0f),
                                     std::max(canvas.width, 0.0f));
        const float height = std::min(
            padding_ * 2.0f + itemHeight_ * static_cast<float>(itemCount_),
            std::max(canvas.height, 0.0f));
        const float maximumX = canvas.x + std::max(canvas.width - width, 0.0f);
        const float maximumY = canvas.y + std::max(canvas.height - height, 0.0f);
        bounds_ = {
            std::clamp(anchor.x, canvas.x, maximumX),
            std::clamp(anchor.y, canvas.y, maximumY),
            width,
            height,
        };
    }

    [[nodiscard]] Rect bounds() const noexcept { return bounds_; }

    [[nodiscard]] Rect item(const int index) const noexcept
    {
        if (index < 0 || index >= itemCount_)
            return {};
        return {bounds_.x + padding_, bounds_.y + padding_ +
                    static_cast<float>(index) * itemHeight_,
                std::max(bounds_.width - padding_ * 2.0f, 0.0f), itemHeight_};
    }

    [[nodiscard]] int hit(const Point point) const noexcept
    {
        for (int index = 0; index < itemCount_; ++index) {
            if (item(index).contains(point))
                return index;
        }
        return -1;
    }

private:
    Rect bounds_{};
    int itemCount_ = 0;
    float itemHeight_ = 0.0f;
    float padding_ = 0.0f;
};

} // namespace sms::ui
