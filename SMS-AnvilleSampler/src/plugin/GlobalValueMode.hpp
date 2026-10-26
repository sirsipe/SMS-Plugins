#pragma once

#include "Parameters.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace midichopper::plugin {

enum class GlobalValueMode { normal = 0, quantized = 1, zeroSnap = 2 };

struct GlobalValueSettings {
    GlobalValueMode mode = GlobalValueMode::normal;
    float minimum = 0.0f;
    float maximum = 1.0f;
};

struct GlobalValueControl {
    std::uint32_t parameter;
    float step;
};

// Quantized intervals match Ctrl+wheel; raw host values remain untouched.
inline constexpr std::array kGlobalValueControls{
    GlobalValueControl{kParameterOutputGainDb, 1.0f},
    GlobalValueControl{kParameterGlobalPan, 0.1f},
    GlobalValueControl{kParameterGlobalTuneSemitones, 1.0f},
    GlobalValueControl{kParameterGlobalLowpass, 0.1f},
    GlobalValueControl{kParameterGlobalHighpass, 0.1f},
};

[[nodiscard]] inline constexpr std::size_t globalControlIndex(const std::uint32_t parameter) noexcept
{
    for (std::size_t i = 0; i < kGlobalValueControls.size(); ++i)
        if (kGlobalValueControls[i].parameter == parameter)
            return i;
    return kGlobalValueControls.size();
}

[[nodiscard]] inline constexpr float globalValueStep(const std::uint32_t parameter) noexcept
{
    const auto index = globalControlIndex(parameter);
    return index < kGlobalValueControls.size() ? kGlobalValueControls[index].step : 0.0f;
}

[[nodiscard]] inline constexpr GlobalValueSettings defaultGlobalValueSettings(
    const std::uint32_t parameter) noexcept
{
    const auto range = parameterRange(parameter);
    return {GlobalValueMode::normal, range.minimum, range.maximum};
}

[[nodiscard]] inline constexpr auto defaultGlobalValueSettings() noexcept
{
    std::array<GlobalValueSettings, kGlobalValueControls.size()> settings{};
    for (std::size_t i = 0; i < settings.size(); ++i)
        settings[i] = defaultGlobalValueSettings(kGlobalValueControls[i].parameter);
    return settings;
}

// Compare representable grid values rather than accepting an out-of-range point
// through an epsilon (which could validate a very narrow range with no grid).
[[nodiscard]] inline double globalGridMinimum(const GlobalValueSettings& settings, float step) noexcept
{
    double grid = std::ceil(static_cast<double>(settings.minimum) / step);
    if (static_cast<float>((grid - 1.0) * step) >= settings.minimum)
        grid -= 1.0;
    return grid;
}
[[nodiscard]] inline double globalGridMaximum(const GlobalValueSettings& settings, float step) noexcept
{
    double grid = std::floor(static_cast<double>(settings.maximum) / step);
    if (static_cast<float>((grid + 1.0) * step) <= settings.maximum)
        grid += 1.0;
    return grid;
}

[[nodiscard]] inline bool validGlobalValueSettings(const std::uint32_t parameter,
                                                  const GlobalValueSettings& settings) noexcept
{
    if (globalControlIndex(parameter) == kGlobalValueControls.size())
        return false;
    const auto range = parameterRange(parameter);
    if (!std::isfinite(settings.minimum) || !std::isfinite(settings.maximum) ||
        settings.minimum < range.minimum || settings.maximum > range.maximum ||
        settings.minimum >= settings.maximum)
        return false;
    switch (settings.mode) {
    case GlobalValueMode::normal: return true;
    case GlobalValueMode::zeroSnap: return settings.minimum <= 0.0f && settings.maximum >= 0.0f;
    case GlobalValueMode::quantized:
        return globalGridMinimum(settings, globalValueStep(parameter)) <=
               globalGridMaximum(settings, globalValueStep(parameter));
    }
    return false;
}

[[nodiscard]] inline float mappedGlobalValue(const std::uint32_t parameter, float raw,
                                            const GlobalValueSettings& requested) noexcept
{
    const auto range = parameterRange(parameter);
    const auto settings = validGlobalValueSettings(parameter, requested)
        ? requested : defaultGlobalValueSettings(parameter);
    raw = std::clamp(std::isfinite(raw) ? raw : range.defaultValue, range.minimum, range.maximum);
    if (settings.minimum == range.minimum && settings.maximum == range.maximum)
        return raw; // Preserve full-range Normal values exactly.
    return static_cast<float>(settings.minimum +
        (static_cast<double>(raw) - range.minimum) / (range.maximum - range.minimum) *
        (settings.maximum - settings.minimum));
}

[[nodiscard]] inline float effectiveGlobalValue(const std::uint32_t parameter, const float raw,
                                               const GlobalValueSettings& requested) noexcept
{
    const auto settings = validGlobalValueSettings(parameter, requested)
        ? requested : defaultGlobalValueSettings(parameter);
    const float value = mappedGlobalValue(parameter, raw, settings);
    if (settings.mode == GlobalValueMode::quantized) {
        const float step = globalValueStep(parameter);
        const double grid = std::clamp(std::round(static_cast<double>(value) / step),
            globalGridMinimum(settings, step), globalGridMaximum(settings, step));
        const float result = std::clamp(static_cast<float>(grid * step), settings.minimum, settings.maximum);
        return result == 0.0f ? 0.0f : result;
    }
    if (settings.mode != GlobalValueMode::zeroSnap)
        return value;
    const float width = (settings.maximum - settings.minimum) * 0.05f;
    const float left = std::min(width, -settings.minimum * 0.5f);
    const float right = std::min(width, settings.maximum * 0.5f);
    if (value < -left)
        return settings.minimum * (value + left) / (settings.minimum + left);
    if (value > right)
        return settings.maximum * (value - right) / (settings.maximum - right);
    return 0.0f;
}

/** Invert range and nonzero snap branches. Quantized entry chooses its pre-grid value. */
[[nodiscard]] inline float rawGlobalValue(const std::uint32_t parameter, float displayed,
                                         const GlobalValueSettings& requested) noexcept
{
    const auto range = parameterRange(parameter);
    const auto settings = validGlobalValueSettings(parameter, requested)
        ? requested : defaultGlobalValueSettings(parameter);
    displayed = std::clamp(std::isfinite(displayed) ? displayed : range.defaultValue,
                           settings.minimum, settings.maximum);
    if (settings.mode == GlobalValueMode::zeroSnap) {
        const float width = (settings.maximum - settings.minimum) * 0.05f;
        if (displayed < 0.0f) {
            const float left = std::min(width, -settings.minimum * 0.5f);
            displayed = displayed * (settings.minimum + left) / settings.minimum - left;
        } else if (displayed > 0.0f) {
            const float right = std::min(width, settings.maximum * 0.5f);
            displayed = displayed * (settings.maximum - right) / settings.maximum + right;
        }
    }
    if (settings.minimum == range.minimum && settings.maximum == range.maximum)
        return displayed;
    return std::clamp(static_cast<float>(range.minimum +
        (static_cast<double>(displayed) - settings.minimum) / (settings.maximum - settings.minimum) *
        (range.maximum - range.minimum)), range.minimum, range.maximum);
}

[[nodiscard]] inline std::string encodeGlobalValueSettings(const GlobalValueSettings& settings)
{
    std::string result = "KC1;" + std::to_string(static_cast<int>(settings.mode));
    for (const float value : {settings.minimum, settings.maximum}) {
        char buffer[64];
        const auto encoded = std::to_chars(buffer, buffer + sizeof(buffer), value,
            std::chars_format::general, std::numeric_limits<float>::max_digits10);
        result += ';';
        result.append(buffer, encoded.ptr);
    }
    return result;
}

/** Strict, transactional durable-state decoding. */
[[nodiscard]] inline bool decodeGlobalValueSettings(const std::uint32_t parameter,
    const std::string_view encoded, GlobalValueSettings& destination) noexcept
{
    if (encoded.size() < 7U || encoded.substr(0, 4) != "KC1;" ||
        encoded[4] < '0' || encoded[4] > '2' || encoded[5] != ';')
        return false;
    const auto separator = encoded.find(';', 6U);
    if (separator == std::string_view::npos)
        return false;
    GlobalValueSettings parsed{static_cast<GlobalValueMode>(encoded[4] - '0'), 0.0f, 0.0f};
    const auto parse = [](const std::string_view text, float& value) {
        const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    };
    if (!parse(encoded.substr(6U, separator - 6U), parsed.minimum) ||
        !parse(encoded.substr(separator + 1U), parsed.maximum) ||
        !validGlobalValueSettings(parameter, parsed))
        return false;
    destination = parsed;
    return true;
}

} // namespace midichopper::plugin
