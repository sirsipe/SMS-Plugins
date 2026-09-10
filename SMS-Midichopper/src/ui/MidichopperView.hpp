#pragma once

#include "DistrhoUI.hpp"
#include "Audio/WaveformSummary.hpp"
#include "Configuration.hpp"
#include "ContextMenu.hpp"
#include "DSP/SamplePlaybackSettings.hpp"

#include <array>
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
    int hoveredContextMenuItem = -1;
    bool editorMode = false;
    bool hasWaveform = false;
    std::array<float, 2> inputLevels{};
    std::array<float, 2> outputLevels{};
    std::span<const char> padState;
    std::span<const char> padActivity;
    const sms::dsp::SamplePlaybackSettings& editorSettings;
    const sms::audio::WaveformSummary& waveform;
    const char* status = "";
};

void draw(DGL_NAMESPACE::NanoVG& canvas, const ViewState& state);

} // namespace midichopper::ui
