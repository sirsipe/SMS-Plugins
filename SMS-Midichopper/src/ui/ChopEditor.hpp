#pragma once

#include "Audio/WaveformSummary.hpp"
#include "Configuration.hpp"
#include "MidichopperLayout.hpp"
#include "PadLayout.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>

namespace midichopper::ui::chop {

inline constexpr std::uint32_t kMinimumSliceFrames = 1U;

struct BoundaryGeometry {
    sms::ui::Rect handle{};
    bool rowWrap = false;
};

struct PlayheadPosition {
    int pad = -1;
    float fraction = 0.0f;
};

[[nodiscard]] inline BoundaryGeometry boundaryGeometry(
    const int padLayout, const int boundary) noexcept
{
    const sms::ui::BankedPadLayout pads(padLayout);
    if (boundary < 0 || boundary + 1 >= pads.visiblePadCount())
        return {};
    const auto grid = pads.grid(layout::mainPadBounds, 10.0f);
    const auto left = grid.cell(pads.visualIndex(boundary));
    const auto right = grid.cell(pads.visualIndex(boundary + 1));
    const bool wrap = std::abs(left.y - right.y) > 1.0f;
    if (!wrap) {
        const float center = (left.x + left.width + right.x) * 0.5f;
        return {{center - 7.0f, left.y + left.height * 0.5f - 22.0f, 14.0f, 44.0f}, false};
    }
    return {{left.x + left.width - 8.0f, left.y + left.height * 0.5f - 22.0f,
             16.0f, 44.0f}, true};
}

[[nodiscard]] inline int boundaryAt(const sms::ui::Point point, const int padLayout,
                                    const std::span<const sms::audio::WaveformSummary> waveforms) noexcept
{
    const sms::ui::BankedPadLayout pads(padLayout);
    for (int boundary = 0; boundary + 1 < pads.visiblePadCount(); ++boundary) {
        if (boundary + 1 >= static_cast<int>(waveforms.size()) ||
            waveforms[static_cast<std::size_t>(boundary)].frames == 0U ||
            waveforms[static_cast<std::size_t>(boundary + 1)].frames == 0U)
            continue;
        const double leftRate = waveforms[static_cast<std::size_t>(boundary)].sampleRate;
        const double rightRate = waveforms[static_cast<std::size_t>(boundary + 1)].sampleRate;
        if (std::abs(leftRate - rightRate) > 0.5)
            continue;
        if (boundaryGeometry(padLayout, boundary).handle.contains(point))
            return boundary;
    }
    return -1;
}

[[nodiscard]] inline std::int64_t clampBoundaryOffset(
    const std::span<const sms::audio::WaveformSummary> waveforms,
    const std::span<const std::int64_t> offsets, const int boundary,
    const std::int64_t requested) noexcept
{
    if (boundary < 0 || boundary + 1 >= static_cast<int>(waveforms.size()))
        return 0;
    std::int64_t original = 0;
    for (int index = 0; index <= boundary; ++index)
        original += waveforms[static_cast<std::size_t>(index)].frames;
    std::int64_t previous = 0;
    for (int index = 0; index < boundary; ++index)
        previous += waveforms[static_cast<std::size_t>(index)].frames;
    if (boundary > 0 && boundary - 1 < static_cast<int>(offsets.size()))
        previous += offsets[static_cast<std::size_t>(boundary - 1)];
    std::int64_t next = original +
        waveforms[static_cast<std::size_t>(boundary + 1)].frames;
    if (boundary + 1 < static_cast<int>(offsets.size()))
        next += offsets[static_cast<std::size_t>(boundary + 1)];
    return std::clamp(requested,
        previous + static_cast<std::int64_t>(kMinimumSliceFrames) - original,
        next - static_cast<std::int64_t>(kMinimumSliceFrames) - original);
}

[[nodiscard]] inline std::uint32_t adjustedFrames(
    const std::span<const sms::audio::WaveformSummary> waveforms,
    const std::span<const std::int64_t> offsets, const int pad) noexcept
{
    if (pad < 0 || pad >= static_cast<int>(waveforms.size()))
        return 0U;
    std::int64_t frames = waveforms[static_cast<std::size_t>(pad)].frames;
    if (pad < static_cast<int>(offsets.size()))
        frames += offsets[static_cast<std::size_t>(pad)];
    if (pad > 0 && pad - 1 < static_cast<int>(offsets.size()))
        frames -= offsets[static_cast<std::size_t>(pad - 1)];
    return static_cast<std::uint32_t>(std::max<std::int64_t>(frames, 0));
}

[[nodiscard]] inline std::uint64_t adjustedStartFrame(
    const std::span<const sms::audio::WaveformSummary> waveforms,
    const std::span<const std::int64_t> offsets, const int pad) noexcept
{
    std::int64_t start = 0;
    for (int index = 0; index < pad && index < static_cast<int>(waveforms.size()); ++index)
        start += waveforms[static_cast<std::size_t>(index)].frames;
    if (pad > 0 && pad - 1 < static_cast<int>(offsets.size()))
        start += offsets[static_cast<std::size_t>(pad - 1)];
    return static_cast<std::uint64_t>(std::max<std::int64_t>(start, 0));
}

/** Map an engine position in the original pad partition to the pending partition. */
[[nodiscard]] inline PlayheadPosition adjustedPlayhead(
    const std::span<const sms::audio::WaveformSummary> waveforms,
    const std::span<const std::int64_t> offsets, const int originalPad,
    const float originalFraction) noexcept
{
    if (originalPad < 0 || originalPad >= static_cast<int>(waveforms.size()) ||
        waveforms[static_cast<std::size_t>(originalPad)].frames == 0U)
        return {};
    double sourceFrame = 0.0;
    for (int pad = 0; pad < originalPad; ++pad)
        sourceFrame += waveforms[static_cast<std::size_t>(pad)].frames;
    sourceFrame += std::clamp(originalFraction, 0.0f, 0.999999f) *
                   waveforms[static_cast<std::size_t>(originalPad)].frames;

    for (int pad = 0; pad < static_cast<int>(waveforms.size()); ++pad) {
        const auto start = adjustedStartFrame(waveforms, offsets, pad);
        const auto frames = adjustedFrames(waveforms, offsets, pad);
        if (frames != 0U && sourceFrame >= static_cast<double>(start) &&
            sourceFrame < static_cast<double>(start) + frames) {
            return {pad, static_cast<float>((sourceFrame - static_cast<double>(start)) / frames)};
        }
    }
    return {};
}

[[nodiscard]] inline sms::audio::WaveformSummary previewWaveform(
    const std::span<const sms::audio::WaveformSummary> waveforms,
    const std::span<const std::int64_t> offsets, const int pad) noexcept
{
    sms::audio::WaveformSummary result;
    if (pad < 0 || pad >= static_cast<int>(waveforms.size()) || waveforms.empty())
        return result;
    result.pad = waveforms[static_cast<std::size_t>(pad)].pad;
    result.sampleRate = waveforms[static_cast<std::size_t>(pad)].sampleRate;
    result.frames = adjustedFrames(waveforms, offsets, pad);
    if (result.frames == 0U)
        return result;

    std::array<std::uint64_t, kPadsPerBank + 1U> starts{};
    for (std::size_t index = 0; index < waveforms.size() && index < kPadsPerBank; ++index)
        starts[index + 1U] = starts[index] + waveforms[index].frames;
    const auto targetStart = adjustedStartFrame(waveforms, offsets, pad);
    for (std::size_t bin = 0; bin < sms::audio::kWaveformBins; ++bin) {
        const auto sourceFrame = targetStart + static_cast<std::uint64_t>(
            (static_cast<double>(bin) + 0.5) * result.frames / sms::audio::kWaveformBins);
        std::size_t sourcePad = 0U;
        while (sourcePad + 1U < waveforms.size() && sourceFrame >= starts[sourcePad + 1U])
            ++sourcePad;
        if (sourcePad >= waveforms.size() || waveforms[sourcePad].frames == 0U)
            continue;
        const auto localFrame = sourceFrame - starts[sourcePad];
        const auto sourceBin = std::min<std::size_t>(sms::audio::kWaveformBins - 1U,
            static_cast<std::size_t>(localFrame * sms::audio::kWaveformBins /
                                     waveforms[sourcePad].frames));
        result.minimum[bin] = waveforms[sourcePad].minimum[sourceBin];
        result.maximum[bin] = waveforms[sourcePad].maximum[sourceBin];
    }
    return result;
}

} // namespace midichopper::ui::chop
