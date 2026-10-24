#pragma once

#include "Audio/WaveformSummary.hpp"
#include "Configuration.hpp"
#include "DSP/SampleMixerSettings.hpp"
#include "DSP/SamplePlaybackSettings.hpp"
#include "WaveformEditor.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace midichopper::ui {

/** Host-authoritative playhead with optional estimation for a known forward voice. */
class PlaybackIndicator {
public:
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;

    [[nodiscard]] float position() const noexcept { return fPosition; }
    [[nodiscard]] int pad() const noexcept { return fPad; }

    void acceptHostPosition(const float value, const Time now, const bool loop = false) noexcept
    {
        if (std::isfinite(value) && value > 0.0f) {
            const int pad = static_cast<int>(std::floor(value)) - 1;
            if (pad != fPad)
                fLoopStart = -1.0f;
            fLoop = loop;
            // DSP reports playback coordinates in either source orientation.
            // Mixer edits cannot tell us the orientation of an already sounding voice.
            fPredict = false;
            fAwaitingSettings = false;
            fPosition = value;
            fPad = static_cast<int>(std::floor(value)) - 1;
            fFraction = value - std::floor(value);
            fActive = true;
            fTick = now;
            fClearDeadline = {};
        } else {
            fPosition = 0.0f;
            fActive = false;
            fPad = -1;
            fClearDeadline = {};
        }
    }

    [[nodiscard]] bool start(const int pad, const int settingsPad,
                            const sms::dsp::SamplePlaybackSettings& settings,
                            const Time now, const bool loop = false,
                            const bool predictForward = true) noexcept
    {
        if (pad < 0 || pad >= static_cast<int>(kPadCount) ||
            (fActive && fPad == pad && now - fStarted < std::chrono::milliseconds(150)))
            return false;
        fLoop = loop;
        fPredict = predictForward;
        fPad = pad;
        fAwaitingSettings = settingsPad != pad;
        fFraction = fAwaitingSettings ? 0.0f : sms::dsp::sanitize(settings).start;
        fLoopStart = fAwaitingSettings ? -1.0f : fFraction;
        fActive = true;
        fTick = fStarted = now;
        fClearDeadline = {};
        if (fPredict)
            updatePosition();
        else
            fPosition = 0.0f;
        return true;
    }

    void stop(const bool holdFinal, const Time now) noexcept
    {
        fActive = false;
        fAwaitingSettings = false;
        if (holdFinal && fPad >= 0 && fPosition > 0.0f) {
            fFraction = std::min(fFraction, 0.985f);
            updatePosition();
            fClearDeadline = now + std::chrono::milliseconds(100);
        } else {
            fPad = -1;
            fPosition = 0.0f;
            fClearDeadline = {};
        }
    }

    void acceptSettings(const int pad, const sms::dsp::SamplePlaybackSettings& settings,
                        const Time now) noexcept
    {
        if (!fPredict || !fAwaitingSettings || fPad != pad)
            return;
        fFraction = sms::dsp::sanitize(settings).start;
        fLoopStart = fFraction;
        updatePosition();
        fAwaitingSettings = false;
        fTick = now;
    }

    /** Mirror the sounding cursor without starting a new voice or resetting timing. */
    void flipDirection(const int pad, const std::uint32_t frames,
                       const sms::dsp::SamplePlaybackSettings& settings,
                       const Time now) noexcept
    {
        if (!fActive || fPad != pad || fPosition <= 0.0f || frames == 0U)
            return;
        const auto playback = sms::dsp::sanitize(settings);
        const double frame = 1.0 / frames;
        const double start = playback.start;
        const double end = std::max(static_cast<double>(playback.end), start + frame);
        double mirrored = 1.0 - frame - static_cast<double>(fFraction);
        if (fLoop && (mirrored < start || mirrored >= end)) {
            const double length = end - start;
            mirrored = start + std::fmod(std::fmod(mirrored - start, length) + length, length);
        } else if (!fLoop) {
            mirrored = std::clamp(mirrored, start, std::max(start, end - frame));
        }
        fFraction = static_cast<float>(mirrored);
        fLoopStart = playback.start;
        fPredict = false;
        fAwaitingSettings = false;
        fTick = now;
        updatePosition();
    }

    /** Returns whether the displayed position needs a repaint. */
    [[nodiscard]] bool update(const Time now, const int selectedPad, const bool hasWaveform,
                              const sms::audio::WaveformSummary& waveform,
                              const sms::dsp::SamplePlaybackSettings& settings,
                              const sms::dsp::SampleMixerSettings& mixerSettings,
                              const float globalTune) noexcept
    {
        if (!fActive) {
            if (fClearDeadline.time_since_epoch().count() != 0 && now >= fClearDeadline) {
                stop(false, now);
                return true;
            }
            return false;
        }
        if (!fPredict || fAwaitingSettings || fPad != selectedPad || !hasWaveform ||
            waveform.frames == 0U || waveform.sampleRate <= 1.0) {
            fTick = now;
            return false;
        }
        const double elapsed = std::chrono::duration<double>(now - fTick).count();
        fTick = now;
        const auto playback = sms::dsp::sanitize(settings);
        const auto mixer = sms::dsp::sanitize(mixerSettings);
        const double tuneRatio = std::exp2(
            static_cast<double>(mixer.tuneSemitones +
                (mixer.globalMixer >= 0.5f ? globalTune : 0.0f)) / 12.0);
        if (fLoop) {
            // Like the voice, retain Start until retrigger while following live End edits.
            if (fLoopStart < 0.0f)
                fLoopStart = playback.start;
            const double end = std::max(static_cast<double>(playback.end),
                static_cast<double>(fLoopStart) + 1.0 / waveform.frames);
            const double length = end - fLoopStart;
            const double advanced = static_cast<double>(fFraction) - fLoopStart +
                elapsed * waveform.sampleRate * tuneRatio / static_cast<double>(waveform.frames);
            fFraction = static_cast<float>(fLoopStart +
                std::fmod(std::max(0.0, advanced), length));
        } else {
            fFraction = sms::ui::waveform::advancePlaybackFraction(
                fFraction, elapsed, waveform.frames, waveform.sampleRate,
                tuneRatio, playback.end);
        }
        updatePosition();
        if (!fLoop && fFraction >= playback.end - 1.0e-6f)
            stop(true, now);
        return true;
    }

private:
    void updatePosition() noexcept { fPosition = static_cast<float>(fPad + 1) + fFraction; }

    float fPosition = 0.0f;
    bool fActive = false;
    bool fPredict = true;
    bool fLoop = false;
    bool fAwaitingSettings = false;
    int fPad = -1;
    float fFraction = 0.0f;
    float fLoopStart = -1.0f;
    Time fTick{};
    Time fStarted{};
    Time fClearDeadline{};
};

} // namespace midichopper::ui
