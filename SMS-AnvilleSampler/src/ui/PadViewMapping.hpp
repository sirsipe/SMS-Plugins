#pragma once

#include "ViewRenderContext.hpp"
#include "PadLayout.hpp"

#include <algorithm>
#include <cstdio>

namespace midichopper::ui::rendering {

[[nodiscard]] inline MidiBankMode midiBankMode(const ViewRenderContext& ctx) noexcept
{
    return ctx.state.midiBankMode == 0 ? MidiBankMode::SelectedBank : MidiBankMode::AllBanks;
}

[[nodiscard]] inline int globalPad(const ViewRenderContext& ctx, const int localPad) noexcept
{
    const sms::ui::BankedPadLayout pads(ctx.state.layout);
    return ctx.state.bank *
               static_cast<int>(bankStride(static_cast<std::uint8_t>(pads.visiblePadCount()),
                                           midiBankMode(ctx))) +
           localPad;
}

[[nodiscard]] inline int bankForGlobalPad(const ViewRenderContext& ctx, const int pad) noexcept
{
    const sms::ui::BankedPadLayout pads(ctx.state.layout);
    return static_cast<int>(bankForPad(static_cast<std::uint32_t>(std::max(pad, 0)),
                                       static_cast<std::uint8_t>(pads.visiblePadCount()),
                                       midiBankMode(ctx)));
}

[[nodiscard]] inline int localPadForGlobalPad(const ViewRenderContext& ctx, const int pad) noexcept
{
    const sms::ui::BankedPadLayout pads(ctx.state.layout);
    return static_cast<int>(localPadInBank(static_cast<std::uint32_t>(std::max(pad, 0)),
                                           static_cast<std::uint8_t>(pads.visiblePadCount()),
                                           midiBankMode(ctx)));
}

[[nodiscard]] inline int mappedMidiNote(const ViewRenderContext& ctx, const int pad) noexcept
{
    return static_cast<int>(midiNoteForPad(static_cast<std::uint32_t>(pad),
                                           static_cast<std::uint8_t>(ctx.state.baseMidiNote),
                                           midiBankMode(ctx)));
}

[[nodiscard]] inline sms::ui::PadGridLayout mainGrid(const ViewRenderContext& ctx) noexcept
{
    const sms::ui::BankedPadLayout pads(ctx.state.layout);
    return pads.grid(layout::mainPadBounds, 10.0f);
}

[[nodiscard]] inline sms::ui::PadGridLayout editorGrid(const ViewRenderContext& ctx) noexcept
{
    const sms::ui::BankedPadLayout pads(ctx.state.layout);
    return pads.grid(layout::editorPadBounds, 6.0f);
}

inline void midiName(const int note, char* const destination, const std::size_t destinationSize)
{
    static constexpr const char* names[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                            "F#", "G",  "G#", "A",  "A#", "B"};
    if (note < 0 || note > 127) {
        std::snprintf(destination, destinationSize, "--");
        return;
    }
    std::snprintf(destination, destinationSize, "%s%d", names[note % 12], note / 12 - 1);
}

} // namespace midichopper::ui::rendering
