#pragma once

#include "Configuration.hpp"
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
    openEditor,
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
    bool menuOpen = false;
    bool padContextMenuOpen = false;
    bool armed = false;
    bool captureActive = false;
    int padLayout = 0;
    sms::ui::ContextMenuGeometry padContextMenu;
    std::span<const bool> padContextMenuEnabled;
    const sms::dsp::SamplePlaybackSettings* editorSettings = nullptr;
    const sms::ui::waveform::EnvelopeGeometry* envelope = nullptr;
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

    struct FixedTarget {
        sms::ui::Rect bounds;
        InteractiveType type;
    };
    const FixedTarget fixedTargets[] = {
        {uiLayout::playMode, InteractiveType::playMode},
        {uiLayout::armMode, InteractiveType::armMode},
        {uiLayout::sequentialMode, InteractiveType::sequentialMode},
        {uiLayout::fixedMode, InteractiveType::fixedMode},
        {uiLayout::fixedLength, InteractiveType::fixedLength},
        {uiLayout::oneShotMode, InteractiveType::oneShotMode},
        {uiLayout::gatedMode, InteractiveType::gatedMode},
        {uiLayout::voiceLimit, InteractiveType::voiceLimit},
        {uiLayout::preRoll, InteractiveType::preRoll},
        {uiLayout::monitor, InteractiveType::monitor},
        {uiLayout::finalizeAction, InteractiveType::finalizeAction},
        {uiLayout::undoAction, InteractiveType::undoAction},
        {uiLayout::clearAction, InteractiveType::clearAction},
    };
    for (const auto& item : fixedTargets) {
        if (item.bounds.contains(point))
            return target(item.type);
    }
    return sms::ui::kNoInteractiveTarget;
}

} // namespace midichopper::ui
