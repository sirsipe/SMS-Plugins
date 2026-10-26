#pragma once

#include "MidichopperInteraction.hpp"

#include <array>

namespace midichopper::ui {

enum class MixerValueEntryView { main, sampleEditor, cutPoints };

[[nodiscard]] constexpr bool isMixerValueCommitKey(const std::uint32_t key) noexcept
{
    // Pinned DGL declares Pad Enter as E06A, but forwards Pugl's E070 unchanged.
    return key == 0x0dU || key == 0xe06aU || key == 0xe070U;
}

/** Both a continuous control and its label open the same numeric editor. */
[[nodiscard]] inline sms::ui::InteractiveTarget mixerValueEntryTargetForControl(
    const sms::ui::InteractiveTarget control) noexcept
{
    if (isMixerValueLabel(control))
        return control;
    if (isTarget(control, InteractiveType::levelFader) && control.index >= 0 && control.index < 2)
        return target(InteractiveType::levelValueLabel, control.index);
    if (control.index < 0 || control.index >= 5)
        return sms::ui::kNoInteractiveTarget;
    if (isTarget(control, InteractiveType::globalMixerKnob))
        return target(InteractiveType::globalMixerValueLabel, control.index);
    if (isTarget(control, InteractiveType::mixerKnob))
        return target(InteractiveType::mixerValueLabel, control.index);
    return sms::ui::kNoInteractiveTarget;
}

/** Numeric focus cycles through the continuous controls visible in the current view. */
[[nodiscard]] inline sms::ui::InteractiveTarget nextMixerValueEntryTarget(
    const sms::ui::InteractiveTarget current, const MixerValueEntryView view,
    const bool backward) noexcept
{
    std::array<sms::ui::InteractiveTarget, 7> order{};
    std::size_t size = 0U;
    order[size++] = target(InteractiveType::levelValueLabel, 0);
    if (view == MixerValueEntryView::main) {
        for (int index = 1; index <= 4; ++index)
            order[size++] = target(InteractiveType::globalMixerValueLabel, index);
    } else if (view == MixerValueEntryView::sampleEditor) {
        for (int index = 0; index < 5; ++index)
            order[size++] = target(InteractiveType::mixerValueLabel, index);
    }
    order[size++] = target(InteractiveType::levelValueLabel, 1);
    for (std::size_t i = 0U; i < size; ++i)
        if (order[i] == current)
            return order[(i + (backward ? size - 1U : 1U)) % size];
    return order[backward ? size - 1U : 0U];
}

} // namespace midichopper::ui
