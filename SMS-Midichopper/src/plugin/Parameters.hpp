#pragma once

#include "Configuration.hpp"

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
    kParameterPadActivity1 = kParameterPadOccupied1 + kPadsPerBank,
    // New parameters must remain after the released pad output parameters.
    kParameterMaxVoices = kParameterPadActivity1 + kPadsPerBank,
    kParameterActiveBank,
    kParameterPadLayout,
    kParameterCurrentCapturePad,
    kParameterMidiBankMode,
    kParameterCount,
};

inline constexpr std::uint32_t kFirstPadStatusParameter = kParameterPadOccupied1;
inline constexpr std::uint32_t kFirstPadActivityParameter = kParameterPadActivity1;
inline constexpr std::uint32_t kParameterPadCount = kPadsPerBank;

struct ParameterRange {
    float defaultValue;
    float minimum;
    float maximum;
};

namespace parameterRanges {
inline constexpr ParameterRange toggle{0.0f, 0.0f, 1.0f};
inline constexpr ParameterRange startPad{1.0f, 1.0f, static_cast<float>(kPadsPerBank)};
inline constexpr ParameterRange preRollMs{0.0f, 0.0f, 100.0f};
inline constexpr ParameterRange fixedLengthSeconds{1.0f, 0.01f, 30.0f};
inline constexpr ParameterRange inputMonitor{1.0f, 0.0f, 1.0f};
inline constexpr ParameterRange baseMidiNote{
    static_cast<float>(kDefaultBaseMidiNote), 0.0f,
    static_cast<float>(128U - kPadsPerBank)};
inline constexpr ParameterRange outputGainDb{0.0f, -24.0f, 12.0f};
inline constexpr ParameterRange maxVoices{
    static_cast<float>(kPadsPerBank), 1.0f, static_cast<float>(kPadsPerBank)};
inline constexpr ParameterRange activeBank{1.0f, 1.0f, static_cast<float>(kBankCount)};
inline constexpr ParameterRange padLayout{
    0.0f, 0.0f, static_cast<float>(kPadLayoutCount - 1U)};
inline constexpr ParameterRange currentCapturePad{
    0.0f, 0.0f, static_cast<float>(kPadCount)};
inline constexpr ParameterRange midiBankMode{
    static_cast<float>(static_cast<std::uint8_t>(kDefaultMidiBankMode)), 0.0f, 1.0f};
} // namespace parameterRanges

[[nodiscard]] inline constexpr ParameterRange parameterRange(const std::uint32_t index) noexcept
{
    switch (index) {
    case kParameterStartPad: return parameterRanges::startPad;
    case kParameterPreRollMs: return parameterRanges::preRollMs;
    case kParameterFixedLengthSeconds: return parameterRanges::fixedLengthSeconds;
    case kParameterInputMonitor: return parameterRanges::inputMonitor;
    case kParameterBaseMidiNote: return parameterRanges::baseMidiNote;
    case kParameterOutputGainDb: return parameterRanges::outputGainDb;
    case kParameterMaxVoices: return parameterRanges::maxVoices;
    case kParameterActiveBank: return parameterRanges::activeBank;
    case kParameterPadLayout: return parameterRanges::padLayout;
    case kParameterCurrentCapturePad: return parameterRanges::currentCapturePad;
    case kParameterMidiBankMode: return parameterRanges::midiBankMode;
    default: return parameterRanges::toggle;
    }
}

} // namespace midichopper::plugin
