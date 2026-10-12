#include "PadWorkflows.hpp"

#include "ChopEditorProtocol.hpp"
#include "PadFileActions.hpp"
#include "PadStructureProtocol.hpp"
#include "StateCodec.hpp"
#include "StateKeys.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <utility>

namespace midichopper::plugin {
namespace {

[[nodiscard]] std::filesystem::path pathFromUtf8(const std::string_view path)
{
    return std::filesystem::path(std::u8string(
        reinterpret_cast<const char8_t*>(path.data()), path.size()));
}

} // namespace

void PadWorkflows::publishPadSettings(const std::uint32_t firstPad,
                        const std::uint32_t padCount)
{
    for (std::uint32_t index = 0; index < padCount; ++index) {
        const std::uint32_t pad = firstPad + index;
        const std::string editor = midichopper::plugin::encodePlaybackSettings(
            sampler_.padPlaybackSettings(pad));
        observer_.publishUiState(
            kPadEditStateKeys[pad].c_str(), editor.c_str());
        const std::string mixer = midichopper::plugin::encodeMixerSettings(
            sampler_.padMixerSettings(pad));
        observer_.publishUiState(
            kPadMixerStateKeys[pad].c_str(), mixer.c_str());
    }
}

void PadWorkflows::publishPadStructureError(const std::uint64_t planId,
                              const char* const message)
{
    const std::string status = "PS1;E;" + std::to_string(planId) + ";" + message;
    observer_.publishUiState(kPadStructureStatusKey, status.c_str());
}

void PadWorkflows::handlePadStructureRequest(const std::string_view encoded)
{
    bool committed = false;
    std::uint64_t attemptedPlanId = 0U;
    std::string committedStatus;
    try {
        midichopper::plugin::PadStructureRequest request;
        if (!midichopper::plugin::decodePadStructureRequest(encoded, request)) {
            publishPadStructureError(0U, "Invalid pad operation");
            return;
        }
        attemptedPlanId = request.planId;
        std::unique_lock mutationLock(padMutationMutex_);
        using midichopper::plugin::PadStructureAction;
        if (request.action == PadStructureAction::cancelSplit) {
            if (pendingSplitPlan_.active && pendingSplitPlan_.id == request.planId)
                pendingSplitPlan_ = {};
            return;
        }
        if (request.action == PadStructureAction::collapse) {
            committedStatus = "PS1;O;0;C";
            pendingSplitPlan_ = {};
            if (!sampler_.collapsePadGap(
                    request.firstPad, request.padCount, request.targetPad)) {
                publishPadStructureError(0U, "Could not collapse this gap");
                return;
            }
            committed = true;
            mutationLock.unlock();
            publishPadSettings(request.firstPad, request.padCount);
            observer_.publishUiState(
                kPadStructureStatusKey, committedStatus.c_str());
            return;
        }
        if (request.action == PadStructureAction::prepareSplit) {
            pendingSplitPlan_ = {};
            PendingSplitPlan prepared;
            sms::audio::WaveformSummary preparedWaveform;
            midichopper::PadData waveformSource;
            bool valid = false;
            controlDispatcher_.invoke([&]() noexcept {
                const auto settings = sampler_.settings();
                if (settings.armed)
                    return;
                const std::uint32_t visibleFirst =
                    static_cast<std::uint32_t>(settings.activeBank) *
                    midichopper::bankStride(settings.padsPerBank, settings.midiBankMode);
                if (request.firstPad != visibleFirst ||
                    request.padCount != settings.padsPerBank ||
                    request.targetPad < visibleFirst ||
                    request.targetPad >= visibleFirst + request.padCount)
                    return;
                const auto source = sampler_.padMetadata(request.targetPad);
                if (!source.occupied || source.recording || source.frames < 2U)
                    return;
                std::uint32_t empty = request.targetPad + 1U;
                const std::uint32_t end = visibleFirst + request.padCount;
                while (empty < end && sampler_.padMetadata(empty).occupied)
                    ++empty;
                if (empty == end)
                    return;
                prepared.active = true;
                prepared.visibleFirst = visibleFirst;
                prepared.visibleCount = request.padCount;
                prepared.firstPad = request.targetPad;
                prepared.emptyPad = empty;
                prepared.generationCount = empty - request.targetPad + 1U;
                for (std::uint32_t index = 0; index < prepared.generationCount; ++index)
                    prepared.generations[index] =
                        sampler_.padMetadata(request.targetPad + index).generation;
                valid = true;
            });
            if (valid) {
                valid = sampler_.exportPad(request.targetPad, waveformSource) &&
                    waveformSource.generation == prepared.generations[0] &&
                    waveformSource.frames >= 2U;
            }
            if (valid) {
                preparedWaveform = sms::audio::summarizeStereo(
                    request.targetPad, waveformSource.stereo.data(),
                    waveformSource.frames, waveformSource.sampleRate);
                controlDispatcher_.invoke([&]() noexcept {
                    const auto settings = sampler_.settings();
                    valid = !settings.armed && settings.padsPerBank == prepared.visibleCount &&
                        static_cast<std::uint32_t>(settings.activeBank) *
                            midichopper::bankStride(settings.padsPerBank, settings.midiBankMode) ==
                            prepared.visibleFirst;
                    for (std::uint32_t index = 0; valid && index < prepared.generationCount; ++index) {
                        const auto metadata = sampler_.padMetadata(prepared.firstPad + index);
                        valid = !metadata.recording &&
                            metadata.generation == prepared.generations[index];
                    }
                });
            }
            if (!valid) {
                publishPadStructureError(0U, "Sample cannot be split in this pad range");
                return;
            }
            prepared.id = nextSplitPlanId_++;
            if (prepared.id == 0U)
                prepared.id = nextSplitPlanId_++;
            pendingSplitPlan_ = prepared;
            mutationLock.unlock();
            const auto ready = midichopper::plugin::encodeSplitPlanReady(
                {prepared.id, prepared.firstPad, prepared.emptyPad, preparedWaveform});
            observer_.publishUiState(kPadStructureStatusKey, ready.c_str());
            return;
        }

        if (!pendingSplitPlan_.active ||
            pendingSplitPlan_.id != request.planId) {
            publishPadStructureError(request.planId, "Split plan expired");
            return;
        }
        const PendingSplitPlan plan = pendingSplitPlan_;
        committedStatus = "PS1;O;" + std::to_string(request.planId) + ";S";
        pendingSplitPlan_ = {};
        bool visible = false;
        controlDispatcher_.invoke([&]() noexcept {
            const auto settings = sampler_.settings();
            const std::uint32_t visibleFirst =
                static_cast<std::uint32_t>(settings.activeBank) *
                midichopper::bankStride(settings.padsPerBank, settings.midiBankMode);
            visible = visibleFirst == plan.visibleFirst &&
                      settings.padsPerBank == plan.visibleCount;
        });
        if (!visible || !sampler_.splitPadAndShiftRight(
                plan.firstPad, plan.emptyPad, request.splitFrame,
                std::span<const std::uint64_t>{
                    plan.generations.data(), plan.generationCount})) {
            publishPadStructureError(
                request.planId, "Pads changed; reopen Split Sample");
            return;
        }
        committed = true;
        mutationLock.unlock();
        publishPadSettings(plan.firstPad, plan.emptyPad - plan.firstPad + 1U);
        observer_.publishUiState(
            kPadStructureStatusKey, committedStatus.c_str());
    } catch (...) {
        pendingSplitPlan_ = {};
        if (committed) {
            try {
                observer_.publishUiState(
                    kPadStructureStatusKey, committedStatus.c_str());
            } catch (...) {
            }
            return;
        }
        try {
            publishPadStructureError(attemptedPlanId, "Pad operation failed");
        } catch (...) {
        }
    }
}

bool PadWorkflows::makeWaveformState(const std::uint32_t pad,
    std::string& waveform,
    sms::dsp::SamplePlaybackSettings* playback,
    sms::dsp::SampleMixerSettings* mixer,
    sms::audio::WaveformSummary* capturedSummary) const
{
    midichopper::PadData snapshot;
    if (!sampler_.exportPublishedPad(pad, snapshot, playback, mixer))
        return false;
    const auto summary = snapshot.frames == 0U
        ? sms::audio::WaveformSummary{pad, 0U, snapshot.sampleRate, {}, {}}
        : sms::audio::summarizeStereo(
            pad, snapshot.stereo.data(), snapshot.frames, snapshot.sampleRate);
    waveform = sms::audio::encodeWaveformSummary(summary);
    if (capturedSummary) *capturedSummary = summary;
    return true;
}

void PadWorkflows::handleChopApplyRequest(const std::string_view encoded)
{
    try {
        handleChopApplyRequestImpl(encoded);
    } catch (...) {
        midichopper::plugin::ChopApplyRequest request;
        static_cast<void>(midichopper::plugin::decodeChopApplyRequest(encoded, request));
        const auto status = midichopper::plugin::encodeChopStatus(
            request.sequence, false, "Chop apply failed");
        observer_.publishUiState(kChopStatusKey, status.c_str());
    }
}

void PadWorkflows::handleChopApplyRequestImpl(const std::string_view encoded)
{
    midichopper::plugin::ChopApplyRequest request;
    if (!midichopper::plugin::decodeChopApplyRequest(encoded, request)) {
        observer_.publishUiState(kChopStatusKey, "CH2;0;ERROR;Invalid request");
        return;
    }
    if (!request.revisionChecked) {
        observer_.publishUiState(kChopStatusKey, "CH2;0;ERROR;Reopen Adjust Cut Points");
        return;
    }
    const std::lock_guard lock(padMutationMutex_);
    if (!sampler_.rechopPads(request.firstPad, request.padCount,
            std::span<const std::int64_t>{request.boundaryOffsets.data(),
                                          request.padCount - 1U},
            std::span<const std::uint64_t>{request.expectedGenerations.data(),
                                           request.padCount})) {
        const auto status = midichopper::plugin::encodeChopStatus(request.sequence,
            false, "Pads changed or storage is unavailable; reopen Adjust Cut Points");
        observer_.publishUiState(kChopStatusKey, status.c_str());
        return;
    }
    for (std::uint32_t index = 0; index < request.padCount; ++index) {
        const auto pad = request.firstPad + index;
        const std::string editor = midichopper::plugin::encodePlaybackSettings(
            sampler_.padPlaybackSettings(pad));
        observer_.publishUiState(kPadEditStateKeys[pad].c_str(), editor.c_str());
        const std::string mixer = midichopper::plugin::encodeMixerSettings(
            sampler_.padMixerSettings(pad));
        observer_.publishUiState(kPadMixerStateKeys[pad].c_str(), mixer.c_str());
    }
    const auto status = midichopper::plugin::encodeChopStatus(request.sequence, true);
    observer_.publishUiState(kChopStatusKey, status.c_str());
}

void PadWorkflows::handlePadClipboardRequest(const std::string_view encoded)
{
    try {
        handlePadClipboardRequestImpl(encoded);
    } catch (...) {
        observer_.publishClipboardResult(midichopper::plugin::PadClipboardResultCode::failed);
    }
}

void PadWorkflows::handlePadClipboardRequestImpl(const std::string_view encoded)
{
    using midichopper::plugin::PadClipboardAction;
    midichopper::plugin::PadClipboardRequest request;
    if (!midichopper::plugin::decodePadClipboardRequest(encoded, request)) {
        observer_.publishClipboardResult(midichopper::plugin::PadClipboardResultCode::failed);
        return;
    }

    const std::lock_guard lock(padMutationMutex_);
    const bool succeeded = request.action == PadClipboardAction::copy
        ? padClipboard_.copyFrom(sampler_, request.pad)
        : padClipboard_.pasteTo(sampler_, request.pad);
    const auto result = !succeeded
        ? midichopper::plugin::PadClipboardResultCode::failed
        : request.action == PadClipboardAction::copy
            ? midichopper::plugin::PadClipboardResultCode::copied
            : midichopper::plugin::PadClipboardResultCode::pasted;

    if (result == midichopper::plugin::PadClipboardResultCode::copied)
        observer_.clipboardAvailable();
    if (result == midichopper::plugin::PadClipboardResultCode::pasted) {
        const std::string editor = midichopper::plugin::encodePlaybackSettings(
            sampler_.padPlaybackSettings(request.pad));
        observer_.publishUiState(
            kPadEditStateKeys[request.pad].c_str(), editor.c_str());
        const std::string mixer = midichopper::plugin::encodeMixerSettings(
            sampler_.padMixerSettings(request.pad));
        observer_.publishUiState(
            kPadMixerStateKeys[request.pad].c_str(), mixer.c_str());
        std::string waveform;
        if (makeWaveformState(request.pad, waveform))
            observer_.publishUiState(kWaveformDataKey, waveform.c_str());
    }
    observer_.publishClipboardResult(result);
}

void PadWorkflows::handlePadFileRequest(const std::string_view encoded)
{
    try {
        handlePadFileRequestImpl(encoded);
    } catch (...) {
        observer_.publishUiState(kPadFileStatusKey, "WAV action failed");
        observer_.publishUiState(kPadFileBusyKey, "0");
        observer_.publishFileResult(midichopper::plugin::PadFileResultCode::failed);
    }
}

void PadWorkflows::handlePadFileRequestImpl(const std::string_view encoded)
{
    using midichopper::plugin::PadFileAction;
    midichopper::plugin::PadFileRequest request;
    if (!midichopper::plugin::decodePadFileRequest(encoded, request)) {
        publishFileStatus("Invalid pad file request");
        observer_.publishFileResult(midichopper::plugin::PadFileResultCode::failed);
        return;
    }

    const std::lock_guard lock(padMutationMutex_);
    observer_.publishUiState(kPadFileBusyKey, "1");
    std::string status;
    auto result = midichopper::plugin::PadFileResultCode::failed;
    if (request.action == PadFileAction::import) {
        std::uint64_t baseline = 0;
        controlDispatcher_.invoke([&]() noexcept {
            baseline = sampler_.padMetadata(request.pad).generation;
        });
        auto loaded = midichopper::plugin::readPadWav(pathFromUtf8(request.path));
        if (!loaded) {
            status = std::move(loaded.error);
        } else {
            midichopper::PadData replacement;
            replacement.sampleRate = loaded.audio.sampleRate;
            replacement.frames = loaded.audio.frames;
            replacement.stereo = std::move(loaded.audio.stereo);
            double sumSquares = 0.0;
            for (const float sample : replacement.stereo) {
                replacement.peak = std::max(replacement.peak, std::abs(sample));
                sumSquares += static_cast<double>(sample) * sample;
            }
            replacement.rms = static_cast<float>(std::sqrt(
                sumSquares / static_cast<double>(replacement.stereo.size())));

            if (!sampler_.importPad(request.pad, replacement, true,
                                   nullptr, nullptr, baseline)) {
                status = "Pad changed or sampler storage is unavailable; retry WAV import";
            } else {
                status = "WAV imported";
                result = midichopper::plugin::PadFileResultCode::importSucceeded;
                const std::string editor = midichopper::plugin::encodePlaybackSettings({});
                observer_.publishUiState(
                    kPadEditStateKeys[request.pad].c_str(), editor.c_str());
                const std::string mixer = midichopper::plugin::encodeMixerSettings({});
                observer_.publishUiState(
                    kPadMixerStateKeys[request.pad].c_str(), mixer.c_str());
                std::string waveform;
                if (makeWaveformState(request.pad, waveform))
                    observer_.publishUiState(kWaveformDataKey, waveform.c_str());
            }
        }
    } else {
        midichopper::PadData snapshot;
        sms::dsp::SamplePlaybackSettings settings;
        sms::dsp::SampleMixerSettings mixerSettings;
        if (!sampler_.exportPad(request.pad, snapshot, &settings, &mixerSettings) ||
            snapshot.frames == 0U) {
            status = "Pad is empty";
        } else {
            sms::audio::WavAudio audio;
            audio.sampleRate = static_cast<std::uint32_t>(
                std::clamp(snapshot.sampleRate, 1.0, 384000.0));
            audio.frames = snapshot.frames;
            audio.stereo = std::move(snapshot.stereo);
            if (request.action == PadFileAction::exportProcessed)
                audio = sms::audio::renderProcessedStereo(audio, settings, mixerSettings);
            status = midichopper::plugin::writePadWav(pathFromUtf8(request.path), audio);
            if (status.empty())
            {
                status = request.action == PadFileAction::exportProcessed
                    ? "Processed WAV exported" : "WAV exported";
                result = request.action == PadFileAction::exportProcessed
                    ? midichopper::plugin::PadFileResultCode::processedExportSucceeded
                    : midichopper::plugin::PadFileResultCode::rawExportSucceeded;
            }
        }
    }
    publishFileStatus(status);
    observer_.publishUiState(kPadFileBusyKey, "0");
    observer_.publishFileResult(result);
}

} // namespace midichopper::plugin
