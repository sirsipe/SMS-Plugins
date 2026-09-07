#pragma once

#include <array>
#include <cstdint>

namespace midichopper {

inline constexpr std::uint32_t kPadsPerBank = 16U;
inline constexpr std::uint32_t kBankCount = 4U;
inline constexpr std::uint32_t kPadCount = kPadsPerBank * kBankCount;
inline constexpr std::uint8_t kDefaultBaseMidiNote = 36U;
inline constexpr std::array<std::uint8_t, 3> kPadsPerBankByLayout{16U, 12U, 8U};
inline constexpr std::uint32_t kPadLayoutCount = kPadsPerBankByLayout.size();

[[nodiscard]] inline constexpr std::uint8_t padsPerBankForLayout(
    const std::uint32_t layout) noexcept
{
    const std::uint32_t index = layout < kPadLayoutCount ? layout : kPadLayoutCount - 1U;
    return kPadsPerBankByLayout[index];
}

} // namespace midichopper
