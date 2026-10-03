#pragma once

#include "DSP/SampleMixerSettings.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace sms::state {

[[nodiscard]] inline std::string encodeSampleMixerSettings(
    const dsp::SampleMixerSettings& requested)
{
    const auto settings = dsp::sanitize(requested);
    char encoded[160]{};
    std::snprintf(encoded, sizeof(encoded), "MX2;%.9g;%.9g;%.9g;%.9g;%.9g;%.9g;%.9g",
                  settings.gainDecibels, settings.pan, settings.tuneSemitones,
                  settings.lowpass, settings.highpass, settings.filterSlope, settings.dirty);
    return encoded;
}

[[nodiscard]] inline bool decodeSampleMixerSettings(
    const char* const encoded, dsp::SampleMixerSettings& destination) noexcept
{
    if (encoded == nullptr)
        return false;
    const bool legacy = std::strncmp(encoded, "MX1;", 4U) == 0;
    if (!legacy && std::strncmp(encoded, "MX2;", 4U) != 0)
        return false;
    const char* cursor = encoded + 4U;
    float values[7]{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const std::size_t count = legacy ? 3U : 7U;
    for (std::size_t index = 0; index < count; ++index) {
        char* end = nullptr;
        values[index] = std::strtof(cursor, &end);
        if (end == cursor || !std::isfinite(values[index]))
            return false;
        cursor = end;
        if (index + 1U != count) {
            if (*cursor != ';')
                return false;
            ++cursor;
        }
    }
    if (*cursor != '\0')
        return false;
    destination = dsp::sanitize(dsp::SampleMixerSettings{
        values[0], values[1], values[2], values[3], values[4], values[5], values[6]});
    return true;
}

} // namespace sms::state
