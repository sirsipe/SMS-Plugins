#include "PluginControls.hpp"

#include "CaptureActionProtocol.hpp"
#include "PlaybackRequest.hpp"
#include "StateKeys.hpp"
#include "SamplerEngine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace midichopper::plugin {

namespace {

static_assert(midichopper::kPadCount <= 64U,
              "pending clear requests use one bit per pad");

[[nodiscard]] float decibelsToGain(const float decibels) noexcept
{
    return std::pow(10.0f, decibels / 20.0f);
}

} // namespace

PluginControls::PluginControls(const int initialLayout, const int initialBankMode) noexcept
{
    for (std::uint32_t index = 0; index < kParameterCount; ++index)
        parameters_[index].store(parameterRange(index).defaultValue, std::memory_order_relaxed);
    parameters_[kParameterPadLayout].store(static_cast<float>(initialLayout), std::memory_order_relaxed);
    parameters_[kParameterMidiBankMode].store(static_cast<float>(initialBankMode), std::memory_order_relaxed);
}

void PluginControls::publishParameterValue(const std::uint32_t index, const float value) noexcept
{
    parameters_[index].store(value, std::memory_order_relaxed);
}

float PluginControls::exchangeParameterValue(const std::uint32_t index, const float value) noexcept
{
    return parameters_[index].exchange(value, std::memory_order_relaxed);
}

void PluginControls::setParameterValue(const std::uint32_t index, const float value) noexcept
{
    if (index >= midichopper::plugin::kParameterCount)
        return;

    // DPF/LV2 reports control parameters only when their value changes,
    // while a UI may send the same momentary command more than once. Turn
    // those calls into edges here so each click is honoured, even if the
    // host's last control-port value remains 1.
    if (index >= midichopper::plugin::kParameterFinalize &&
        index <= midichopper::plugin::kParameterClearAll && value >= 0.5f) {
        const auto bit = 1U << (index - midichopper::plugin::kParameterFinalize);
        pendingCommands_.fetch_or(bit, std::memory_order_release);
        parameters_[index].store(0.0f, std::memory_order_relaxed);
        return;
    }
    if (index == midichopper::plugin::kParameterStopAllPlayback && value >= 0.5f) {
        pendingCommands_.fetch_or(0x8U, std::memory_order_release);
        parameters_[index].store(0.0f, std::memory_order_relaxed);
        return;
    }

    if (index == midichopper::plugin::kParameterCaptureTargetRequest) {
        const std::uint32_t pad =
            midichopper::plugin::padFromCaptureTargetRequest(value);
        if (pad < midichopper::kPadCount)
            pendingCaptureTarget_.store(pad + 1U, std::memory_order_release);
        parameters_[index].store(0.0f, std::memory_order_relaxed);
        return;
    }

    parameters_[index].store(value, std::memory_order_relaxed);
}

bool PluginControls::queueStateCommand(const char* const key, const char* const value)
{
    if (std::strcmp(key, kCaptureActionRequestKey) == 0) {
        pendingCommands_.fetch_or(captureActionRequestMask(
            value != nullptr ? value : ""), std::memory_order_release);
        return true;
    }
    if (std::strcmp(key, kPlayStopRequestKey) == 0) {
        midichopper::plugin::PlaybackRequest request;
        if (value != nullptr && midichopper::plugin::decodePlaybackRequest(value, request))
            pendingPlaybackRequest_.store(
                midichopper::plugin::encodePlaybackRequest(request) + 1U,
                std::memory_order_release);
        return true;
    }
    if (std::strcmp(key, kPadClearRequestKey) == 0) {
        const char* const input = value != nullptr ? value : "";
        char* end = nullptr;
        const auto requested = std::strtoul(input, &end, 10);
        if (end != input && *end == '\0' && requested >= 1U &&
            requested <= midichopper::kPadCount) {
            const auto pad = static_cast<std::uint32_t>(requested - 1U);
            pendingClearPads_.fetch_or(
                std::uint64_t{1} << pad, std::memory_order_release);
        }
        return true;
    }
    if (std::strcmp(key, kChopPreviewRequestKey) == 0) {
        midichopper::plugin::ChopPreviewRequest request;
        if (midichopper::plugin::decodeChopPreviewRequest(
                value != nullptr ? value : "", request)) {
            chopPreviewMailbox_.publish(request);
        }
        return true;
    }
    if (std::strcmp(key, kChopMidiPreviewKey) == 0) {
        midichopper::plugin::ChopMidiPreviewRequest request;
        if (midichopper::plugin::decodeChopMidiPreviewRequest(
                value != nullptr ? value : "", request)) {
            const std::lock_guard lock(chopMidiPreviewMutex_);
            pendingChopMidiPreviewSequence_.fetch_add(1U, std::memory_order_acq_rel);
            const std::uint32_t packed = request.active
                ? 1U | ((request.firstPad & 0xffU) << 8U) |
                    ((request.sourcePadCount & 0xffU) << 16U) |
                    ((request.previewPadCount & 0xffU) << 24U)
                : 0U;
            pendingChopMidiPreviewConfig_.store(packed, std::memory_order_relaxed);
            for (std::size_t index = 0;
                 index < midichopper::plugin::kChopMidiPreviewPadCount; ++index) {
                pendingChopMidiPreviewFrames_[index].store(
                    request.sourceFrames[index], std::memory_order_relaxed);
                pendingChopMidiPreviewEndFrames_[index].store(
                    request.sourceEndFrames[index], std::memory_order_relaxed);
            }
            pendingChopMidiPreviewSequence_.fetch_add(1U, std::memory_order_release);
        }
        return true;
    }
    return false;
}

void PluginControls::applyToEngine(
    SamplerEngine& sampler, const GlobalSettings& globalSettings) noexcept
{
    applySettings(sampler, globalSettings);
    applyChopMidiPreviewConfig(sampler);
    applyCommandTriggers(sampler);
}

float PluginControls::value(const std::uint32_t index) const noexcept
{
    return parameters_[index].load(std::memory_order_relaxed);
}

float PluginControls::clampedParameter(const std::uint32_t index) const noexcept
{
    const auto range = midichopper::plugin::parameterRange(index);
    return std::clamp(value(index), range.minimum, range.maximum);
}

float PluginControls::effectiveGlobalParameter(
    const std::uint32_t index, const GlobalSettings& globalSettings) const noexcept
{
    return midichopper::plugin::effectiveGlobalValue(index, value(index),
        globalSettings[midichopper::plugin::globalControlIndex(index)]);
}

void PluginControls::applySettings(
    midichopper::SamplerEngine& sampler, const GlobalSettings& globalSettings) const noexcept
{
    using namespace midichopper::plugin;
    midichopper::EngineSettings settings;
    settings.armed = value(kParameterMode) >= 0.5f;
    settings.startPad = static_cast<std::uint8_t>(
        clampedParameter(kParameterStartPad) - parameterRanges::startPad.minimum);
    settings.preRollMilliseconds = clampedParameter(kParameterPreRollMs);
    settings.captureMode = value(kParameterCaptureMode) >= 0.5f
        ? midichopper::CaptureMode::FixedDuration : midichopper::CaptureMode::Sequential;
    settings.fixedLengthSeconds = clampedParameter(kParameterFixedLengthSeconds);
    settings.playbackMode = value(kParameterPlaybackMode) >= 0.5f
        ? midichopper::PlaybackMode::Hold : midichopper::PlaybackMode::OneShot;
    settings.monitorInput = inputMonitorEnabled(
        clampedParameter(kParameterInputMonitor), settings.armed);
    settings.baseNote = static_cast<std::uint8_t>(clampedParameter(kParameterBaseMidiNote));
    settings.gain = decibelsToGain(effectiveGlobalParameter(kParameterOutputGainDb, globalSettings));
    settings.monitorGain = decibelsToGain(clampedParameter(kParameterMonitorGainDb));
    settings.pan = effectiveGlobalParameter(kParameterGlobalPan, globalSettings);
    settings.tuneSemitones = effectiveGlobalParameter(kParameterGlobalTuneSemitones, globalSettings);
    settings.lowpass = effectiveGlobalParameter(kParameterGlobalLowpass, globalSettings);
    settings.highpass = effectiveGlobalParameter(kParameterGlobalHighpass, globalSettings);
    settings.filterSlope = clampedParameter(kParameterGlobalFilterSlope);
    settings.dirty = clampedParameter(kParameterGlobalDirty);
    settings.reverse = clampedParameter(kParameterGlobalReverse) >= 0.5f;
    settings.maxVoices = static_cast<std::uint8_t>(clampedParameter(kParameterMaxVoices));
    settings.activeBank = static_cast<std::uint8_t>(
        clampedParameter(kParameterActiveBank) - parameterRanges::activeBank.minimum);
    settings.midiBankMode = value(kParameterMidiBankMode) >= 0.5f
        ? midichopper::MidiBankMode::AllBanks : midichopper::MidiBankMode::SelectedBank;
    const auto layout = static_cast<std::uint32_t>(
        clampedParameter(kParameterPadLayout));
    settings.padsPerBank = midichopper::padsPerBankForLayout(layout);
    sampler.setSettings(settings);
}

void PluginControls::applyCommandTriggers(midichopper::SamplerEngine& sampler) noexcept
{
    const uint32_t commands = pendingCommands_.exchange(0U, std::memory_order_acquire);
    if ((commands & 0x1U) != 0U) sampler.finalizeRecording();
    if ((commands & 0x2U) != 0U) sampler.undoLastSlice();
    if ((commands & 0x4U) != 0U) sampler.clearAllPads();
    if ((commands & 0x8U) != 0U) sampler.stopAllPlayback();
    const std::uint32_t pendingPlayback =
        pendingPlaybackRequest_.exchange(0U, std::memory_order_acquire);
    midichopper::plugin::PlaybackRequest playback;
    if (pendingPlayback != 0U &&
        midichopper::plugin::decodePlaybackRequest(pendingPlayback - 1U, playback)) {
        using midichopper::plugin::PlaybackAction;
        switch (playback.action) {
        case PlaybackAction::Toggle: sampler.togglePlayback(playback.pad); break;
        case PlaybackAction::ToggleLoop: sampler.togglePlayback(playback.pad, true); break;
        case PlaybackAction::Play: sampler.auditionPad(playback.pad, false); break;
        case PlaybackAction::Loop: sampler.auditionPad(playback.pad, true); break;
        case PlaybackAction::StopAudition: sampler.stopPadAudition(); break;
        }
    }
    const std::uint64_t clearPads =
        pendingClearPads_.exchange(0U, std::memory_order_acquire);
    if (clearPads != 0U) {
        for (std::uint32_t pad = 0; pad < midichopper::kPadCount; ++pad) {
            if ((clearPads & (std::uint64_t{1} << pad)) != 0U)
                sampler.clearPad(pad);
        }
    }
    const std::uint32_t captureTarget =
        pendingCaptureTarget_.exchange(0U, std::memory_order_acquire);
    if (captureTarget != 0U)
        sampler.selectCaptureTarget(captureTarget - 1U);
    midichopper::plugin::ChopPreviewRequest preview;
    if (chopPreviewMailbox_.tryConsume(preview)) {
        if (preview.play) {
            sampler.startChopPreview(preview.firstPad, preview.padCount,
                preview.sourceFrame, preview.sourceEndFrame);
        } else {
            sampler.stopChopPreview();
        }
    }
}

void PluginControls::applyChopMidiPreviewConfig(midichopper::SamplerEngine& sampler) noexcept
{
    const std::uint64_t before =
        pendingChopMidiPreviewSequence_.load(std::memory_order_acquire);
    if ((before & 1U) != 0U || before == appliedChopMidiPreviewSequence_)
        return;
    const std::uint32_t packed =
        pendingChopMidiPreviewConfig_.load(std::memory_order_relaxed);
    midichopper::ChopMidiPreview preview;
    preview.active = (packed & 1U) != 0U;
    preview.firstPad = (packed >> 8U) & 0xffU;
    preview.sourcePadCount = (packed >> 16U) & 0xffU;
    preview.previewPadCount = (packed >> 24U) & 0xffU;
    for (std::size_t index = 0;
         index < midichopper::plugin::kChopMidiPreviewPadCount; ++index) {
        preview.sourceFrames[index] =
            pendingChopMidiPreviewFrames_[index].load(std::memory_order_relaxed);
        preview.sourceEndFrames[index] =
            pendingChopMidiPreviewEndFrames_[index].load(std::memory_order_relaxed);
    }
    const std::uint64_t after =
        pendingChopMidiPreviewSequence_.load(std::memory_order_acquire);
    if (before != after || (after & 1U) != 0U)
        return;
    sampler.setChopMidiPreview(preview);
    appliedChopMidiPreviewSequence_ = after;
}

} // namespace midichopper::plugin
