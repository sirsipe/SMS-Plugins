#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>

namespace reference_resampler {
// Analytic oracle: evaluate the continuous windowed sinc directly,
// independently of the production lookup tables and their interpolation.
// Fixtures using this oracle stay within the full-support range (step <= 256).
inline float sample(const std::span<const float> stereo, const double position,
                    const double step, const std::uint32_t start,
                    const std::uint32_t end, const bool looping,
                    const bool reverse = false, const std::uint32_t channel = 0U)
{
    const double scale = 1.0 / std::max(1.0, step);
    const double blend = std::clamp((step - 1.0) / 0.1, 0.0, 1.0);
    const double radius = (blend < 1.0 ? 16.0 : 12.0) / scale;
    // I0(z) = sum((z*z/4)^k / (k!)^2); this converges rapidly for beta=5.5
    // and avoids platform-specific special-function availability.
    const auto i0 = [](const double value) {
        double term = 1.0, sum = 1.0;
        for (unsigned k = 1U; k <= 20U; ++k) {
            term *= value * value / (4.0 * k * k);
            sum += term;
        }
        return sum;
    };
    const double kaiserNormalization = i0(5.5);
    double sum = 0.0, normalization = 0.0;
    for (auto tap = static_cast<std::int64_t>(std::ceil(position - radius));
         tap <= static_cast<std::int64_t>(std::floor(position + radius)); ++tap) {
        const double x = (static_cast<double>(tap) - position) * scale;
        const auto sinc = [x](const double cutoff) {
            return x == 0.0 ? cutoff :
                std::sin(std::numbers::pi * cutoff * x) / (std::numbers::pi * x);
        };
        const double blackman = std::abs(x) <= 16.0
            ? 0.42 + 0.5 * std::cos(std::numbers::pi * x / 16.0) +
              0.08 * std::cos(std::numbers::pi * x / 8.0) : 0.0;
        const double kaiser = std::abs(x) <= 12.0
            ? i0(5.5 * std::sqrt(std::max(0.0, 1.0 - x * x / 144.0))) /
              kaiserNormalization : 0.0;
        const double weight = (1.0 - blend) * blackman * sinc(1.0) +
                              blend * kaiser * sinc(0.85);
        auto index = tap;
        if (looping) {
            const auto length = static_cast<std::int64_t>(end - start);
            index = start + ((index - start) % length + length) % length;
        } else {
            index = std::clamp(index, static_cast<std::int64_t>(start),
                               static_cast<std::int64_t>(end) - 1);
        }
        if (reverse) index = static_cast<std::int64_t>(stereo.size() / 2U) - 1 - index;
        sum += stereo[static_cast<std::size_t>(index) * 2U + channel] * weight;
        normalization += weight;
    }
    return static_cast<float>(sum / normalization);
}
} // namespace reference_resampler
