#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace sms::dsp {

/**
 * Allocation-free stereo sample-peak follower for real-time audio metering.
 * Attack is immediate; a short hold and dB-linear release keep transient peaks
 * visible across slower host-to-UI update rates.
 */
class StereoPeakMeter {
public:
    static constexpr double holdSeconds = 0.1;
    static constexpr double releaseDbPerSecond = 24.0;
    static constexpr float minimumVisibleAmplitude = 0.001f;

    void process(const float* const left, const float* const right,
                 const std::uint32_t frames, const double sampleRate) noexcept
    {
        const std::array blockPeaks{blockPeak(left, frames), blockPeak(right, frames)};
        const double blockSeconds = sampleRate > 0.0 && std::isfinite(sampleRate)
            ? static_cast<double>(frames) / sampleRate : 0.0;

        for (std::size_t channel = 0; channel < levels_.size(); ++channel) {
            const float peak = blockPeaks[channel] > minimumVisibleAmplitude
                ? blockPeaks[channel] : 0.0f;
            if (peak >= levels_[channel]) {
                levels_[channel] = peak;
                holdRemaining_[channel] = holdSeconds;
                continue;
            }

            const double releaseSeconds = std::max(0.0,
                blockSeconds - holdRemaining_[channel]);
            holdRemaining_[channel] = std::max(0.0,
                holdRemaining_[channel] - blockSeconds);
            const float released = levels_[channel] * static_cast<float>(std::pow(
                10.0, -releaseDbPerSecond * releaseSeconds / 20.0));
            levels_[channel] = std::max(peak, released);
            if (levels_[channel] <= minimumVisibleAmplitude)
                levels_[channel] = 0.0f;
        }
    }

    [[nodiscard]] float left() const noexcept { return levels_[0]; }
    [[nodiscard]] float right() const noexcept { return levels_[1]; }

    void reset() noexcept
    {
        levels_.fill(0.0f);
        holdRemaining_.fill(0.0);
    }

private:
    [[nodiscard]] static float blockPeak(const float* const samples,
                                         const std::uint32_t frames) noexcept
    {
        float peak = 0.0f;
        if (samples == nullptr)
            return peak;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
            const float magnitude = std::abs(samples[frame]);
            if (std::isfinite(magnitude))
                peak = std::max(peak, std::min(magnitude, 1.0f));
            else if (std::isinf(magnitude))
                peak = 1.0f;
        }
        return peak;
    }

    std::array<float, 2> levels_{};
    std::array<double, 2> holdRemaining_{};
};

} // namespace sms::dsp
