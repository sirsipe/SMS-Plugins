#pragma once

#include "SamplerEngine.hpp"

namespace midichopper {

/** Non-real-time, per-instance snapshot used by pad Copy and Paste actions. */
class PadClipboard {
public:
    void reserveFrames(std::uint32_t frames)
    {
        staging_.stereo.reserve(static_cast<std::size_t>(frames) * 2U);
    }
    [[nodiscard]] bool copyFrom(const SamplerEngine& sampler, std::uint32_t pad);
    [[nodiscard]] bool pasteTo(SamplerEngine& sampler, std::uint32_t pad) const;
    [[nodiscard]] bool hasSample() const noexcept { return sample_.frames != 0U; }

private:
    PadData sample_;
    PadData staging_;
    sms::dsp::SamplePlaybackSettings settings_{};
    sms::dsp::SampleMixerSettings mixerSettings_{};
};

} // namespace midichopper
