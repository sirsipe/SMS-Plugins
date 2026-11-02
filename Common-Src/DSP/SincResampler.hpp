#pragma once

#include <algorithm>
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#endif
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace sms::dsp {

struct StereoFrame {
    float left = 0.0f;
    float right = 0.0f;
};

/** Random-access, stereo windowed-sinc interpolation. Construct outside audio:
 * the first constructor prepares shared coefficient tables. configure()/read()
 * neither allocate nor initialize statics. The reader supplies immutable PCM.
 * No cursor/history is retained, so cuts and direction changes keep their timing.
 */
class SincResampler {
public:
    SincResampler() noexcept : tables_(&tables()) { configure(1.0); }

    [[nodiscard]] double sourceRadius() const noexcept { return radius_; }

    /** Source frames advanced per output frame. The 15% downsampling transition
     * band gives >50 dB rejection at/above destination Nyquist from 1.1x to 256x.
     * Between 1x and 1.1x, blend toward the narrower kernel continuously: this
     * trades rejection near unity for continuity with native-rate playback.
     * Beyond that, support is capped to bound callback work; rejection degrades.
     */
    void configure(const double step) noexcept {
        const double rate = std::isfinite(step) && step > 0.0 ? step : 1.0;
        if (rate == configuredRate_) return;
        configuredRate_ = rate;
        downsampling_ = rate > 1.0;
        scale_ = downsampling_ ? 1.0 / rate : 1.0;
        blend_ = static_cast<float>(std::clamp((rate - 1.0) / 0.1, 0.0, 1.0));
        transition_ = blend_ > 0.0f && blend_ < 1.0f;
        radius_ = std::min((blend_ == 1.0f ? kDownHalfWidth : kHalfWidth) / scale_, kMaximumRadius);
        kernel_ = blend_ == 1.0f ? tables_->down.data() : tables_->up.data();
        const float coefficientStep = static_cast<float>(scale_ * kResolution);
        integerTableStep_ = downsampling_ && !transition_ && coefficientStep >= 1.0f &&
            coefficientStep == std::floor(coefficientStep) ? static_cast<std::uint32_t>(coefficientStep) : 0U;
        phaseZeroWeight_ = 0.0;
        // At integer table strides, integer cursor phases use the table
        // directly. Cache their normalization once per rate change. Restrict
        // support to table indices that the generic path would actually read.
        const auto extent = static_cast<std::int64_t>(std::floor(radius_));
        if (extent * integerTableStep_ >= kTableSize - 1U) integerTableStep_ = 0U;
        if (integerTableStep_ != 0U) {
            for (auto offset = -extent; offset <= extent; ++offset) {
                const auto index = static_cast<std::uint32_t>(std::abs(offset)) * integerTableStep_;
                phaseZeroWeight_ += kernel_[index];
            }
        }
    }

    /** Requires start < end and start <= position < end. All reader indices
     * remain in [start,end): periodic extension for loops, edge extension for
     * one-shots. A reader returns both channels so coefficients are shared.
     */
    template<class Reader>
    [[nodiscard]] StereoFrame read(const double position, const std::uint32_t start,
        const std::uint32_t end, const bool looping, Reader&& reader) const noexcept {
        const auto center = static_cast<std::int64_t>(position);
        if (end - start == 1U) return reader(start);
        if (!downsampling_ && position == static_cast<double>(center))
            return reader(static_cast<std::uint32_t>(center));
        const auto first = static_cast<std::int64_t>(std::ceil(position - radius_));
        const auto last = static_cast<std::int64_t>(std::floor(position + radius_));
        const auto length = static_cast<std::int64_t>(end) - start;
        const float phase = static_cast<float>(position - static_cast<double>(center));
        const float tableStep = static_cast<float>(scale_ * kResolution);
        const auto convolve = [&](auto&& readTap) noexcept {
            // Coefficients repeat at table-aligned rates and integer phases.
            // Two accumulators break the dependency chain. Explicit loops on
            // either side of the center keep table strides constant without
            // outlined helper lambdas. Keep reader calls in ascending order:
            // the loop mapper depends on this. Double sums can round differently
            // after regrouping; coefficients and precision remain unchanged.
#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
            if (integerTableStep_ != 0U && phase == 0.0f) {
                __m128d sum0 = _mm_setzero_pd();
                __m128d sum1 = _mm_setzero_pd();
                {
                    auto tap = first;
                    const auto lastTap = center - 1;
                    std::int64_t index = (center - first) * integerTableStep_;
                    const auto direction = -static_cast<std::int64_t>(integerTableStep_);
                    for (; tap + 1 <= lastTap; tap += 2, index += direction * 2) {
                        const auto a = readTap(tap);
                        const auto b = readTap(tap + 1);
                        sum0 = _mm_add_pd(
                            sum0, _mm_mul_pd(_mm_set_pd(a.right, a.left), _mm_set1_pd(kernel_[index])));
                        sum1 = _mm_add_pd(sum1, _mm_mul_pd(_mm_set_pd(b.right, b.left),
                                                           _mm_set1_pd(kernel_[index + direction])));
                    }
                    for (; tap <= lastTap; ++tap, index += direction) {
                        const auto sample = readTap(tap);
                        sum0 = _mm_add_pd(sum0, _mm_mul_pd(_mm_set_pd(sample.right, sample.left),
                                                           _mm_set1_pd(kernel_[index])));
                    }
                }
                {
                    auto tap = center;
                    const auto lastTap = last;
                    std::int64_t index = 0;
                    const auto direction = static_cast<std::int64_t>(integerTableStep_);
                    for (; tap + 1 <= lastTap; tap += 2, index += direction * 2) {
                        const auto a = readTap(tap);
                        const auto b = readTap(tap + 1);
                        sum0 = _mm_add_pd(
                            sum0, _mm_mul_pd(_mm_set_pd(a.right, a.left), _mm_set1_pd(kernel_[index])));
                        sum1 = _mm_add_pd(sum1, _mm_mul_pd(_mm_set_pd(b.right, b.left),
                                                           _mm_set1_pd(kernel_[index + direction])));
                    }
                    for (; tap <= lastTap; ++tap, index += direction) {
                        const auto sample = readTap(tap);
                        sum0 = _mm_add_pd(sum0, _mm_mul_pd(_mm_set_pd(sample.right, sample.left),
                                                           _mm_set1_pd(kernel_[index])));
                    }
                }
                double channels[2];
                _mm_storeu_pd(channels, _mm_div_pd(_mm_add_pd(sum0, sum1), _mm_set1_pd(phaseZeroWeight_)));
                return StereoFrame{static_cast<float>(channels[0]), static_cast<float>(channels[1])};
            }
#else
            if (integerTableStep_ != 0U && phase == 0.0f) {
                double left0 = 0.0, right0 = 0.0;
                double left1 = 0.0, right1 = 0.0;
                {
                    auto tap = first;
                    const auto lastTap = center - 1;
                    std::int64_t index = (center - first) * integerTableStep_;
                    const auto direction = -static_cast<std::int64_t>(integerTableStep_);
                    for (; tap + 1 <= lastTap; tap += 2, index += direction * 2) {
                        const auto a = readTap(tap);
                        const float coefficientA = kernel_[index];
                        left0 += static_cast<double>(a.left) * coefficientA;
                        right0 += static_cast<double>(a.right) * coefficientA;
                        const auto b = readTap(tap + 1);
                        const float coefficientB = kernel_[index + direction];
                        left1 += static_cast<double>(b.left) * coefficientB;
                        right1 += static_cast<double>(b.right) * coefficientB;
                    }
                    for (; tap <= lastTap; ++tap, index += direction) {
                        const auto sample = readTap(tap);
                        const float coefficient = kernel_[index];
                        left0 += static_cast<double>(sample.left) * coefficient;
                        right0 += static_cast<double>(sample.right) * coefficient;
                    }
                }
                {
                    auto tap = center;
                    const auto lastTap = last;
                    std::int64_t index = 0;
                    const auto direction = static_cast<std::int64_t>(integerTableStep_);
                    for (; tap + 1 <= lastTap; tap += 2, index += direction * 2) {
                        const auto a = readTap(tap);
                        const float coefficientA = kernel_[index];
                        left0 += static_cast<double>(a.left) * coefficientA;
                        right0 += static_cast<double>(a.right) * coefficientA;
                        const auto b = readTap(tap + 1);
                        const float coefficientB = kernel_[index + direction];
                        left1 += static_cast<double>(b.left) * coefficientB;
                        right1 += static_cast<double>(b.right) * coefficientB;
                    }
                    for (; tap <= lastTap; ++tap, index += direction) {
                        const auto sample = readTap(tap);
                        const float coefficient = kernel_[index];
                        left0 += static_cast<double>(sample.left) * coefficient;
                        right0 += static_cast<double>(sample.right) * coefficient;
                    }
                }
                const double left = left0 + left1;
                const double right = right0 + right1;
                return StereoFrame{static_cast<float>(left / phaseZeroWeight_),
                                   static_cast<float>(right / phaseZeroWeight_)};
            }
#endif

            double left = 0.0, right = 0.0, weight = 0.0;
            for (auto offset = first - center; offset <= last - center; ++offset) {
                const float tablePosition = std::abs(static_cast<float>(offset) - phase) * tableStep;
                const auto index = static_cast<std::uint32_t>(tablePosition);
                if (index >= kTableSize - 1U)
                    continue;
                const float fraction = tablePosition - static_cast<float>(index);
                float coefficient = kernel_[index] + (kernel_[index + 1U] - kernel_[index]) * fraction;
                if (transition_) {
                    const float down =
                        tables_->down[index] + (tables_->down[index + 1U] - tables_->down[index]) * fraction;
                    coefficient += blend_ * (down - coefficient);
                }
                const auto sample = readTap(center + offset);
                left += static_cast<double>(sample.left) * coefficient;
                right += static_cast<double>(sample.right) * coefficient;
                weight += coefficient;
            }
            return StereoFrame{static_cast<float>(left / weight), static_cast<float>(right / weight)};
        };
        // Most reads are interior: keep boundary mapping out of their FIR loop.
        if (first >= start && last < end)
            return convolve(
                [&](const std::int64_t tap) noexcept { return reader(static_cast<std::uint32_t>(tap)); });
        if (looping) {
            auto wrapped = (first - start) % length;
            if (wrapped < 0)
                wrapped += length;
            auto mapped = static_cast<std::uint32_t>(start + wrapped);
            auto next = first;
            return convolve([&](const std::int64_t tap) noexcept {
                // Taps arrive consecutively; wrap by increment, avoiding an
                // integer division per tap on very short loops. A skipped
                // table endpoint is the only possible discontinuity.
                if (tap != next)
                    mapped = start + static_cast<std::uint32_t>((mapped - start + tap - next) % length);
                const auto sample = reader(mapped);
                if (++mapped == end) mapped = start;
                next = tap + 1;
                return sample;
            });
        }
        return convolve([&](const std::int64_t tap) noexcept {
            const auto mapped = std::clamp(tap, static_cast<std::int64_t>(start),
                                          static_cast<std::int64_t>(end) - 1);
            return reader(static_cast<std::uint32_t>(mapped));
        });
    }

private:
    static constexpr double kHalfWidth = 16.0;
    static constexpr double kDownHalfWidth = 12.0;
    static constexpr double kMaximumRadius = 4096.0;
    static constexpr double kResolution = 256.0;
    static constexpr std::uint32_t kTableSize = 4097U;
    struct Tables {
        std::array<float, kTableSize> up{}, down{};
        Tables() noexcept {
            // I0 series avoids platform-specific special-function support.
            const auto bessel = [](const double value) noexcept {
                double sum = 1.0, term = 1.0;
                for (unsigned order = 1; order <= 20U; ++order) {
                    term *= (value * value * 0.25) / (order * order);
                    sum += term;
                }
                return sum;
            };
            const double normalization = bessel(5.5);
            for (std::uint32_t index = 0; index < kTableSize; ++index) {
                const double x = index / kResolution;
                const double window = 0.42 + 0.5 * std::cos(std::numbers::pi * x / kHalfWidth) +
                                      0.08 * std::cos(2.0 * std::numbers::pi * x / kHalfWidth);
                const auto sinc = [x](const double cutoff) noexcept {
                    return x == 0.0 ? cutoff :
                        std::sin(std::numbers::pi * cutoff * x) / (std::numbers::pi * x);
                };
                up[index] = static_cast<float>(window * sinc(1.0));
                if (x <= kDownHalfWidth) {
                    const double relative = x / kDownHalfWidth;
                    const double kaiser = bessel(5.5 * std::sqrt(std::max(0.0, 1.0 - relative * relative))) /
                                          normalization;
                    down[index] = static_cast<float>(kaiser * sinc(0.85));
                }
            }
        }
    };
    [[nodiscard]] static const Tables& tables() noexcept {
        static const Tables value;
        return value;
    }
    double configuredRate_ = -1.0;
    double phaseZeroWeight_ = 0.0;
    std::uint32_t integerTableStep_ = 0U;
    const Tables* tables_;
    const float* kernel_ = nullptr;
    double scale_ = 1.0;
    double radius_ = kHalfWidth;
    bool downsampling_ = false;
    bool transition_ = false;
    float blend_ = 0.0f;
};

} // namespace sms::dsp
