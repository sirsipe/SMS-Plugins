#pragma once

#include "Configuration.hpp"
#include "ChopEditor.hpp"
#include "ContextMenu.hpp"
#include "Interaction.hpp"
#include "MidichopperLayout.hpp"
#include "PadLayout.hpp"
#include "WaveformEditor.hpp"

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
            if (uiLayout::chopPadButton(pad).contains(point))
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
    if (uiLayout::finalizeAction.contains(point))
        return target(InteractiveType::finalizeAction);
    if (uiLayout::undoAction.contains(point))
        return target(InteractiveType::undoAction);
    if (uiLayout::clearAction.contains(point))
        return target(InteractiveType::clearAction);
    return sms::ui::kNoInteractiveTarget;
}

} // namespace midichopper::ui
