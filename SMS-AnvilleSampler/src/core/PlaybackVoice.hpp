#pragma once

#include "SampleStoragePool.hpp"
#include "DSP/AdsrEnvelope.hpp"
#include "DSP/ColorEffects.hpp"
#include "DSP/SampleMixerSettings.hpp"
#include "DSP/SamplePlaybackSettings.hpp"
#include "DSP/SincResampler.hpp"

#include <cstdint>

namespace midichopper {

/** Audio-owner DSP runtime for one pad. Storage and publication belong to the engine. */
class PlaybackVoice {
public:
    enum class Mode : std::uint8_t { OneShot, Hold, Loop };
    struct GlobalSettings {
        float pan = 0.0f;
        float tuneSemitones = 0.0f;
        float lowpass = 0.0f;
        float highpass = 0.0f;
        float filterSlope = 0.0f;
        float dirty = 0.0f;
        bool reverse = false;
    };
    struct Parameters {
        std::uint32_t frames = 0;
        double sourceSampleRate = 48000.0;
        double sampleRate = 48000.0;
        sms::dsp::SamplePlaybackSettings playback;
        sms::dsp::SampleMixerSettings mixer;
        GlobalSettings global;
    };

    /** The engine supplies non-empty source frames and sanitized settings. */
    void start(const Parameters& parameters, std::uint8_t velocity, Mode mode,
               std::int16_t triggerNote, std::uint64_t order) noexcept;
    /** Requires an active voice and non-empty source frames. False asks the
     * engine to hard-stop its pad and clear audition ownership. */
    [[nodiscard]] bool refresh(const Parameters& parameters) noexcept;
    /** True reports a stop that the engine must publish, including an idle envelope. */
    [[nodiscard]] bool release(const sms::dsp::SamplePlaybackSettings& playback) noexcept;
    void redirect(std::uint32_t frames, bool reverse) noexcept;
    /** Requires an active voice and its matching immutable storage/metadata.
     * Adds to the existing mix in pad order; returns whether the voice remains active. */
    [[nodiscard]] bool render(const SampleStoragePool& pool,
        const SampleStoragePool::Storage* storage, std::uint32_t frames,
        double sourceSampleRate, float& left, float& right) noexcept;

    /** Rate reconfiguration suspends playback without resetting its DSP/ownership. */
    void suspend() noexcept { playing_ = false; }
    /** Capture/clear/reset only reset activity, age, and the envelope. */
    void reset() noexcept;
    void hardStop() noexcept;

    [[nodiscard]] bool playing() const noexcept { return playing_; }
    [[nodiscard]] Mode mode() const noexcept { return mode_; }
    [[nodiscard]] std::int16_t triggerNote() const noexcept { return triggerNote_; }
    [[nodiscard]] bool reverse() const noexcept { return reverse_; }
    [[nodiscard]] std::uint64_t order() const noexcept { return order_; }
    [[nodiscard]] double position() const noexcept { return position_; }

private:
    bool playing_ = false;
    Mode mode_ = Mode::OneShot;
    std::int16_t triggerNote_ = -1; // MIDI ownership survives bank/mapping changes; -1 for auditions.
    bool reverse_ = false; // latched at trigger; local mixer edits affect the next voice
    std::uint64_t order_ = 0;
    double position_ = 0.0;
    double step_ = 1.0;
    std::uint32_t startFrame_ = 0;
    std::uint32_t endFrame_ = 0;
    bool releaseFromEnd_ = false;
    bool noteReleased_ = false;
    float velocityGain_ = 1.0f;
    float gainLeft_ = 1.0f;
    float gainRight_ = 1.0f;
    bool dirty_ = false;
    sms::dsp::SincResampler resampler_;
    sms::dsp::ColorFilters padFilters_;
    sms::dsp::ColorFilters globalFilters_;
    sms::dsp::AdsrEnvelope envelope_;
};

} // namespace midichopper
