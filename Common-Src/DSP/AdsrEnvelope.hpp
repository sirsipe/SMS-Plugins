#pragma once

#include "SamplePlaybackSettings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sms::dsp {

/** Allocation-free, sample-accurate linear ADSR envelope for a single voice. */
class AdsrEnvelope {
public:
    enum class Stage : std::uint8_t { Idle, Attack, Decay, Sustain, Release };

    void configure(const double sampleRate, const SamplePlaybackSettings& settings) noexcept
    {
        sampleRate_ = sampleRate > 1.0 ? sampleRate : 48000.0;
        settings_ = sanitize(settings);
    }

    void noteOn() noexcept
    {
        level_ = 0.0f;
        if (framesFor(settings_.attackSeconds) != 0U) {
            stage_ = Stage::Attack;
            remaining_ = framesFor(settings_.attackSeconds);
            increment_ = 1.0f / static_cast<float>(remaining_);
        } else {
            beginDecay();
        }
    }

    void noteOff() noexcept
    {
        if (stage_ == Stage::Idle || stage_ == Stage::Release)
            return;
        remaining_ = framesFor(settings_.releaseSeconds);
        if (remaining_ == 0U || level_ <= 0.0f) {
            reset();
            return;
        }
        stage_ = Stage::Release;
        increment_ = -level_ / static_cast<float>(remaining_);
    }

    [[nodiscard]] float next() noexcept
    {
        const float output = std::clamp(level_, 0.0f, 1.0f);
        switch (stage_) {
        case Stage::Attack:
            level_ += increment_;
            if (--remaining_ == 0U) {
                level_ = 1.0f;
                beginDecay();
            }
            break;
        case Stage::Decay:
            level_ += increment_;
            if (--remaining_ == 0U) {
                level_ = settings_.sustainLevel;
                stage_ = Stage::Sustain;
            }
            break;
        case Stage::Release:
            level_ += increment_;
            if (--remaining_ == 0U)
                reset();
            break;
        case Stage::Sustain:
        case Stage::Idle:
            break;
        }
        return output;
    }

    void reset() noexcept
    {
        stage_ = Stage::Idle;
        level_ = 0.0f;
        remaining_ = 0U;
        increment_ = 0.0f;
    }

    [[nodiscard]] bool active() const noexcept { return stage_ != Stage::Idle; }
    [[nodiscard]] bool releasing() const noexcept { return stage_ == Stage::Release; }
    [[nodiscard]] Stage stage() const noexcept { return stage_; }
    [[nodiscard]] float level() const noexcept { return level_; }
    [[nodiscard]] std::uint32_t releaseFrames() const noexcept
    {
        return framesFor(settings_.releaseSeconds);
    }

private:
    [[nodiscard]] std::uint32_t framesFor(const float seconds) const noexcept
    {
        return static_cast<std::uint32_t>(std::clamp(
            std::llround(static_cast<double>(seconds) * sampleRate_),
            0LL, static_cast<long long>(0xffffffffU)));
    }

    void beginDecay() noexcept
    {
        remaining_ = framesFor(settings_.decaySeconds);
        if (remaining_ == 0U) {
            level_ = settings_.sustainLevel;
            stage_ = Stage::Sustain;
            increment_ = 0.0f;
            return;
        }
        level_ = 1.0f;
        stage_ = Stage::Decay;
        increment_ = (settings_.sustainLevel - 1.0f) / static_cast<float>(remaining_);
    }

    double sampleRate_ = 48000.0;
    SamplePlaybackSettings settings_{};
    Stage stage_ = Stage::Idle;
    float level_ = 0.0f;
    float increment_ = 0.0f;
    std::uint32_t remaining_ = 0U;
};

} // namespace sms::dsp
