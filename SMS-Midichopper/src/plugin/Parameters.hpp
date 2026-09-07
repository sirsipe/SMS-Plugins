#pragma once

#include <cstdint>

namespace midichopper::plugin {

/** Stable host-automation parameter order. Do not reorder released entries. */
enum Parameter : std::uint32_t {
    kParameterMode = 0,
    kParameterStartPad,
    kParameterPreRollMs,
    kParameterCaptureMode,
    kParameterFixedLengthSeconds,
    kParameterPlaybackMode,
    kParameterInputMonitor,
    kParameterBaseMidiNote,
    kParameterOutputGainDb,
    kParameterFinalize,
    kParameterUndo,
    kParameterClearAll,
    kParameterPadOccupied1,
    kParameterPadActivity1 = kParameterPadOccupied1 + 16,
    // New parameters must remain after the released pad output parameters.
    kParameterMaxVoices = kParameterPadActivity1 + 16,
    kParameterActiveBank,
    kParameterPadLayout,
    kParameterCurrentCapturePad,
    kParameterCount,
};

inline constexpr std::uint32_t kFirstPadStatusParameter = kParameterPadOccupied1;
inline constexpr std::uint32_t kFirstPadActivityParameter = kParameterPadActivity1;
inline constexpr std::uint32_t kParameterPadCount = 16;

} // namespace midichopper::plugin
