#pragma once

#include <algorithm>

namespace sms::ui {

struct Point { float x = 0.0f; float y = 0.0f; };

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    [[nodiscard]] constexpr bool contains(const Point point) const noexcept
    {
        return point.x >= x && point.x < x + width &&
               point.y >= y && point.y < y + height;
    }
};

struct PadGridLayout {
    Rect bounds{};
    int columns = 4;
    int rows = 4;
    float gap = 0.0f;

    [[nodiscard]] Rect cell(const int index) const noexcept
    {
        if (columns <= 0 || rows <= 0 || index < 0 || index >= columns * rows)
            return {};
        const float cellWidth = (bounds.width - gap * static_cast<float>(columns - 1)) /
                                static_cast<float>(columns);
        const float cellHeight = (bounds.height - gap * static_cast<float>(rows - 1)) /
                                 static_cast<float>(rows);
        const int column = index % columns;
        const int row = index / columns;
        return {bounds.x + column * (cellWidth + gap),
                bounds.y + row * (cellHeight + gap), cellWidth, cellHeight};
    }

    [[nodiscard]] int hit(const Point point) const noexcept
    {
        if (!bounds.contains(point))
            return -1;
        for (int index = 0; index < columns * rows; ++index) {
            if (cell(index).contains(point))
                return index;
        }
        return -1;
    }
};

} // namespace sms::ui
