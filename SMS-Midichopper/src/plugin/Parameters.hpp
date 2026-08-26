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
    kParameterCount = kParameterPadActivity1 + 16,
};

inline constexpr std::uint32_t kFirstPadStatusParameter = kParameterPadOccupied1;
inline constexpr std::uint32_t kFirstPadActivityParameter = kParameterPadActivity1;
inline constexpr std::uint32_t kParameterPadCount = 16;

} // namespace midichopper::plugin
