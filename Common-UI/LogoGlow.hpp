#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <span>
#include <vector>

namespace sms::ui {

inline constexpr int logoWidth = 170;
inline constexpr int logoHeight = 78;
inline constexpr int logoGlowPadding = 24;
inline constexpr int logoGlowWidth = logoWidth + 2 * logoGlowPadding;
inline constexpr int logoGlowHeight = logoHeight + 2 * logoGlowPadding;

// Always keep at least 65% of the configured brightness. A zero period is steady.
inline float logoGlowOpacity(float alpha, float periodSeconds, double elapsedSeconds) noexcept
{
    if (periodSeconds <= 0.0f)
        return alpha;
    const double phase = std::fmod(std::max(0.0, elapsedSeconds), periodSeconds) / periodSeconds;
    return alpha * static_cast<float>(0.825 + 0.175 * std::cos(2.0 * std::numbers::pi * phase));
}

// UI-only startup work: stretch the actual logo alpha to its display rectangle,
// then make a padded white Gaussian mask. RGB is irrelevant to the silhouette.
inline std::vector<std::uint8_t> makeLogoGlow(std::span<const std::uint8_t> rgba,
                                            int width, int height, float radius)
{
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        !std::isfinite(radius) || radius <= 0.0f || radius > logoGlowPadding ||
        rgba.size() != static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4)
        return {};
    constexpr int count = logoGlowWidth * logoGlowHeight;
    std::vector<float> mask(count, 0.0f), horizontal(count, 0.0f);
    for (int y = 0; y < logoHeight; ++y) {
        const float sy = std::clamp((y + 0.5f) * height / logoHeight - 0.5f, 0.0f,
                                    static_cast<float>(height - 1));
        const int y0 = static_cast<int>(sy), y1 = std::min(y0 + 1, height - 1);
        for (int x = 0; x < logoWidth; ++x) {
            const float sx = std::clamp((x + 0.5f) * width / logoWidth - 0.5f, 0.0f,
                                        static_cast<float>(width - 1));
            const int x0 = static_cast<int>(sx), x1 = std::min(x0 + 1, width - 1);
            const auto alpha = [&](int px, int py) {
                return static_cast<float>(rgba[(static_cast<std::size_t>(py) * width + px) * 4 + 3]);
            };
            const float top = std::lerp(alpha(x0, y0), alpha(x1, y0), sx - x0);
            const float bottom = std::lerp(alpha(x0, y1), alpha(x1, y1), sx - x0);
            mask[(y + logoGlowPadding) * logoGlowWidth + x + logoGlowPadding] =
                std::lerp(top, bottom, sy - y0);
        }
    }
    const int extent = static_cast<int>(std::ceil(radius));
    const float sigma = std::max(0.5f, radius / 3.0f);
    std::vector<float> kernel(2 * extent + 1);
    float sum = 0.0f;
    for (int offset = -extent; offset <= extent; ++offset) {
        const float weight = std::exp(-static_cast<float>(offset * offset) / (2.0f * sigma * sigma));
        kernel[offset + extent] = weight;
        sum += weight;
    }
    for (auto& weight : kernel)
        weight /= sum;
    for (int y = 0; y < logoGlowHeight; ++y)
        for (int x = 0; x < logoGlowWidth; ++x)
            for (int offset = -extent; offset <= extent; ++offset)
                if (x + offset >= 0 && x + offset < logoGlowWidth)
                    horizontal[y * logoGlowWidth + x] +=
                        mask[y * logoGlowWidth + x + offset] * kernel[offset + extent];
    std::vector<std::uint8_t> result(count * 4, 255);
    for (int y = 0; y < logoGlowHeight; ++y)
        for (int x = 0; x < logoGlowWidth; ++x) {
            float value = 0.0f;
            for (int offset = -extent; offset <= extent; ++offset)
                if (y + offset >= 0 && y + offset < logoGlowHeight)
                    value += horizontal[(y + offset) * logoGlowWidth + x] * kernel[offset + extent];
            result[(y * logoGlowWidth + x) * 4 + 3] =
                static_cast<std::uint8_t>(std::clamp(std::lround(value * 2.0f), 0L, 255L));
        }
    return result;
}

} // namespace sms::ui
