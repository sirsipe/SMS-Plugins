#include "PadClipboard.hpp"

#include <utility>

namespace midichopper {

bool PadClipboard::copyFrom(const SamplerEngine& sampler, const std::uint32_t pad)
{
    PadData sample;
    if (!sampler.exportPad(pad, sample) || sample.frames == 0U)
        return false;
    const auto settings = sampler.padPlaybackSettings(pad);
    sample_ = std::move(sample);
    settings_ = settings;
    return true;
}

bool PadClipboard::pasteTo(SamplerEngine& sampler, const std::uint32_t pad) const
{
    if (!hasSample() || !sampler.importPad(pad, sample_))
        return false;
    sampler.setPadPlaybackSettings(pad, settings_);
    return true;
}

} // namespace midichopper
