#pragma once

#include "WaveformViewport.hpp"
#include "DSP/SamplePlaybackSettings.hpp"

#include <algorithm>

namespace midichopper::ui {

[[nodiscard]] constexpr bool sampleGlobalReverse(const bool globalReverse,
                                                const float globalMixer) noexcept
{ return globalReverse && globalMixer >= 0.5f; }

[[nodiscard]] constexpr bool sampleDisplayReversed(const bool padReverse,
                                                  const bool globalReverse) noexcept
{ return padReverse != globalReverse; }

/** Keep the same raw interval when the Sample Editor flips source orientation. */
[[nodiscard]] inline sms::dsp::SamplePlaybackSettings mirrorSampleRegion(
    const sms::dsp::SamplePlaybackSettings& settings) noexcept
{
    return sms::dsp::mirroredRegion(settings);
}

/** Global reversal is a display projection; the same transform maps edits back
 * into local pad settings without changing their saved direction. */
[[nodiscard]] inline sms::dsp::SamplePlaybackSettings sampleEditorSettings(
    const sms::dsp::SamplePlaybackSettings& local, const bool globalReverse) noexcept
{ return globalReverse ? mirrorSampleRegion(local) : local; }

/** Only edited display boundaries map back to saved cuts. Projecting unchanged
 * cuts twice can move a non-binary boundary by one floating-point step. */
[[nodiscard]] inline sms::dsp::SamplePlaybackSettings sampleEditorLocalEdit(
    const sms::dsp::SamplePlaybackSettings& local,
    sms::dsp::SamplePlaybackSettings displayed, const bool globalReverse,
    const bool startEdited, const bool endEdited) noexcept
{
    const float displayedStart = displayed.start;
    const float displayedEnd = displayed.end;
    displayed.start = local.start;
    displayed.end = local.end;
    if (globalReverse) {
        if (startEdited)
            displayed.end = 1.0f - displayedStart;
        if (endEdited)
            displayed.start = 1.0f - displayedEnd;
    } else {
        if (startEdited)
            displayed.start = displayedStart;
        if (endEdited)
            displayed.end = displayedEnd;
    }
    return displayed;
}

/** Sample Editor coordinates follow the oriented whole source, before trimming.
 * Stored summaries and detail remain in raw coordinates for cut/split editing. */
[[nodiscard]] inline sms::ui::waveform::Viewport sampleSourceViewport(
    sms::ui::waveform::Viewport viewport, const bool reverse) noexcept
{
    if (reverse && viewport.start <= viewport.end && viewport.end <= viewport.total) {
        const auto start = viewport.total - viewport.end;
        viewport.end = viewport.total - viewport.start;
        viewport.start = start;
    }
    return viewport;
}

[[nodiscard]] inline sms::audio::WaveformSummary sampleDisplaySummary(
    sms::audio::WaveformSummary summary, const bool reverse) noexcept
{
    if (reverse) {
        std::reverse(summary.minimum.begin(), summary.minimum.end());
        std::reverse(summary.maximum.begin(), summary.maximum.end());
    }
    return summary;
}

[[nodiscard]] inline sms::audio::WaveformSummary sampleVisibleSummary(
    const sms::audio::WaveformSummary& raw, const sms::ui::waveform::Viewport viewport,
    const sms::ui::waveform::CachedDetail* const detail, const bool reverse) noexcept
{
    // Reproject in raw coordinates before flipping bins so rounding at deep zoom
    // and sources not divisible by the bin count follows the requested PCM range.
    return sampleDisplaySummary(sms::ui::waveform::visibleSummary(
        raw, sampleSourceViewport(viewport, reverse), detail), reverse);
}

} // namespace midichopper::ui
