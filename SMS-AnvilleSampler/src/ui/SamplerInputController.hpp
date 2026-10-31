#pragma once

#include "ApplicationMenuController.hpp"
#include "MixerInteractionController.hpp"
#include "SamplerUiSession.hpp"
#include "WaveformEditor.hpp"

namespace midichopper::ui {

enum class InputButton { left, middle, right, other };
enum class InputKey { other, space, escape, commit, tab, backspace, deleteText };

class SamplerInputHost {
public:
    virtual ~SamplerInputHost() = default;
    virtual void focusSamplerInput() = 0;
    virtual void ignoreSamplerKeyRepeat(bool ignore) = 0;
    virtual void editSamplerParameter(std::uint32_t parameter, bool begin) = 0;
    virtual void repaintSamplerInput() = 0;
    virtual ApplicationMenuContext applicationMenuContext() const = 0;
};

/** Resolves event precedence and owns pointer/key captures, never sampler state. */
class SamplerInputController {
public:
    using Clock = std::chrono::steady_clock;
    SamplerInputController(SamplerInputHost& host, SamplerUiSession& session,
        MixerInteractionController& mixer, ApplicationMenuController& menu,
        int colorCount) noexcept;

    bool mouse(sms::ui::Point position, InputButton button, bool press,
               KnobAdjustment adjustment = KnobAdjustment::normal);
    bool motion(sms::ui::Point position);
    bool scroll(sms::ui::Point position, float delta, bool control, bool shift,
                KnobAdjustment adjustment = KnobAdjustment::normal);
    bool keyboard(InputKey key, bool press, bool backward = false);
    bool character(std::uint32_t codepoint);
    void focus(bool focused);
    void idle(Clock::time_point now);
    void invalidate(UiInteractionInvalidation reason);
    void clearHover() noexcept { static_cast<void>(fHover.clear()); }

    [[nodiscard]] sms::ui::InteractiveTarget hovered() const noexcept { return fHover.target(); }
    [[nodiscard]] bool clearArmed() const noexcept { return fClearArmed; }
    [[nodiscard]] MixerInteractionContext mixerContext() const noexcept;
    [[nodiscard]] sms::ui::InteractiveTarget targetAt(sms::ui::Point position) const noexcept;

private:
    using EditTarget = sms::ui::waveform::EditTarget;
    [[nodiscard]] sms::ui::waveform::EnvelopeGeometry envelopeGeometry() const noexcept;
    bool beginViewportDrag(sms::ui::InteractiveTarget target, sms::ui::Point position);
    void updateViewportDrag(sms::ui::Point position);
    void updateEditorDrag(sms::ui::Point position);
    void updateMainSliderDrag(float x);
    void wheelParameter(std::uint32_t parameter, float value);
    bool resetEnvelope(sms::ui::InteractiveTarget target);

    SamplerInputHost& fHost;
    SamplerUiSession& fSession;
    MixerInteractionController& fMixer;
    ApplicationMenuController& fMenu;
    const int fColorCount;
    sms::ui::HoverState fHover;
    SpaceKeyTracker fSpaceKey;
    bool fPadContextPointerCaptured = false;
    bool fResetPointerCaptured = false;
    bool fPlayButtonPressCaptured = false;
    bool fClearArmed = false;
    Clock::time_point fClearDeadline{};
    EditTarget fDragTarget = EditTarget::none;
    sms::ui::Point fDragStart{};
    sms::dsp::SamplePlaybackSettings fDragStartSettings{};
    int fViewportDrag = -1;
    float fViewportScrollGrabX = 0.0f;
    int fMainSliderDrag = -1;
};

} // namespace midichopper::ui
