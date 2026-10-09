#pragma once

#include "Configuration.hpp"

#include <array>
#include <cstdio>
#include <string>

namespace midichopper::plugin {

inline std::array<std::string, midichopper::kPadCount> makePadStateKeys(const char* const prefix)
{
    std::array<std::string, midichopper::kPadCount> keys;
    for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
        char key[24];
        std::snprintf(key, sizeof(key), "%s%02u", prefix, pad + 1U);
        keys[pad] = key;
    }
    return keys;
}

inline const auto kPadStateKeys = makePadStateKeys("pad_");
inline const auto kPadEditStateKeys = makePadStateKeys("pad_edit_");
inline const auto kPadMixerStateKeys = makePadStateKeys("pad_mix_");
inline const auto kPadColorStateKeys = makePadStateKeys("pad_color_");

inline constexpr const char* kWaveformRequestKey = "waveform_request";
inline constexpr const char* kWaveformDataKey = "waveform_data";
inline constexpr const char* kWaveformDetailRequestKey = "waveform_detail_request";
inline constexpr const char* kWaveformDetailDataKey = "waveform_detail_data";
inline constexpr const char* kPadClearRequestKey = "pad_clear_request";
inline constexpr const char* kPadFileRequestKey = "pad_file_request";
inline constexpr const char* kPadFileBusyKey = "pad_file_busy";
inline constexpr const char* kPadFileStatusKey = "pad_file_status";
inline constexpr const char* kPadClipboardRequestKey = "pad_clipboard_request";
inline constexpr const char* kChopApplyRequestKey = "chop_apply_request";
inline constexpr const char* kChopStatusKey = "chop_status";
inline constexpr const char* kChopSnapshotRequestKey = "chop_snapshot_request";
inline constexpr const char* kChopSnapshotDataKey = "chop_snapshot_data";
inline constexpr const char* kChopPreviewRequestKey = "chop_preview_request";
inline constexpr const char* kChopMidiPreviewKey = "chop_midi_preview";
inline constexpr const char* kPadStructureRequestKey = "pad_structure_request";
inline constexpr const char* kPadStructureStatusKey = "pad_structure_status";
inline constexpr const char* kPlayStopRequestKey = "play_stop_request";

} // namespace midichopper::plugin
