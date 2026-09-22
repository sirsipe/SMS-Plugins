#pragma once

#include "Audio/WaveformSummary.hpp"
#include "Configuration.hpp"
#include "ChopEditor.hpp"
#include "ContextMenu.hpp"
#include "DSP/SampleMixerSettings.hpp"
#include "Interaction.hpp"
#include "MidichopperLayout.hpp"
#include "PadLayout.hpp"
#include "WaveformEditor.hpp"

#include <cstdint>
#include <span>

namespace midichopper::ui {

enum class InteractiveType : int {
    menuButton = 0,
    menuLayout,
    menuMidiBankMode,
    padContextItem,
    closeEditor,
    bank,
    pad,
    regionHandle,
    envelopeNode,
    envelopeSlider,
    mixerKnob,
    globalMixerKnob,
    playOnSelect,
    openEditor,
    chopBoundary,
    chopPadPreview,
    chopApply,
    chopCancel,
    playMode,
    armMode,
    sequentialMode,
    fixedMode,
    fixedLength,
    oneShotMode,
    gatedMode,
    voiceLimit,
    preRoll,
    monitor,
    finalizeAction,
    undoAction,
    clearAction,
};

class PadPressTracker {
public:
    [[nodiscard]] constexpr int pad() const noexcept { return pad_; }

    /** Record a new press and return the MIDI note that must be released first. */
    [[nodiscard]] constexpr int press(const int pad, const int midiNote) noexcept
    {
        const int previousMidiNote = midiNote_;
        pad_ = pad;
        midiNote_ = midiNote;
        return previousMidiNote;
    }

    /** Clear the active press and return its MIDI note for note-off. */
    [[nodiscard]] constexpr int release() noexcept
    {
        const int releasedMidiNote = midiNote_;
        pad_ = -1;
        midiNote_ = -1;
        return releasedMidiNote;
    }

private:
    int pad_ = -1;
    int midiNote_ = -1;
};

class DoubleClickTracker {
public:
    [[nodiscard]] bool press(const sms::ui::InteractiveTarget target,
                             const sms::ui::Point point,
                             const std::uint64_t milliseconds) noexcept
    {
        const float dx = point.x - point_.x;
        const float dy = point.y - point_.y;
        const bool matched = target.valid() && target == target_ &&
            milliseconds >= milliseconds_ && milliseconds - milliseconds_ <= 350U &&
            dx * dx + dy * dy <= 36.0f;
        target_ = matched ? sms::ui::kNoInteractiveTarget : target;
        point_ = point;
        milliseconds_ = milliseconds;
        return matched;
    }

private:
    sms::ui::InteractiveTarget target_ = sms::ui::kNoInteractiveTarget;
    sms::ui::Point point_{};
    std::uint64_t milliseconds_ = 0U;
};

[[nodiscard]] constexpr bool isResettableMixerControl(
    const sms::ui::InteractiveTarget candidate) noexcept
{
    return candidate.is(static_cast<int>(InteractiveType::mixerKnob)) ||
           candidate.is(static_cast<int>(InteractiveType::globalMixerKnob)) ||
           candidate.is(static_cast<int>(InteractiveType::envelopeSlider));
}

inline void resetMixerKnob(sms::dsp::SampleMixerSettings& settings,
                           const int knob) noexcept
{
    switch (knob) {
    case 0: settings.gainDecibels = 0.0f; break;
    case 1: settings.pan = 0.0f; break;
    case 2: settings.tuneSemitones = 0.0f; break;
    default: return;
    }
    settings = sms::dsp::sanitize(settings);
}

[[nodiscard]] inline float knobDragNormalized(const float startValue,
                                              const float startY,
                                              const float currentY) noexcept
{
    if (!std::isfinite(startValue) || !std::isfinite(startY) || !std::isfinite(currentY))
        return std::clamp(std::isfinite(startValue) ? startValue : 0.0f, 0.0f, 1.0f);
    return std::clamp(startValue + (startY - currentY) / 120.0f, 0.0f, 1.0f);
}

/** Map a bipolar control so its zero value is drawn at twelve o'clock. */
[[nodiscard]] inline float bipolarKnobPosition(const float value,
                                              const float minimum,
                                              const float maximum) noexcept
{
    if (!std::isfinite(value) || !std::isfinite(minimum) || !std::isfinite(maximum) ||
        minimum >= 0.0f || maximum <= 0.0f)
        return 0.5f;
    return value < 0.0f
        ? 0.5f * std::clamp(value / -minimum + 1.0f, 0.0f, 1.0f)
        : 0.5f + 0.5f * std::clamp(value / maximum, 0.0f, 1.0f);
}

/** Editor data only needs reloading when the requested pad changes. */
[[nodiscard]] constexpr bool editorPadSelectionChanged(const int selectedPad,
                                                       const int requestedPad) noexcept
{
    return selectedPad != requestedPad;
}

/** Collect the independently delivered parts of one sample-editor response. */
class EditorSnapshotCollector {
public:
    void begin(const int pad) noexcept
    {
        pad_ = pad;
        waveformReady_ = false;
        playbackReady_ = false;
        mixerReady_ = false;
    }

    [[nodiscard]] bool pending() const noexcept { return pad_ >= 0; }
    [[nodiscard]] int pad() const noexcept { return pad_; }
    [[nodiscard]] bool readyFor(const int pad) const noexcept
    {
        return pad_ == pad && waveformReady_ && playbackReady_ && mixerReady_;
    }

    bool accept(const sms::audio::WaveformSummary& waveform) noexcept
    {
        if (pad_ < 0 || waveform.pad != static_cast<std::uint32_t>(pad_))
            return false;
        waveform_ = waveform;
        waveformReady_ = true;
        return true;
    }

    bool accept(const int pad, const sms::dsp::SamplePlaybackSettings& playback) noexcept
    {
        if (pad != pad_)
            return false;
        playback_ = playback;
        playbackReady_ = true;
        return true;
    }

    bool accept(const int pad, const sms::dsp::SampleMixerSettings& mixer) noexcept
    {
        if (pad != pad_)
            return false;
        mixer_ = mixer;
        mixerReady_ = true;
        return true;
    }

    void complete() noexcept { pad_ = -1; }

    [[nodiscard]] const sms::audio::WaveformSummary& waveform() const noexcept
    {
        return waveform_;
    }
    [[nodiscard]] const sms::dsp::SamplePlaybackSettings& playback() const noexcept
    {
        return playback_;
    }
    [[nodiscard]] const sms::dsp::SampleMixerSettings& mixer() const noexcept
    {
        return mixer_;
    }

private:
    int pad_ = -1;
    bool waveformReady_ = false;
    bool playbackReady_ = false;
    bool mixerReady_ = false;
    sms::audio::WaveformSummary waveform_{};
    sms::dsp::SamplePlaybackSettings playback_{};
    sms::dsp::SampleMixerSettings mixer_{};
};

[[nodiscard]] constexpr sms::ui::InteractiveTarget
target(const InteractiveType type, const int index = -1) noexcept
{
    return {static_cast<int>(type), index};
}

[[nodiscard]] constexpr bool isTarget(const sms::ui::InteractiveTarget candidate,
                                      const InteractiveType type,
                                      const int index = -1) noexcept
{
    return candidate.is(static_cast<int>(type), index);
}

struct InteractionContext {
    bool editorMode = false;
    bool chopEditorMode = false;
    bool menuOpen = false;
    bool padContextMenuOpen = false;
    bool armed = false;
    bool fixedCapture = false;
    bool captureActive = false;
    bool chopReady = false;
    bool chopApplyEnabled = false;
    int padLayout = 0;
    sms::ui::ContextMenuGeometry padContextMenu;
    std::span<const bool> padContextMenuEnabled;
    const sms::dsp::SamplePlaybackSettings* editorSettings = nullptr;
    const sms::ui::waveform::EnvelopeGeometry* envelope = nullptr;
    std::span<const sms::audio::WaveformSummary> chopWaveforms;
    std::span<const std::int64_t> chopOffsets;
};

/** Resolve exactly one enabled interactive target using the same overlay priority as clicks. */
[[nodiscard]] inline sms::ui::InteractiveTarget
interactiveTargetAt(const sms::ui::Point point, const InteractionContext& context) noexcept
{
    namespace uiLayout = layout;

    if (context.padContextMenuOpen) {
        const int item = context.padContextMenu.hit(point);
        if (item >= 0 && item < static_cast<int>(context.padContextMenuEnabled.size()) &&
            context.padContextMenuEnabled[static_cast<std::size_t>(item)])
            return target(InteractiveType::padContextItem, item);
        return sms::ui::kNoInteractiveTarget;
    }

    if (uiLayout::menuButton.contains(point))
        return target(InteractiveType::menuButton);

    if (context.menuOpen) {
        if (!context.captureActive) {
            for (int index = 0; index < static_cast<int>(kPadLayoutCount); ++index) {
                if (uiLayout::menuOption(index).contains(point))
                    return target(InteractiveType::menuLayout, index);
            }
        }
        for (int index = 0; index < static_cast<int>(kMidiBankModeCount); ++index) {
            if (uiLayout::midiBankModeOption(index).contains(point))
                return target(InteractiveType::menuMidiBankMode, index);
        }
        return sms::ui::kNoInteractiveTarget;
    }

    const sms::ui::BankedPadLayout pads(context.padLayout);
    if (context.chopEditorMode) {
        if (context.chopApplyEnabled && uiLayout::chopApply.contains(point))
            return target(InteractiveType::chopApply);
        if (uiLayout::chopCancel.contains(point))
            return target(InteractiveType::chopCancel);
        const int boundary = chop::boundaryAt(
            point, uiLayout::chopWaveform, context.chopWaveforms, context.chopOffsets);
        if (boundary >= 0)
            return target(InteractiveType::chopBoundary, boundary);
        for (int pad = 0; context.chopReady && pad < static_cast<int>(chop::kPadCount); ++pad) {
            if (chop::adjustedFrames(context.chopWaveforms, context.chopOffsets, pad) != 0U &&
                uiLayout::chopPadButton(pad).contains(point))
                return target(InteractiveType::chopPadPreview, pad);
        }
        return sms::ui::kNoInteractiveTarget;
    }
    if (context.editorMode) {
        if (uiLayout::closeEditor.contains(point))
            return target(InteractiveType::closeEditor);
        if (!context.captureActive) {
            for (int bank = 0; bank < static_cast<int>(kBankCount); ++bank) {
                if (uiLayout::editorBank(bank).contains(point))
                    return target(InteractiveType::bank, bank);
            }
        }
        const int visualPad = pads.grid(uiLayout::editorPadBounds, 6.0f).hit(point);
        const int localPad = pads.localIndex(visualPad);
        if (localPad >= 0)
            return target(InteractiveType::pad, localPad);
        if (uiLayout::editorWaveform.contains(point) && context.editorSettings != nullptr) {
            return target(InteractiveType::regionHandle, static_cast<int>(
                sms::ui::waveform::nearestRegionHandle(
                    point.x, uiLayout::editorWaveform, *context.editorSettings)));
        }
        if (uiLayout::envelopeGraph.contains(point) && context.envelope != nullptr) {
            const auto editTarget = sms::ui::waveform::hitEnvelopeHandle(point, *context.envelope);
            if (editTarget != sms::ui::waveform::EditTarget::none)
                return target(InteractiveType::envelopeNode, static_cast<int>(editTarget));
        }
        for (int slider = 0; slider < 4; ++slider) {
            if (uiLayout::editorSlider(slider).contains(point))
                return target(InteractiveType::envelopeSlider, slider);
        }
        for (int slider = 0; slider < 3; ++slider) {
            if (uiLayout::mixerKnob(slider).contains(point))
                return target(InteractiveType::mixerKnob, slider);
        }
        if (uiLayout::playOnSelect.contains(point))
            return target(InteractiveType::playOnSelect);
        return sms::ui::kNoInteractiveTarget;
    }

    if (!context.armed && uiLayout::openEditor.contains(point))
        return target(InteractiveType::openEditor);
    if (!context.captureActive) {
        for (int bank = 0; bank < static_cast<int>(kBankCount); ++bank) {
            if (uiLayout::mainBank(bank).contains(point))
                return target(InteractiveType::bank, bank);
        }
    }
    if (!context.captureActive) {
        const int visualPad = pads.grid(uiLayout::mainPadBounds, 10.0f).hit(point);
        const int localPad = pads.localIndex(visualPad);
        if (localPad >= 0)
            return target(InteractiveType::pad, localPad);
    }

    if (uiLayout::playMode.contains(point))
        return target(InteractiveType::playMode);
    if (uiLayout::armMode.contains(point))
        return target(InteractiveType::armMode);
    if (context.armed) {
        if (uiLayout::sequentialMode.contains(point))
            return target(InteractiveType::sequentialMode);
        if (uiLayout::fixedMode.contains(point))
            return target(InteractiveType::fixedMode);
        if (context.fixedCapture && uiLayout::fixedLength.contains(point))
            return target(InteractiveType::fixedLength);
        if (uiLayout::preRoll(context.fixedCapture).contains(point))
            return target(InteractiveType::preRoll);
    } else {
        if (uiLayout::oneShotMode.contains(point))
            return target(InteractiveType::oneShotMode);
        if (uiLayout::gatedMode.contains(point))
            return target(InteractiveType::gatedMode);
        if (uiLayout::voiceLimit.contains(point))
            return target(InteractiveType::voiceLimit);
    }
    if (uiLayout::monitor.contains(point))
        return target(InteractiveType::monitor);
    for (int knob = 0; knob < 3; ++knob) {
        if (uiLayout::globalMixerKnob(knob).contains(point))
            return target(InteractiveType::globalMixerKnob, knob);
    }
    if (context.armed) {
        if (uiLayout::finalizeAction.contains(point))
            return target(InteractiveType::finalizeAction);
        if (uiLayout::undoAction.contains(point))
            return target(InteractiveType::undoAction);
        if (uiLayout::clearAction.contains(point))
            return target(InteractiveType::clearAction);
    }
    return sms::ui::kNoInteractiveTarget;
}

} // namespace midichopper::ui
