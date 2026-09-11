#pragma once

#include <cmath>
#include <cstdint>

namespace midichopper::plugin {

/** Keep repeated result codes observable through a host output parameter. */
template <class Result, std::uint32_t resultCount>
[[nodiscard]] inline constexpr float alternatingResultEventValue(
    const Result result, const bool alternateHalf) noexcept
{
    const auto code = static_cast<std::uint32_t>(result);
    return code == 0U ? 0.0f :
        static_cast<float>(code + (alternateHalf ? resultCount : 0U));
}

template <class Result, std::uint32_t resultCount>
class AlternatingResultEventTracker {
public:
    [[nodiscard]] Result consume(const float value) noexcept
    {
        if (!std::isfinite(value) || value < 0.5f ||
            value > static_cast<float>(resultCount * 2U) + 0.5f)
            return static_cast<Result>(0U);
        const auto encoded = static_cast<std::uint32_t>(value + 0.5f);
        if (encoded == lastEncoded_)
            return static_cast<Result>(0U);
        lastEncoded_ = encoded;
        return static_cast<Result>((encoded - 1U) % resultCount + 1U);
    }

private:
    std::uint32_t lastEncoded_ = 0U;
};

} // namespace midichopper::plugin
