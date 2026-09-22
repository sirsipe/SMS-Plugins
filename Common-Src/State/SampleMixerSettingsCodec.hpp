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
    char encoded[96]{};
    std::snprintf(encoded, sizeof(encoded), "MX1;%.9g;%.9g;%.9g",
                  settings.gainDecibels, settings.pan, settings.tuneSemitones);
    return encoded;
}

[[nodiscard]] inline bool decodeSampleMixerSettings(
    const char* const encoded, dsp::SampleMixerSettings& destination) noexcept
{
    if (encoded == nullptr || std::strncmp(encoded, "MX1;", 4U) != 0)
        return false;
    const char* cursor = encoded + 4U;
    float values[3]{};
    for (std::size_t index = 0; index < 3U; ++index) {
        char* end = nullptr;
        values[index] = std::strtof(cursor, &end);
        if (end == cursor || !std::isfinite(values[index]))
            return false;
        cursor = end;
        if (index != 2U) {
            if (*cursor != ';')
                return false;
            ++cursor;
        }
    }
    if (*cursor != '\0')
        return false;
    destination = dsp::sanitize(dsp::SampleMixerSettings{
        values[0], values[1], values[2]});
    return true;
}

} // namespace sms::state
