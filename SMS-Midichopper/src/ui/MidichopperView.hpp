#pragma once

#include "DistrhoUI.hpp"
#include "Audio/WaveformSummary.hpp"
#include "Configuration.hpp"
#include "ContextMenu.hpp"
#include "DSP/SampleMixerSettings.hpp"
#include "DSP/SamplePlaybackSettings.hpp"
#include "Interaction.hpp"
#include "WaveformViewport.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace midichopper::ui {

struct ViewState {
    bool armed = false;
    float captureMode = 0.0f;
    float fixedLengthSeconds = 1.0f;
    float playbackMode = 0.0f;
    float monitorInput = 1.0f;
    int startPad = 0;
    float preRollMs = 0.0f;
    int baseMidiNote = static_cast<int>(kDefaultBaseMidiNote);
    int midiBankMode = static_cast<int>(kDefaultMidiBankMode);
    float outputGainDb = 0.0f;
    float globalPan = 0.0f;
    float globalTuneSemitones = 0.0f;
    int maxVoices = static_cast<int>(kPadsPerBank);
    int bank = 0;
    int layout = 0;
    int selectedPad = -1;
    int currentPad = -1;
    int pressedPad = -1;
    bool clearArmed = false;
    bool menuOpen = false;
    bool padContextMenuOpen = false;
    sms::ui::ContextMenuGeometry padContextMenu;
    std::span<const sms::ui::ContextMenuItemView> padContextMenuItems;
    sms::ui::InteractiveTarget hoveredTarget;
    sms::ui::InteractiveTarget mixerValueEntryTarget;
    const char* mixerValueEntryText = "";
    bool editorMode = false;
    bool chopEditorMode = false;
    bool chopSplitMode = false;
    bool playOnSelect = false;
    bool hasWaveform = false;
    std::array<float, 2> inputLevels{};
    std::array<float, 2> outputLevels{};
    std::span<const char> padState;
    std::span<const char> padActivity;
    const sms::dsp::SamplePlaybackSettings& editorSettings;
    const sms::dsp::SampleMixerSettings& mixerSettings;
    const sms::audio::WaveformSummary& waveform;
    float playbackPosition = 0.0f;
    std::span<const sms::audio::WaveformSummary> chopWaveforms;
    std::span<const std::int64_t> chopOffsets;
    int chopFirstPad = -1;
    int chopTargetPad = -1;
    float chopPreviewPosition = 0.0f;
    int chopPreviewPad = -1;
    int chopActiveBoundary = -1;
    bool chopReady = false;
    bool chopDirty = false;
    bool chopApplying = false;
    bool chopPreviousEnabled = false;
    bool chopNextEnabled = false;
    const char* status = "";
    sms::ui::waveform::Viewport waveformViewport;
    const sms::audio::WaveformSummary* waveformDetail = nullptr;
};

void draw(DGL_NAMESPACE::NanoVG& canvas, const ViewState& state);

} // namespace midichopper::ui
