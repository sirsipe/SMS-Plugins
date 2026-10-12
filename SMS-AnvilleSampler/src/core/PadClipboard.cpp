#include "PadClipboard.hpp"

#include <utility>

namespace midichopper {

bool PadClipboard::copyFrom(const SamplerEngine& sampler, const std::uint32_t pad)
{
    sms::dsp::SamplePlaybackSettings settings;
    sms::dsp::SampleMixerSettings mixerSettings;
    if (!sampler.exportPad(pad, staging_, &settings, &mixerSettings) || staging_.frames == 0U)
        return false;
    std::swap(sample_, staging_);
    settings_ = settings;
    mixerSettings_ = mixerSettings;
    return true;
}

bool PadClipboard::pasteTo(SamplerEngine& sampler, const std::uint32_t pad) const
{
    if (!hasSample() || !sampler.importPad(pad, sample_, false, &settings_, &mixerSettings_))
        return false;
    return true;
}

} // namespace midichopper
