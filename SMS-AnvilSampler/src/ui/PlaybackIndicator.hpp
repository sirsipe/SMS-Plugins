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

/** UI-local playhead estimation between host position notifications. */
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
                            const Time now, const bool loop = false) noexcept
    {
        if (pad < 0 || pad >= static_cast<int>(kPadCount) ||
            (fActive && fPad == pad && now - fStarted < std::chrono::milliseconds(150)))
            return false;
        fLoop = loop;
        fPad = pad;
        fAwaitingSettings = settingsPad != pad;
        fFraction = fAwaitingSettings ? 0.0f : sms::dsp::sanitize(settings).start;
        fLoopStart = fAwaitingSettings ? -1.0f : fFraction;
        fActive = true;
        fTick = fStarted = now;
        fClearDeadline = {};
        updatePosition();
        return true;
    }

    void stop(const bool holdFinal, const Time now) noexcept
    {
        fActive = false;
        fAwaitingSettings = false;
        if (holdFinal && fPad >= 0) {
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
        if (!fAwaitingSettings || fPad != pad)
            return;
        fFraction = sms::dsp::sanitize(settings).start;
        fLoopStart = fFraction;
        updatePosition();
        fAwaitingSettings = false;
        fTick = now;
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
        if (fAwaitingSettings || fPad != selectedPad || !hasWaveform ||
            waveform.frames == 0U || waveform.sampleRate <= 1.0) {
            fTick = now;
            return false;
        }
        const double elapsed = std::chrono::duration<double>(now - fTick).count();
        fTick = now;
        const auto playback = sms::dsp::sanitize(settings);
        const auto mixer = sms::dsp::sanitize(mixerSettings);
        const double tuneRatio = std::exp2(
            static_cast<double>(mixer.tuneSemitones + globalTune) / 12.0);
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
