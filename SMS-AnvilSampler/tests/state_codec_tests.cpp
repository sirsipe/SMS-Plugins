#include "StateCodec.hpp"
#include "Audio/WaveformSummary.hpp"
#include "WaveformDetailProtocol.hpp"
#include "ChopEditorProtocol.hpp"
#include "EditorSnapshotProtocol.hpp"
#include "PadStructureProtocol.hpp"
#include "PadColorState.hpp"
#include "CaptureActionProtocol.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void check(const bool condition, const char* const message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void roundTrip()
{
    midichopper::PadData original;
    original.sampleRate = 44100.0;
    original.frames = 3;
    original.stereo = {-1.0f, -0.5f, 0.0f, 0.25f, 0.75f, 1.0f};

    const std::string encoded = midichopper::plugin::encodePadState(original, 44100U);
    check(!encoded.empty(), "encoded state is not empty");

    midichopper::plugin::DecodedPadState decoded;
    check(midichopper::plugin::decodePadState(encoded.c_str(), decoded), "valid state decodes");
    check(decoded.sourceSampleRate == 44100U, "sample rate round-trips");
    check(decoded.pad.frames == original.frames, "frame count round-trips");
    check(decoded.pad.stereo.size() == original.stereo.size(), "channel data size round-trips");
    for (std::size_t index = 0; index < original.stereo.size(); ++index)
        check(std::abs(decoded.pad.stereo[index] - original.stereo[index]) < 4.0e-5f,
              "PCM16 round-trip tolerance");
}

void longPadStateRoundTrip()
{
    midichopper::PadData original;
    original.sampleRate = 1000.0;
    original.frames = 31000U;
    original.stereo.assign(static_cast<std::size_t>(original.frames) * 2U, 0.25f);
    const auto encoded = midichopper::plugin::encodePadState(original, 1000U);
    check(!encoded.empty(), "state saves a pad longer than 30 seconds");
    midichopper::plugin::DecodedPadState decoded;
    check(midichopper::plugin::decodePadState(encoded.c_str(), decoded) &&
          decoded.pad.frames == original.frames,
          "long pad restores with its full frame count");
}

void rejectsDamage()
{
    midichopper::PadData original;
    original.sampleRate = 48000.0;
    original.frames = 2;
    original.stereo = {0.1f, -0.1f, 0.2f, -0.2f};
    std::string encoded = midichopper::plugin::encodePadState(original, 48000U);
    check(encoded.size() > 8U, "test state has payload");

    midichopper::plugin::DecodedPadState decoded;
    encoded[encoded.size() / 2U] = encoded[encoded.size() / 2U] == 'A' ? 'B' : 'A';
    check(!midichopper::plugin::decodePadState(encoded.c_str(), decoded), "CRC rejects modified payload");
    check(!midichopper::plugin::decodePadState("not base64", decoded), "invalid Base64 is rejected");
    check(!midichopper::plugin::decodePadState(nullptr, decoded), "null state is rejected");
}

void editorStateRoundTrip()
{
    sms::dsp::SamplePlaybackSettings original;
    original.start = 0.125f;
    original.end = 0.875f;
    original.attackSeconds = 0.012f;
    original.decaySeconds = 0.25f;
    original.sustainLevel = 0.625f;
    original.releaseSeconds = 1.5f;
    const std::string encoded = midichopper::plugin::encodePlaybackSettings(original);
    sms::dsp::SamplePlaybackSettings decoded;
    check(midichopper::plugin::decodePlaybackSettings(encoded.c_str(), decoded),
          "sample editor settings decode");
    check(std::abs(decoded.start - original.start) < 1.0e-6f, "cut start round-trips");
    check(std::abs(decoded.end - original.end) < 1.0e-6f, "cut end round-trips");
    check(std::abs(decoded.releaseSeconds - original.releaseSeconds) < 1.0e-6f,
          "ADSR release round-trips");
    check(!midichopper::plugin::decodePlaybackSettings("SP1;broken", decoded),
          "malformed editor state is rejected");

    const float stereo[] = {-1.0f, 0.5f, -0.25f, 1.0f};
    const auto summary = sms::audio::summarizeStereo(
        midichopper::kPadCount - 1U, stereo, 2U);
    const std::string waveform = sms::audio::encodeWaveformSummary(summary);
    sms::audio::WaveformSummary restored;
    check(sms::audio::decodeWaveformSummary(waveform, restored),
          "waveform summary transport decodes");
    check(restored.pad == midichopper::kPadCount - 1U && restored.frames == 2U,
          "waveform summary identity round-trips for every bank");
    check(std::abs(restored.sampleRate - 48000.0) < 0.5,
          "waveform summary sample rate round-trips");
    check(restored.minimum[0] < -0.99f && restored.maximum[0] > 0.49f,
          "waveform summary retains min/max peaks");
    std::string malformedWaveform = waveform;
    malformedWaveform.insert(malformedWaveform.find(';', 4U), "junk");
    check(!sms::audio::decodeWaveformSummary(malformedWaveform, restored),
          "waveform summary rejects partially parsed pad field");
}

void mixerStateRoundTrip()
{
    sms::dsp::SampleMixerSettings original;
    original.gainDecibels = -7.25f;
    original.pan = 0.375f;
    original.tuneSemitones = -11.5f;
    original.lowpass = 0.6f;
    original.highpass = 0.2f;
    original.filterSlope = 2.0f;
    original.dirty = 1.0f;
    const std::string encoded = midichopper::plugin::encodeMixerSettings(original);
    sms::dsp::SampleMixerSettings decoded;
    check(midichopper::plugin::decodeMixerSettings(encoded.c_str(), decoded),
          "sample mixer settings decode");
    check(std::abs(decoded.gainDecibels - original.gainDecibels) < 1.0e-6f,
          "mixer gain round-trips");
    check(std::abs(decoded.pan - original.pan) < 1.0e-6f,
          "mixer pan round-trips");
    check(std::abs(decoded.tuneSemitones - original.tuneSemitones) < 1.0e-6f,
          "mixer tune round-trips");
    check(decoded.lowpass == original.lowpass &&
          decoded.highpass == original.highpass &&
          decoded.filterSlope == original.filterSlope &&
          decoded.dirty == original.dirty,
          "mixer effects round-trip");
    check(!midichopper::plugin::decodeMixerSettings("MX1;broken", decoded) &&
          !midichopper::plugin::decodeMixerSettings("MX2;0;0;0;0;0;1", decoded) &&
          !midichopper::plugin::decodeMixerSettings("SP1;0;0;0", decoded),
          "malformed and wrong-version mixer state is rejected");

    check(midichopper::plugin::decodeMixerSettings("MX2;0;0;0;0;0;0;0", decoded) &&
          decoded.filterSlope == 0.0f,
          "new-session mixer state defaults to 6 dB slope");
    check(midichopper::plugin::decodeMixerSettings("MX1;-90;2;48", decoded),
          "finite out-of-range mixer state decodes safely");
    check(decoded.gainDecibels == sms::dsp::kMinimumSampleGainDecibels &&
          decoded.pan == sms::dsp::kMaximumSamplePan &&
          decoded.tuneSemitones == sms::dsp::kMaximumTuneSemitones,
          "decoded mixer state clamps to supported ranges");
    check(decoded.lowpass == 0.0f && decoded.highpass == 0.0f &&
          decoded.filterSlope == 1.0f && decoded.dirty == 0.0f,
          "legacy mixer state restores with effects bypassed");
}

void editorSnapshotProtocolRoundTrip()
{
    namespace plugin = midichopper::plugin;
    plugin::EditorSnapshotRequest request{UINT64_MAX, 63U}, decodedRequest;
    check(plugin::decodeEditorSnapshotRequest(plugin::encodeEditorSnapshotRequest(request), decodedRequest) &&
          decodedRequest.sequence == request.sequence && decodedRequest.pad == request.pad,
          "editor request retains sequence and final pad identity");
    for (const auto invalid : {"ES1;0;0", "ES1;1;64", "ES1;1;-1", "ES1;1;0;", "ES1;1",
             "ES1;18446744073709551616;0", "ES2;1;0", "0"})
        check(!plugin::decodeEditorSnapshotRequest(invalid, decodedRequest),
              "malformed editor snapshot requests rejected");
    plugin::EditorSnapshotReply reply;
    reply.request = request;
    reply.waveform.pad = 63U;
    reply.waveform.frames = UINT32_MAX;
    reply.waveform.sampleRate = 768000.0;
    reply.waveform.minimum.fill(-1.0f);
    reply.waveform.maximum.fill(1.0f);
    reply.playback.start = 0.25f;
    reply.playback.attackSeconds = 0.125f;
    reply.mixer.pan = -0.5f;
    reply.mixer.dirty = 1.0f;
    const auto encoded = plugin::encodeEditorSnapshotReply(reply);
    plugin::EditorSnapshotReply decoded;
    check(encoded.size() < 8192U && plugin::decodeEditorSnapshotReply(encoded, decoded) &&
          decoded.request.sequence == reply.request.sequence &&
          decoded.waveform.frames == reply.waveform.frames &&
          decoded.waveform.minimum[0] == -1.0f && decoded.playback.start == 0.25f &&
          decoded.playback.attackSeconds == 0.125f && decoded.mixer.pan == -0.5f && decoded.mixer.dirty == 1.0f,
          "one bounded reply carries coherent waveform, playback and mixer values");
    check(!plugin::decodeEditorSnapshotReply(encoded + "|", decoded) &&
          !plugin::decodeEditorSnapshotReply(encoded.substr(0, encoded.rfind('|')), decoded) &&
          !plugin::decodeEditorSnapshotReply(encoded.substr(0, encoded.rfind('|')) + "|MX2;nan", decoded) &&
          !plugin::decodeEditorSnapshotReply(encoded + std::string("\0extra", 6), decoded),
          "missing, extra, non-finite and embedded-null editor reply fields rejected");
    reply.waveform.pad = 62U;
    check(!plugin::decodeEditorSnapshotReply(plugin::encodeEditorSnapshotReply(reply), decoded),
          "editor waveform must belong to envelope pad");
    reply.waveform.pad = 63U;
    reply.request.sequence = 0U;
    check(!plugin::decodeEditorSnapshotReply(plugin::encodeEditorSnapshotReply(reply), decoded),
          "editor reply requires a live request identity");
}

void chopProtocolRoundTrip()
{
    midichopper::plugin::ChopApplyRequest apply;
    apply.firstPad = 16U;
    apply.padCount = 4U;
    apply.boundaryOffsets[0] = -120;
    apply.boundaryOffsets[1] = 0;
    apply.boundaryOffsets[2] = 240;
    const auto encodedApply = midichopper::plugin::encodeChopApplyRequest(apply);
    midichopper::plugin::ChopApplyRequest decodedApply;
    check(midichopper::plugin::decodeChopApplyRequest(encodedApply, decodedApply) &&
          decodedApply.firstPad == 16U && decodedApply.padCount == 4U &&
          decodedApply.boundaryOffsets[0] == -120 &&
          decodedApply.boundaryOffsets[2] == 240,
          "chop apply request round-trips");
    check(!midichopper::plugin::decodeChopApplyRequest("CH1;0;2", decodedApply) &&
          !midichopper::plugin::decodeChopApplyRequest("CH1;63;2;0", decodedApply),
          "invalid chop apply requests are rejected");

    check(!decodedApply.revisionChecked, "legacy cut command retains explicit unguarded identity");
    apply.revisionChecked = true;
    apply.sequence = 123U;
    apply.expectedGenerations = {0U, 2U, UINT64_MAX, 4U};
    check(midichopper::plugin::decodeChopApplyRequest(
              midichopper::plugin::encodeChopApplyRequest(apply), decodedApply) &&
          decodedApply.revisionChecked && decodedApply.sequence == 123U &&
          decodedApply.expectedGenerations == apply.expectedGenerations &&
          decodedApply.boundaryOffsets == apply.boundaryOffsets,
          "revision-checked cut command round-trips with empty-pad and maximum generations");
    for (const auto invalid : {"CH2;0;0;3;1;2;3;0;0", "CH2;1;0;3;1;2;0;0",
             "CH2;1;0;3;1;2;3;0;0;", "CH2;1;0;3;-1;2;3;0;0",
             "CH2;1;0;3;18446744073709551616;2;3;0;0", "CH3;1;0;3;1;2;3;0;0"})
        check(!midichopper::plugin::decodeChopApplyRequest(invalid, decodedApply),
              "malformed revision-checked cut command rejected");

    midichopper::plugin::ChopSnapshotRequest snapshotRequest{UINT64_MAX, 61U, 3U};
    midichopper::plugin::ChopSnapshotRequest decodedRequest;
    check(midichopper::plugin::decodeChopSnapshotRequest(
              midichopper::plugin::encodeChopSnapshotRequest(snapshotRequest), decodedRequest) &&
          decodedRequest.sequence == snapshotRequest.sequence && decodedRequest.firstPad == 61U,
          "coherent cut baseline request round-trips at final legal pad range");
    for (const auto invalid : {"CQ1;0;0;3", "CQ1;1;62;3", "CQ1;1;0;2", "CQ1;1;0;3;",
             "CQ1;1;0", "CQ2;1;0;3", "CQ1;1;-1;3"})
        check(!midichopper::plugin::decodeChopSnapshotRequest(invalid, decodedRequest),
              "malformed coherent cut baseline request rejected");

    midichopper::plugin::ChopSnapshotReply snapshot;
    snapshot.request = snapshotRequest;
    snapshot.generations = {UINT64_MAX, UINT64_MAX, UINT64_MAX};
    for (std::size_t index = 0; index < snapshot.waveforms.size(); ++index) {
        snapshot.waveforms[index].pad = 61U + static_cast<std::uint32_t>(index);
        snapshot.waveforms[index].frames = UINT32_MAX;
        snapshot.waveforms[index].sampleRate = 768000.0;
        snapshot.waveforms[index].minimum.fill(-1.0f);
        snapshot.waveforms[index].maximum.fill(1.0f);
    }
    const auto encodedSnapshot = midichopper::plugin::encodeChopSnapshotReply(snapshot);
    midichopper::plugin::ChopSnapshotReply decodedSnapshot;
    check(encodedSnapshot.size() < 8192U &&
          midichopper::plugin::decodeChopSnapshotReply(encodedSnapshot, decodedSnapshot) &&
          decodedSnapshot.request.sequence == snapshot.request.sequence &&
          decodedSnapshot.generations == snapshot.generations &&
          decodedSnapshot.waveforms[2].pad == 63U &&
          decodedSnapshot.waveforms[2].frames == UINT32_MAX &&
          decodedSnapshot.waveforms[2].minimum[0] == -1.0f,
          "coherent baseline reply round-trips within transport bound at maximum field widths");
    check(!midichopper::plugin::decodeChopSnapshotReply(encodedSnapshot + "|", decodedSnapshot) &&
          !midichopper::plugin::decodeChopSnapshotReply(
              encodedSnapshot.substr(0, encodedSnapshot.rfind('|')), decodedSnapshot) &&
          !midichopper::plugin::decodeChopSnapshotReply("CS1;1;0;3;1;2;3|broken", decodedSnapshot),
          "missing, extra and malformed baseline waveforms rejected");
    snapshot.waveforms[1].pad = 63U;
    check(!midichopper::plugin::decodeChopSnapshotReply(
              midichopper::plugin::encodeChopSnapshotReply(snapshot), decodedSnapshot),
          "baseline waveforms must match their contiguous pad range");

    midichopper::plugin::ChopApplyStatus status;
    check(midichopper::plugin::decodeChopApplyStatus(
              midichopper::plugin::encodeChopStatus(123U, true), status) &&
          status.sequence == 123U && status.success &&
          midichopper::plugin::decodeChopApplyStatus(
              "CH2;123;ERROR;Samples changed", status) &&
          !status.success && status.message == "Samples changed" &&
          midichopper::plugin::decodeChopSnapshotError(
              "CS1;123;ERROR;Recording active", status) && status.sequence == 123U,
          "cut completion and snapshot errors retain their request sequence");
    check(!midichopper::plugin::decodeChopApplyStatus("CH1;OK", status) &&
          !midichopper::plugin::decodeChopApplyStatus("CH2;0;OK", status) &&
          !midichopper::plugin::decodeChopApplyStatus("CH2;1;OK;extra", status) &&
          !midichopper::plugin::decodeChopSnapshotError("CS1;1;ERROR;", status),
          "unmatched and malformed completion identities rejected");

    const midichopper::plugin::ChopPreviewRequest preview{true, 8U, 8U, 12345U, 23456U};
    const auto encodedPreview = midichopper::plugin::encodeChopPreviewRequest(preview);
    midichopper::plugin::ChopPreviewRequest decodedPreview;
    check(midichopper::plugin::decodeChopPreviewRequest(encodedPreview, decodedPreview) &&
          decodedPreview.play && decodedPreview.firstPad == 8U &&
          decodedPreview.padCount == 8U && decodedPreview.sourceFrame == 12345U &&
          decodedPreview.sourceEndFrame == 23456U,
          "chop preview request round-trips");
    check(!midichopper::plugin::decodeChopPreviewRequest("CP1;2;0;2;0;1", decodedPreview) &&
          !midichopper::plugin::decodeChopPreviewRequest("CP1;1;0;3;50;50", decodedPreview) &&
          !midichopper::plugin::decodeChopPreviewRequest("CP1;1;0;3;0", decodedPreview),
          "invalid chop preview command is rejected");

    midichopper::plugin::ChopMidiPreviewRequest midiPreview;
    midiPreview.active = true;
    midiPreview.firstPad = 20U;
    midiPreview.sourcePadCount = 1U;
    midiPreview.previewPadCount = 2U;
    midiPreview.sourceFrames[0] = 0U;
    midiPreview.sourceEndFrames[0] = 500U;
    midiPreview.sourceFrames[1] = 500U;
    midiPreview.sourceEndFrames[1] = 1000U;
    const auto encodedMidiPreview =
        midichopper::plugin::encodeChopMidiPreviewRequest(midiPreview);
    midichopper::plugin::ChopMidiPreviewRequest decodedMidiPreview;
    check(midichopper::plugin::decodeChopMidiPreviewRequest(
              encodedMidiPreview, decodedMidiPreview) &&
          decodedMidiPreview.active && decodedMidiPreview.firstPad == 20U &&
          decodedMidiPreview.sourcePadCount == 1U &&
          decodedMidiPreview.previewPadCount == 2U &&
          decodedMidiPreview.sourceFrames[1] == 500U &&
          decodedMidiPreview.sourceEndFrames[1] == 1000U,
          "split MIDI preview map round-trips");
    const auto disabledMidiPreview =
        midichopper::plugin::encodeChopMidiPreviewRequest({});
    check(midichopper::plugin::decodeChopMidiPreviewRequest(
              disabledMidiPreview, decodedMidiPreview) && !decodedMidiPreview.active,
          "disabled MIDI preview map round-trips");
    midiPreview.previewPadCount = 0U;
    check(midichopper::plugin::decodeChopMidiPreviewRequest(
              midichopper::plugin::encodeChopMidiPreviewRequest(midiPreview),
              decodedMidiPreview) && decodedMidiPreview.active &&
          decodedMidiPreview.previewPadCount == 0U,
          "loading editor MIDI mute map round-trips");
    check(!midichopper::plugin::decodeChopMidiPreviewRequest(
              "CM1;1;0;3;3;0;4;4;8;8", decodedMidiPreview) &&
          !midichopper::plugin::decodeChopMidiPreviewRequest(
              "CM1;1;0;1;2;0;5;6;4", decodedMidiPreview) &&
          !midichopper::plugin::decodeChopMidiPreviewRequest(
              "CM1;0;extra", decodedMidiPreview),
          "invalid MIDI preview maps are rejected");
}

void padStructureProtocolRoundTrip()
{
    using midichopper::plugin::PadStructureAction;
    using midichopper::plugin::PadStructureRequest;
    PadStructureRequest decoded;

    PadStructureRequest collapse;
    collapse.action = PadStructureAction::collapse;
    collapse.firstPad = 16U;
    collapse.padCount = 12U;
    collapse.targetPad = 19U;
    check(midichopper::plugin::decodePadStructureRequest(
              midichopper::plugin::encodePadStructureRequest(collapse), decoded) &&
          decoded.action == PadStructureAction::collapse &&
          decoded.firstPad == 16U && decoded.padCount == 12U &&
          decoded.targetPad == 19U,
          "collapse request round-trips");

    collapse.action = PadStructureAction::prepareSplit;
    check(midichopper::plugin::decodePadStructureRequest(
              midichopper::plugin::encodePadStructureRequest(collapse), decoded) &&
          decoded.action == PadStructureAction::prepareSplit,
          "split preparation request round-trips");

    PadStructureRequest apply;
    apply.action = PadStructureAction::applySplit;
    apply.planId = 42U;
    apply.splitFrame = 1234U;
    check(midichopper::plugin::decodePadStructureRequest(
              midichopper::plugin::encodePadStructureRequest(apply), decoded) &&
          decoded.action == PadStructureAction::applySplit &&
          decoded.planId == 42U && decoded.splitFrame == 1234U,
          "split apply request round-trips");

    apply.action = PadStructureAction::cancelSplit;
    check(midichopper::plugin::decodePadStructureRequest(
              midichopper::plugin::encodePadStructureRequest(apply), decoded) &&
          decoded.action == PadStructureAction::cancelSplit && decoded.planId == 42U,
          "split cancellation request round-trips");

    sms::audio::WaveformSummary splitWaveform;
    splitWaveform.pad = 20U;
    splitWaveform.frames = 200U;
    splitWaveform.sampleRate = 48000.0;
    splitWaveform.minimum.fill(-0.5f);
    splitWaveform.maximum.fill(0.5f);
    midichopper::plugin::SplitPlanReady ready{42U, 20U, 24U, splitWaveform};
    midichopper::plugin::SplitPlanReady decodedReady;
    check(midichopper::plugin::decodeSplitPlanReady(
              midichopper::plugin::encodeSplitPlanReady(ready), decodedReady) &&
          decodedReady.planId == ready.planId &&
          decodedReady.targetPad == ready.targetPad &&
          decodedReady.emptyPad == ready.emptyPad &&
          decodedReady.waveform.frames == ready.waveform.frames &&
          decodedReady.waveform.minimum.front() < -0.49f,
          "split plan response round-trips");

    check(!midichopper::plugin::decodePadStructureRequest("PS2;C;0;16;1", decoded) &&
          !midichopper::plugin::decodePadStructureRequest("PS1;C;63;2;63", decoded) &&
          !midichopper::plugin::decodePadStructureRequest("PS1;A;0;20", decoded) &&
          !midichopper::plugin::decodePadStructureRequest("PS1;A;1;0", decoded) &&
          !midichopper::plugin::decodePadStructureRequest("PS1;X;1;", decoded) &&
          !midichopper::plugin::decodePadStructureRequest("PS1;X;1;extra", decoded) &&
          !midichopper::plugin::decodeSplitPlanReady("PS1;R;1;20;20;broken", decodedReady),
          "malformed pad structure messages are rejected");
}

void captureActionsRejectRestoredOrMalformedState()
{
    using midichopper::plugin::captureActionRequestMask;
    check(captureActionRequestMask("finalize") == 0x1U &&
          captureActionRequestMask("undo") == 0x2U &&
          captureActionRequestMask("clear") == 0x4U,
          "capture action requests match the existing audio-block command bits");
    for (const auto value : {"", "0", "1", "CLEAR", "clear_all", "clear;undo",
                             "finalize ", " undo", "unknown"})
        check(captureActionRequestMask(value) == 0U,
              "empty restored and malformed capture requests never run an action");
}

} // namespace

int main()
{
    using namespace midichopper::plugin;
    check(decodePadColorIndex("0", 6) == 0 &&
          decodePadColorIndex("4", 6) == 4 &&
          decodePadColorIndex("6", 6) == 6,
          "saved pad palette indices decode");
    check(decodePadColorIndex("6", 5) == 0 &&
          decodePadColorIndex("7", 6) == 0 &&
          decodePadColorIndex("-1", 6) == 0 &&
          decodePadColorIndex("2x", 6) == 0 &&
          decodePadColorIndex("", 6) == 0,
          "unavailable or malformed pad colors fall back to no color");
    const WaveformDetailRequest detail{73U, 12U, 3U, 1000U, 1128U};
    WaveformDetailRequest decodedDetail;
    check(decodeWaveformDetailRequest(encodeWaveformDetailRequest(detail), decodedDetail) &&
          decodedDetail.sequence == 73U && decodedDetail.start == 1000U &&
          decodedDetail.end == 1128U,
          "visible waveform request round-trips its generation and frame range");
    sms::audio::WaveformSummary detailSummary;
    detailSummary.pad = 12U;
    detailSummary.frames = 128U;
    detailSummary.maximum[0] = 0.75f;
    WaveformDetailReply detailReply{detail, detailSummary};
    WaveformDetailReply decodedReply;
    check(decodeWaveformDetailReply(encodeWaveformDetailReply(detailReply), decodedReply) &&
          decodedReply.request.sequence == 73U &&
          decodedReply.waveform.maximum[0] > 0.74f,
          "visible waveform reply preserves identity and detail");
    check(!decodeWaveformDetailRequest("WD1;1;63;3;0;128", decodedDetail) &&
          !decodeWaveformDetailRequest("WD1;1;0;1;2;2", decodedDetail) &&
          !decodeWaveformDetailRequest("WD1;1;0;1;0;128junk", decodedDetail),
          "visible waveform requests reject invalid pad and frame bounds");
    roundTrip();
    longPadStateRoundTrip();
    rejectsDamage();
    editorStateRoundTrip();
    mixerStateRoundTrip();
    editorSnapshotProtocolRoundTrip();
    chopProtocolRoundTrip();
    padStructureProtocolRoundTrip();
    captureActionsRejectRestoredOrMalformedState();
    std::cout << "state codec tests passed\n";
}
