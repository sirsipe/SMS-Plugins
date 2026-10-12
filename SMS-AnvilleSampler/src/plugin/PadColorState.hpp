#pragma once

#include <charconv>
#include <string_view>

namespace midichopper::plugin {

// Zero is the uncolored pad. Out-of-palette and malformed saved values are
// displayed as uncolored by the UI, including when a theme has fewer colors.
inline int decodePadColorIndex(const std::string_view value,
                               const int paletteSize) noexcept
{
    int index = 0;
    const auto [end, error] = std::from_chars(
        value.data(), value.data() + value.size(), index);
    if (error != std::errc{} || end != value.data() + value.size() ||
        index < 0 || index > paletteSize)
        return 0;
    return index;
}

} // namespace midichopper::plugin
