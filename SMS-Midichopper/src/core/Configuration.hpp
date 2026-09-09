#pragma once

#include <array>
#include <cstdint>

namespace midichopper {

inline constexpr std::uint32_t kPadsPerBank = 16U;
inline constexpr std::uint32_t kBankCount = 4U;
inline constexpr std::uint32_t kPadCount = kPadsPerBank * kBankCount;
inline constexpr std::uint8_t kDefaultBaseMidiNote = 36U;
inline constexpr std::uint8_t kMidiNoteCount = 128U;
inline constexpr std::array<std::uint8_t, 3> kPadsPerBankByLayout{16U, 12U, 8U};
inline constexpr std::uint32_t kPadLayoutCount = kPadsPerBankByLayout.size();

enum class MidiBankMode : std::uint8_t { SelectedBank, AllBanks };
inline constexpr std::uint32_t kMidiBankModeCount = 2U;
inline constexpr MidiBankMode kDefaultMidiBankMode = MidiBankMode::AllBanks;

[[nodiscard]] inline constexpr std::uint8_t padsPerBankForLayout(
    const std::uint32_t layout) noexcept
{
    const std::uint32_t index = layout < kPadLayoutCount ? layout : kPadLayoutCount - 1U;
    return kPadsPerBankByLayout[index];
}

[[nodiscard]] inline constexpr std::uint8_t maximumBaseMidiNote(
    const MidiBankMode mode) noexcept
{
    const std::uint32_t addressedPads = mode == MidiBankMode::AllBanks
        ? kPadCount : kPadsPerBank;
    return static_cast<std::uint8_t>(kMidiNoteCount - addressedPads);
}

[[nodiscard]] inline constexpr std::uint8_t effectiveBaseMidiNote(
    const std::uint8_t baseNote, const MidiBankMode mode) noexcept
{
    return baseNote <= maximumBaseMidiNote(mode) ? baseNote : maximumBaseMidiNote(mode);
}

[[nodiscard]] inline constexpr std::uint32_t bankStride(
    const std::uint8_t padsPerBank, const MidiBankMode mode) noexcept
{
    return mode == MidiBankMode::AllBanks ? padsPerBank : kPadsPerBank;
}

[[nodiscard]] inline constexpr std::uint32_t bankForPad(
    const std::uint32_t pad, const std::uint8_t padsPerBank,
    const MidiBankMode mode) noexcept
{
    return pad / bankStride(padsPerBank, mode);
}

[[nodiscard]] inline constexpr std::uint32_t localPadInBank(
    const std::uint32_t pad, const std::uint8_t padsPerBank,
    const MidiBankMode mode) noexcept
{
    return pad % bankStride(padsPerBank, mode);
}

/** MIDI note emitted for a global pad index in the selected addressing mode. */
[[nodiscard]] inline constexpr std::uint8_t midiNoteForPad(
    const std::uint32_t pad, const std::uint8_t baseNote,
    const MidiBankMode mode) noexcept
{
    const std::uint32_t offset = mode == MidiBankMode::AllBanks
        ? pad : pad % kPadsPerBank;
    return static_cast<std::uint8_t>(effectiveBaseMidiNote(baseNote, mode) + offset);
}

/** Global sample slot addressed by a note, or kPadCount when it is unmapped. */
[[nodiscard]] inline constexpr std::uint32_t padForMidiNote(
    const std::uint8_t note, const std::uint8_t baseNote,
    const std::uint8_t activeBank, const std::uint8_t padsPerBank,
    const MidiBankMode mode) noexcept
{
    const std::uint8_t effectiveBase = effectiveBaseMidiNote(baseNote, mode);
    if (note < effectiveBase)
        return kPadCount;
    const std::uint32_t offset = static_cast<std::uint32_t>(note - effectiveBase);
    if (mode == MidiBankMode::AllBanks)
        return offset < static_cast<std::uint32_t>(padsPerBank) * kBankCount
            ? offset : kPadCount;
    if (offset >= padsPerBank || activeBank >= kBankCount)
        return kPadCount;
    return static_cast<std::uint32_t>(activeBank) * kPadsPerBank + offset;
}

} // namespace midichopper
