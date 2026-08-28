#pragma once

#include "DSP/SamplePlaybackSettings.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace sms::state {

[[nodiscard]] inline std::string encodeSamplePlaybackSettings(
    const dsp::SamplePlaybackSettings& requested)
{
    const auto settings = dsp::sanitize(requested);
    char encoded[192]{};
    std::snprintf(encoded, sizeof(encoded), "SP1;%.9g;%.9g;%.9g;%.9g;%.9g;%.9g",
                  settings.start, settings.end, settings.attackSeconds,
                  settings.decaySeconds, settings.sustainLevel, settings.releaseSeconds);
    return encoded;
}

[[nodiscard]] inline bool decodeSamplePlaybackSettings(
    const char* const encoded, dsp::SamplePlaybackSettings& destination) noexcept
{
    if (encoded == nullptr || std::strncmp(encoded, "SP1;", 4U) != 0)
        return false;
    const char* cursor = encoded + 4U;
    float values[6]{};
    for (std::size_t index = 0; index < 6U; ++index) {
        char* end = nullptr;
        values[index] = std::strtof(cursor, &end);
        if (end == cursor || !std::isfinite(values[index]))
            return false;
        cursor = end;
        if (index != 5U) {
            if (*cursor != ';')
                return false;
            ++cursor;
        }
    }
    if (*cursor != '\0')
        return false;
    destination = dsp::sanitize({values[0], values[1], values[2],
                                 values[3], values[4], values[5]});
    return true;
}

} // namespace sms::state
