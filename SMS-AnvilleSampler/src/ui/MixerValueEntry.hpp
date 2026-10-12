#pragma once

#include "MidichopperInteraction.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string_view>

namespace midichopper::ui {

/** Numeric-entry editing state; host parameter gestures remain in the UI adapter. */
class MixerValueEntry {
public:
    [[nodiscard]] sms::ui::InteractiveTarget target() const noexcept { return fTarget; }
    [[nodiscard]] int pad() const noexcept { return fPad; }
    [[nodiscard]] const char* text() const noexcept { return fText.data(); }

    void begin(const sms::ui::InteractiveTarget target, const int pad,
               const float displayedValue) noexcept
    {
        std::snprintf(fText.data(), fText.size(), "%.6g", displayedValue);
        fLength = std::strlen(fText.data());
        fTarget = target;
        fPad = pad;
        fReplaceOnType = true;
    }

    void cancel() noexcept
    {
        fTarget = sms::ui::kNoInteractiveTarget;
        clear();
        fPad = -1;
    }

    void clear() noexcept
    {
        fLength = 0U;
        fText[0] = '\0';
        fReplaceOnType = false;
    }

    void backspace() noexcept
    {
        if (fReplaceOnType)
            clear();
        else if (fLength > 0U)
            fText[--fLength] = '\0';
    }

    /** Returns true only when an accepted character changes the text. */
    [[nodiscard]] bool type(const std::uint32_t codepoint) noexcept
    {
        if (codepoint > 0x7fU)
            return false;
        const char character = static_cast<char>(codepoint);
        const bool digit = character >= '0' && character <= '9';
        const bool decimalPoint = character == '.';
        const bool sign = character == '+' || character == '-';
        if (!digit && !decimalPoint && !sign)
            return false;
        if (fReplaceOnType)
            clear();
        const std::string_view current{fText.data(), fLength};
        if ((decimalPoint && current.find('.') != std::string_view::npos) ||
            (sign && fLength != 0U) || fLength + 1U >= fText.size())
            return false;
        fText[fLength++] = character;
        fText[fLength] = '\0';
        return true;
    }

    [[nodiscard]] std::optional<float> value(const float displayScale,
                                            const float minimum,
                                            const float maximum) const noexcept
    {
        return mixerValueFromText({fText.data(), fLength}, displayScale, minimum, maximum);
    }

private:
    sms::ui::InteractiveTarget fTarget{};
    std::array<char, 24> fText{};
    std::size_t fLength = 0U;
    int fPad = -1;
    bool fReplaceOnType = false;
};

} // namespace midichopper::ui
