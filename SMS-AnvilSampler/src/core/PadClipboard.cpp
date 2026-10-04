#include "PadClipboard.hpp"

#include <utility>

namespace midichopper {

bool PadClipboard::copyFrom(const SamplerEngine& sampler, const std::uint32_t pad)
{
    if (!sampler.exportPad(pad, staging_) || staging_.frames == 0U)
        return false;
    const auto settings = sampler.padPlaybackSettings(pad);
    const auto mixerSettings = sampler.padMixerSettings(pad);
    std::swap(sample_, staging_);
    settings_ = settings;
    mixerSettings_ = mixerSettings;
    return true;
}

bool PadClipboard::pasteTo(SamplerEngine& sampler, const std::uint32_t pad) const
{
    if (!hasSample() || !sampler.importPad(pad, sample_))
        return false;
    sampler.setPadPlaybackSettings(pad, settings_);
    sampler.setPadMixerSettings(pad, mixerSettings_);
    return true;
}

} // namespace midichopper
