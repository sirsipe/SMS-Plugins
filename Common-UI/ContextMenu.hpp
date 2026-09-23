#pragma once

#include "UI/Geometry.hpp"

#include <algorithm>
#include <cstdint>
#include <span>

namespace sms::ui {

enum class ContextMenuItemKind : std::uint8_t {
    action,
    separator,
};

struct ContextMenuItemView {
    const char* label = "";
    bool enabled = true;
    bool dangerous = false;
    ContextMenuItemKind kind = ContextMenuItemKind::action;
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
        place(anchor, canvas, requestedWidth);
    }

    ContextMenuGeometry(const Point anchor,
                        const std::span<const ContextMenuItemKind> itemKinds,
                        const Rect canvas, const float requestedWidth = 172.0f,
                        const float itemHeight = 32.0f,
                        const float separatorHeight = 10.0f,
                        const float padding = 5.0f) noexcept
        : itemCount_(static_cast<int>(std::min(itemKinds.size(), kMaximumItems))),
          itemHeight_(std::max(itemHeight, 0.0f)),
          separatorHeight_(std::max(separatorHeight, 0.0f)),
          padding_(std::max(padding, 0.0f))
    {
        for (int index = 0; index < itemCount_; ++index) {
            if (itemKinds[static_cast<std::size_t>(index)] ==
                ContextMenuItemKind::separator) {
                separatorMask_ |= std::uint64_t{1} << static_cast<unsigned>(index);
            }
        }
        place(anchor, canvas, requestedWidth);
    }

    [[nodiscard]] Rect bounds() const noexcept { return bounds_; }

    [[nodiscard]] Rect item(const int index) const noexcept
    {
        if (index < 0 || index >= itemCount_)
            return {};
        float offset = padding_;
        for (int preceding = 0; preceding < index; ++preceding)
            offset += rowHeight(preceding);
        return {bounds_.x + padding_, bounds_.y + offset,
                std::max(bounds_.width - padding_ * 2.0f, 0.0f), rowHeight(index)};
    }

    [[nodiscard]] int hit(const Point point) const noexcept
    {
        for (int index = 0; index < itemCount_; ++index) {
            if (!isSeparator(index) && item(index).contains(point))
                return index;
        }
        return -1;
    }

private:
    static constexpr std::size_t kMaximumItems = 64U;

    [[nodiscard]] bool isSeparator(const int index) const noexcept
    {
        return index >= 0 && index < itemCount_ &&
               (separatorMask_ &
                (std::uint64_t{1} << static_cast<unsigned>(index))) != 0U;
    }

    [[nodiscard]] float rowHeight(const int index) const noexcept
    {
        return isSeparator(index) ? separatorHeight_ : itemHeight_;
    }

    void place(const Point anchor, const Rect canvas, const float requestedWidth) noexcept
    {
        const float width = std::min(std::max(requestedWidth, 0.0f),
                                     std::max(canvas.width, 0.0f));
        float contentHeight = padding_ * 2.0f;
        for (int index = 0; index < itemCount_; ++index)
            contentHeight += rowHeight(index);
        const float height = std::min(contentHeight, std::max(canvas.height, 0.0f));
        const float maximumX = canvas.x + std::max(canvas.width - width, 0.0f);
        const float maximumY = canvas.y + std::max(canvas.height - height, 0.0f);
        bounds_ = {
            std::clamp(anchor.x, canvas.x, maximumX),
            std::clamp(anchor.y, canvas.y, maximumY),
            width,
            height,
        };
    }

    Rect bounds_{};
    int itemCount_ = 0;
    float itemHeight_ = 0.0f;
    float separatorHeight_ = 0.0f;
    float padding_ = 0.0f;
    std::uint64_t separatorMask_ = 0U;
};

} // namespace sms::ui
