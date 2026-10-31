#pragma once

#include "GlobalControlMenu.hpp"
#include "MixerValueEntry.hpp"
#include "MixerValueNavigation.hpp"
#include "DSP/SampleMixerSettings.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>

namespace midichopper::ui {

/** Current values projected by the adapter; the controller owns interaction state. */
struct MixerInteractionContext {
    int selectedPad = -1;
    sms::dsp::SampleMixerSettings mixer{};
    MixerValueEntryView view = MixerValueEntryView::main;
    float monitorGain = 0.0f;
    // Output gain, pan, tune, lowpass, highpass, filter slope, Dirty (raw host values).
    std::array<float, 7> globalRaw{};
};

enum class MixerPointerButton { left, middle, other };
enum class MixerEntryKey { other, escape, commit, tab, backspace, deleteText };
enum class MixerCancelScope { sample, global, focus, mode, all };

/** Typed effects keep DPF, preference I/O and editor snapshot bookkeeping outside. */
class MixerInteractionHost {
public:
    virtual ~MixerInteractionHost() = default;
    virtual void editMixerParameter(std::uint32_t parameter, bool begin) = 0;
    virtual void setMixerParameter(std::uint32_t parameter, float value) = 0;
    virtual void commitPadMixer(int pad, const sms::dsp::SampleMixerSettings& settings) = 0;
    virtual void saveGlobalMixerSettings(std::uint32_t parameter,
                                         const plugin::GlobalValueSettings& settings) = 0;
    virtual void setMixerStatus(const char* message) = 0;
    virtual void repaintMixer() = 0;
    virtual void focusMixer() = 0;
    virtual void clearMixerHover() noexcept = 0;
};

/** Numeric editing, mixer gestures and private global-control settings workflow.
 * SamplerInputController retains overlay and editor event precedence.
 */
class MixerInteractionController {
public:
    using Clock = std::chrono::steady_clock;
    explicit MixerInteractionController(MixerInteractionHost& host,
                                         bool stateTransport = true) noexcept;

    [[nodiscard]] const GlobalControlMenu& menu() const noexcept { return fMenu; }
    [[nodiscard]] const MixerValueEntry& entry() const noexcept { return fEntry; }
    [[nodiscard]] const auto& settings() const noexcept { return fSettings; }
    [[nodiscard]] plugin::GlobalValueSettings settings(std::uint32_t parameter) const noexcept;
    [[nodiscard]] float effective(std::uint32_t parameter, float raw) const noexcept;
    [[nodiscard]] bool faderDragging() const noexcept { return fFaderDrag >= 0; }
    [[nodiscard]] bool sampleDragging() const noexcept { return fSampleDrag >= 0; }
    [[nodiscard]] bool globalDragging() const noexcept { return fGlobalDrag >= 0; }

    bool acceptState(std::string_view key, const char* value);
    bool openMenu(sms::ui::InteractiveTarget control, sms::ui::Point position);
    void closeMenu();
    bool menuPress(MixerPointerButton button, sms::ui::InteractiveTarget clicked,
                   sms::ui::Point position, Clock::time_point now = Clock::now());
    bool faderPress(const MixerInteractionContext&, sms::ui::InteractiveTarget clicked,
                    sms::ui::Point position, Clock::time_point now = Clock::now());
    bool sampleMixerPress(const MixerInteractionContext&, sms::ui::InteractiveTarget clicked,
                          sms::ui::Point position, KnobAdjustment adjustment,
                          Clock::time_point now = Clock::now());
    bool globalMixerPress(const MixerInteractionContext&, sms::ui::InteractiveTarget clicked,
                          sms::ui::Point position, KnobAdjustment adjustment,
                          Clock::time_point now = Clock::now());
    bool resetControl(const MixerInteractionContext&, sms::ui::InteractiveTarget clicked);
    // Call these at their existing precedence points: captures, fader, then knobs.
    bool releaseCaptured(MixerPointerButton button = MixerPointerButton::left) noexcept;
    bool releaseFader();
    bool releaseKnob(const MixerInteractionContext&);
    bool motion(const MixerInteractionContext&, sms::ui::Point position);
    bool wheel(const MixerInteractionContext&, sms::ui::InteractiveTarget hovered,
               float delta, KnobAdjustment adjustment);
    bool keyboard(const MixerInteractionContext&, MixerEntryKey key,
                  bool press, bool backward = false);
    bool character(std::uint32_t codepoint);
    void hover(sms::ui::InteractiveTarget hovered);
    void idle(Clock::time_point now = Clock::now());
    void cancelEntry() noexcept { fEntry.cancel(); }
    void cancelEntryUnless(sms::ui::InteractiveTarget clicked) noexcept;
    void cancelScope(MixerCancelScope scope);
    void beginEntry(const MixerInteractionContext&, sms::ui::InteractiveTarget target);
    bool commitEntry(const MixerInteractionContext&);

private:
    static std::uint32_t levelParameter(int index) noexcept;
    static float displayScale(std::uint32_t parameter) noexcept;
    static std::uint64_t milliseconds(Clock::time_point now) noexcept;
    void commitSettings(std::uint32_t parameter, const plugin::GlobalValueSettings& settings);
    void resetRange(int bound = -1);
    void setWheelValue(std::uint32_t parameter, float value);
    void updateFader(float y);
    void updateSampleDrag(const MixerInteractionContext&, sms::ui::Point position);
    void updateGlobalDrag(sms::ui::Point position);

    MixerInteractionHost& fHost;
    const bool fStateTransport;
    std::array<plugin::GlobalValueSettings, 5> fSettings = plugin::defaultGlobalValueSettings();
    GlobalControlMenu fMenu;
    Clock::time_point fRangeResetDeadline{};
    MixerValueEntry fEntry;
    DoubleClickTracker fDoubleClick;
    bool fEntryPointerCaptured = false;
    bool fResetPointerCaptured = false;
    bool fMenuPointerCaptured = false;
    int fFaderDrag = -1;
    float fFaderGrabY = 0.0f;
    int fSampleDrag = -1;
    float fSampleStartY = 0.0f;
    sms::dsp::SampleMixerSettings fSampleStart{};
    KnobAdjustment fSampleAdjustment = KnobAdjustment::normal;
    int fGlobalDrag = -1;
    float fGlobalStartY = 0.0f;
    std::array<float, 7> fGlobalStart{};
    KnobAdjustment fGlobalAdjustment = KnobAdjustment::normal;
};

} // namespace midichopper::ui
