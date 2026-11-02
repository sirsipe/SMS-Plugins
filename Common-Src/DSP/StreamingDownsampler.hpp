#pragma once

#include "SincResampler.hpp"

#include <cassert>
#include <limits>
#include <vector>

namespace sms::dsp {

/** Bounded-history, stereo sinc downsampling for live capture. configure()
 * allocates/touches history outside audio. push()/finish()/reset() do not
 * allocate. Output centers retain their original timing; push waits for right
 * taps, and finish edge-extends the last input instead of adding an audio tail.
 * A sink must not throw; false means stop feeding until reset. History is one FIR window,
 * never a copy of the recording. Rates must be finite, positive, output<=input.
 */
class StreamingDownsampler {
  public:
    void configure(const double inputRate, const double outputRate) {
        step_ = inputRate / outputRate;
        filter_.configure(step_);
        radius_ = filter_.sourceRadius();
        history_.assign(static_cast<std::size_t>(std::ceil(2.0 * radius_)) + 4U, {});
        reset();
    }

    void reset() noexcept { inputFrames_ = outputFrames_ = 0U; }

    [[nodiscard]] std::uint32_t framesForInput(const std::uint64_t frames) const noexcept {
        if (frames == 0U)
            return 0U;
        return static_cast<std::uint32_t>(std::max(1LL, std::llround(static_cast<double>(frames) / step_)));
    }

    template <class Sink> bool push(const StereoFrame frame, Sink&& sink) noexcept {
        if (inputFrames_ == std::numeric_limits<std::uint32_t>::max())
            return false;
        history_[inputFrames_ % history_.size()] = frame;
        ++inputFrames_;
        while (std::ceil(outputFrames_ * step_ + radius_) < inputFrames_)
            if (!emit(sink))
                return false;
        return true;
    }

    template <class Sink> bool finish(Sink&& sink) noexcept {
        const auto count = framesForInput(inputFrames_);
        while (outputFrames_ < count)
            if (!emit(sink))
                return false;
        return true;
    }

  private:
    template <class Sink> bool emit(Sink& sink) noexcept {
        const auto sample = filter_.read(
            outputFrames_ * step_, 0U, inputFrames_, false, [&](const std::uint32_t index) noexcept {
                assert(static_cast<std::uint64_t>(index) + history_.size() >= inputFrames_);
                return history_[index % history_.size()];
            });
        if (!sink(sample))
            return false;
        ++outputFrames_;
        return true;
    }

    SincResampler filter_;
    std::vector<StereoFrame> history_;
    double step_ = 1.0;
    double radius_ = 16.0;
    std::uint32_t inputFrames_ = 0U;
    std::uint32_t outputFrames_ = 0U;
};

} // namespace sms::dsp
