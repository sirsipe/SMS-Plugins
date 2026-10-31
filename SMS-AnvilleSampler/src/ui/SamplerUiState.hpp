#pragma once

#include "Audio/WaveformSummary.hpp"
#include "Configuration.hpp"
#include "DSP/SampleMixerSettings.hpp"
#include "DSP/SamplePlaybackSettings.hpp"
#include "EditorAuditionMode.hpp"
#include "../plugin/Parameters.hpp"

#include <array>

namespace midichopper::ui
{

/** Host-reported and optimistic sampler state; interaction state lives elsewhere. */
struct SamplerUiState {
    bool armed = false;
    float captureMode = 0.0f;
    float fixedLength = plugin::parameterRanges::fixedLengthSeconds.defaultValue;
    float playbackMode = 0.0f;
    float monitor = plugin::parameterRanges::inputMonitor.defaultValue;
    int startPad = 0;
    float preRoll = 0.0f;
    int baseNote = static_cast<int>(plugin::parameterRanges::baseMidiNote.defaultValue);
    int midiBankMode = static_cast<int>(kDefaultMidiBankMode);

    float gain = 0.0f;
    float monitorGain = 0.0f;
    float globalPan = 0.0f;
    float globalTune = 0.0f;
    float globalLowpass = 0.0f;
    float globalHighpass = 0.0f;
    float globalFilterSlope = 0.0f;
    float globalDirty = 0.0f;
    bool globalReverse = false;
    int maxVoices = static_cast<int>(plugin::parameterRanges::maxVoices.defaultValue);

    int bank = 0;
    int layout = 0;
    int selectedPad = -1;
    int lastPlayedPad = -1;
    int currentPad = -1;
    bool editorMode = false;
    EditorAuditionMode editorAuditionMode = EditorAuditionMode::Play;
    bool anyPlaybackActive = false;

    bool hasWaveform = false;
    sms::dsp::SamplePlaybackSettings editorSettings{};
    int editorSettingsPad = -1;
    sms::dsp::SampleMixerSettings mixerSettings{};
    int mixerSettingsPad = -1;
    sms::audio::WaveformSummary waveform{};

    std::array<float, 2> inputLevels{};
    std::array<float, 2> outputLevels{};
    std::array<char, kPadsPerBank> padState{};
    std::array<char, kPadsPerBank> padStatus{};
    std::array<int, kPadCount> padColors{};
    char status[160]{};
};

} // namespace midichopper::ui
