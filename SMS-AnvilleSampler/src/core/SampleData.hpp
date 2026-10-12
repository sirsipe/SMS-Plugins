#pragma once

#include <cstdint>
#include <vector>

namespace midichopper {

/** A non-real-time copy of one pad, suitable for project state and UI work. */
struct PadData {
    double sampleRate = 48000.0;
    std::uint32_t frames = 0;
    float peak = 0.0f;
    float rms = 0.0f;
    std::vector<float> stereo;
    std::uint64_t generation = 0; // transient snapshot revision; not serialized
};

} // namespace midichopper
