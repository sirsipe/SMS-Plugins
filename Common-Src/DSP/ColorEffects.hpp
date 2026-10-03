#pragma once

#include "SampleMixerSettings.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace sms::dsp {

inline constexpr double kDirtySampleRate = 26040.0;

/** Source frame held by the 26.04 kHz dirty clock, before varispeed playback. */
[[nodiscard]] inline std::uint32_t dirtySourceFrame(
    const std::uint32_t frame, const double sourceRate) noexcept
{
    if (sourceRate <= 1.0 || sourceRate <= kDirtySampleRate)
        return frame;
    const double heldTime = std::floor(static_cast<double>(frame) *
        kDirtySampleRate / sourceRate) / kDirtySampleRate;
    return std::min(frame, static_cast<std::uint32_t>(
        std::floor(heldTime * sourceRate + 1e-8)));
}

[[nodiscard]] inline float dirtyMono(const float left, const float right) noexcept
{
    const float mono = std::clamp((left + right) * 0.5f, -1.0f, 1.0f);
    return std::round(mono * 2047.0f) / 2047.0f;
}

/** Per-voice stereo lowpass/highpass, with 6/12/24 dB slopes. */
class ColorFilters {
public:
    void reset() noexcept
    {
        low_ = {};
        high_ = {};
    }

    void configure(const SampleMixerSettings& requested, const double sampleRate) noexcept
    {
        const auto settings = sanitize(requested);
        low_.configure(settings.lowpass, settings.filterSlope, sampleRate, false);
        high_.configure(settings.highpass, settings.filterSlope, sampleRate, true);
    }

    void process(float& left, float& right) noexcept
    {
        low_.process(left, right);
        high_.process(left, right);
    }

private:
    struct Biquad {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
        double z1[2]{}, z2[2]{};

        float process(const float input, const unsigned channel) noexcept
        {
            const double out = b0 * input + z1[channel];
            z1[channel] = b1 * input - a1 * out + z2[channel];
            z2[channel] = b2 * input - a2 * out;
            if (std::abs(z1[channel]) < 1e-20) z1[channel] = 0.0;
            if (std::abs(z2[channel]) < 1e-20) z2[channel] = 0.0;
            return static_cast<float>(out);
        }
    };

    struct Filter {
        float amount = -1.0f;
        float slope = -1.0f;
        double rate = 0.0;
        double onePoleA = 0.0;
        double onePoleState[2]{};
        Biquad first{}, second{};
        bool highpass = false;

        void configure(const float newAmount, const float newSlope,
                       const double sampleRate, const bool isHighpass) noexcept
        {
            if (amount == newAmount && slope == newSlope && rate == sampleRate)
                return;
            if (newAmount <= 0.0f || newSlope != slope || sampleRate != rate) {
                onePoleState[0] = onePoleState[1] = 0.0;
                first = {};
                second = {};
            }
            amount = newAmount;
            slope = newSlope;
            rate = sampleRate;
            highpass = isHighpass;
            if (amount <= 0.0f || rate <= 1.0)
                return;

            const double cutoff = highpass
                ? 20.0 * std::pow(1000.0, amount)
                : 20000.0 * std::pow(0.001, amount);
            const double hz = std::clamp(cutoff, 10.0, rate * 0.45);
            onePoleA = std::exp(-2.0 * 3.14159265358979323846 * hz / rate);
            // Resonance rises with filter amount; 6 dB mode has no Q.
            const double q = 0.7071067811865476 + 0.55 * amount;
            const double omega = 2.0 * 3.14159265358979323846 * hz / rate;
            const double cosine = std::cos(omega);
            const double alpha = std::sin(omega) / (2.0 * q);
            const double norm = 1.0 / (1.0 + alpha);
            const double b0 = highpass ? (1.0 + cosine) * 0.5 : (1.0 - cosine) * 0.5;
            const double b1 = highpass ? -(1.0 + cosine) : 1.0 - cosine;
            first.b0 = second.b0 = b0 * norm;
            first.b1 = second.b1 = b1 * norm;
            first.b2 = second.b2 = b0 * norm;
            first.a1 = second.a1 = -2.0 * cosine * norm;
            first.a2 = second.a2 = (1.0 - alpha) * norm;
        }

        void process(float& left, float& right) noexcept
        {
            if (amount <= 0.0f || rate <= 1.0)
                return;
            float* channels[2]{&left, &right};
            for (unsigned channel = 0; channel < 2; ++channel) {
                float value = *channels[channel];
                if (slope < 0.5f) {
                    onePoleState[channel] = (1.0 - onePoleA) * value +
                        onePoleA * onePoleState[channel];
                    if (std::abs(onePoleState[channel]) < 1e-20)
                        onePoleState[channel] = 0.0;
                    value = highpass ? value - static_cast<float>(onePoleState[channel])
                                     : static_cast<float>(onePoleState[channel]);
                } else {
                    value = first.process(value, channel);
                    if (slope >= 1.5f)
                        value = second.process(value, channel);
                }
                *channels[channel] = value;
            }
        }
    };

    Filter low_{}, high_{};
};

} // namespace sms::dsp
