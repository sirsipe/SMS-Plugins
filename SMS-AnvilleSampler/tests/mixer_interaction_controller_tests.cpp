#include "MixerInteractionController.hpp"
#include "StateKeys.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace midichopper::plugin;
using namespace midichopper::ui;
using namespace std::chrono_literals;
using Clock = MixerInteractionController::Clock;

void check(const bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool near(const float a, const float b) { return std::abs(a - b) < 0.0001f; }

struct Effect {
    std::string kind;
    std::uint32_t parameter = kParameterCount;
    float value = 0.0f;
};

struct Host final : MixerInteractionHost {
    MixerInteractionContext* context = nullptr;
    std::vector<Effect> effects;
    std::string status;
    int repaints = 0;
    int focuses = 0;
    int hoverClears = 0;
    int committedPad = -1;
    GlobalValueSettings savedSettings{};

    void editMixerParameter(const std::uint32_t parameter, const bool begin) override
    { effects.push_back({begin ? "begin" : "end", parameter}); }
    void setMixerParameter(const std::uint32_t parameter, const float value) override
    {
        effects.push_back({"set", parameter, value});
        const auto index = globalControlIndex(parameter);
        if (index < 5U) context->globalRaw[index] = value;
        else if (parameter == kParameterGlobalFilterSlope) context->globalRaw[5] = value;
        else if (parameter == kParameterGlobalDirty) context->globalRaw[6] = value;
        else if (parameter == kParameterMonitorGainDb) context->monitorGain = value;
    }
    void commitPadMixer(const int pad, const sms::dsp::SampleMixerSettings& settings) override
    {
        effects.push_back({"pad"});
        committedPad = pad;
        context->mixer = settings;
    }
    void saveGlobalMixerSettings(const std::uint32_t parameter,
                                 const GlobalValueSettings& settings) override
    {
        effects.push_back({"save", parameter});
        savedSettings = settings;
    }
    void setMixerStatus(const char* message) override { status = message; }
    void repaintMixer() override { ++repaints; }
    void focusMixer() override { ++focuses; }
    void clearMixerHover() noexcept override { ++hoverClears; }
};

struct Fixture {
    MixerInteractionContext context;
    Host host;
    MixerInteractionController mixer{host};
    Fixture() { host.context = &context; context.selectedPad = 3; }
    void text(const char* value)
    {
        mixer.keyboard(context, MixerEntryKey::deleteText, true);
        for (const char* p = value; *p; ++p)
            check(mixer.character(static_cast<unsigned char>(*p)), "numeric editor consumes input");
    }
    void restore(const std::uint32_t parameter, const GlobalValueSettings& settings)
    {
        const auto encoded = encodeGlobalValueSettings(settings);
        check(mixer.acceptState(kGlobalControlStateKeys[globalControlIndex(parameter)], encoded.c_str()),
              "global settings state was handled");
    }
    void open(const std::uint32_t parameter)
    {
        check(mixer.openMenu(target(InteractiveType::globalMixerKnob,
              static_cast<int>(globalControlIndex(parameter))), {100.0f, 100.0f}), "settings menu opens");
    }
    void menu(const InteractiveType type, const int index, const Clock::time_point now = {})
    {
        check(mixer.menuPress(MixerPointerButton::left, target(type, index), {110.0f, 110.0f}, now),
              "settings menu consumes the press");
    }
};

void effect(const Host& host, const std::size_t index, const char* kind,
            const std::uint32_t parameter, const float value = 0.0f)
{
    check(index < host.effects.size(), "expected effect exists");
    const auto& actual = host.effects[index];
    check(actual.kind == kind && actual.parameter == parameter && near(actual.value, value),
          "effect kind, parameter, value and ordering match");
}

void defaultsAndState()
{
    Fixture f;
    check(!f.mixer.entry().target().valid() && !f.mixer.menu().active() &&
          !f.mixer.faderDragging() && !f.mixer.sampleDragging() && !f.mixer.globalDragging(),
          "controller starts with all interactions inactive");
    for (const auto control : kGlobalValueControls) {
        const auto settings = f.mixer.settings(control.parameter);
        const auto range = parameterRange(control.parameter);
        check(settings.mode == GlobalValueMode::normal && settings.minimum == range.minimum &&
              settings.maximum == range.maximum, "global defaults preserve original ranges");
    }
    f.restore(kParameterGlobalPan, {GlobalValueMode::zeroSnap, -0.5f, 0.75f});
    const auto before = f.mixer.settings(kParameterGlobalPan);
    const auto paints = f.host.repaints;
    check(f.mixer.acceptState("global_pan_settings", "KC1;0;2;1"), "malformed known state is consumed");
    check(f.mixer.settings(kParameterGlobalPan).minimum == before.minimum && f.host.repaints == paints,
          "malformed settings preserve current state without repaint");
    check(!f.mixer.acceptState("unrelated", "1") && !f.mixer.acceptState("global_pan_settings", nullptr),
          "unknown and null states are rejected");
    MixerInteractionController noState{f.host, false};
    check(!noState.acceptState("global_pan_settings", "KC1;0;-0.5;0.5"),
          "disabled state transport does not consume state");
    check(f.host.effects.empty(), "host restores never send settings or parameters back");
}

void numericEntry()
{
    Fixture f;
    f.restore(kParameterOutputGainDb, {GlobalValueMode::normal, -18.0f, 6.0f});
    f.mixer.beginEntry(f.context, target(InteractiveType::levelFader, 1));
    check(std::string(f.mixer.entry().text()) == "-2" && f.host.focuses == 1,
          "OUT entry displays mapped value and focuses the view");
    f.text("3");
    check(f.mixer.commitEntry(f.context), "valid global numeric value commits");
    check(f.host.effects.size() == 3U, "global numeric commit emits one balanced gesture");
    effect(f.host, 0, "begin", kParameterOutputGainDb);
    effect(f.host, 1, "set", kParameterOutputGainDb, 7.5f);
    effect(f.host, 2, "end", kParameterOutputGainDb);
    check(!f.mixer.entry().target().valid(), "successful numeric commit closes entry");

    f.host.effects.clear();
    f.mixer.beginEntry(f.context, target(InteractiveType::mixerKnob, 1));
    f.text("25");
    check(f.mixer.commitEntry(f.context) && near(f.context.mixer.pan, 0.25f) && f.host.committedPad == 3,
          "pad percentages commit through the typed pad effect");
    check(f.host.effects.size() == 1U && f.host.effects[0].kind == "pad", "pad entry sends no host gesture");
    f.host.effects.clear();
    f.mixer.beginEntry(f.context, target(InteractiveType::mixerValueLabel, 0));
    f.text("999");
    check(!f.mixer.commitEntry(f.context) && f.mixer.entry().target().valid() &&
          f.host.status == "Enter a number within the control's range" && f.host.effects.empty(),
          "invalid numeric input stays editable without committing");
    f.text("1");
    f.context.selectedPad = 4;
    check(!f.mixer.commitEntry(f.context) && !f.mixer.entry().target().valid() && f.host.effects.empty(),
          "stale pad numeric entry cannot modify a newly selected pad");
}

void keyboardNavigation()
{
    Fixture f;
    f.context.view = MixerValueEntryView::sampleEditor;
    f.mixer.beginEntry(f.context, target(InteractiveType::mixerKnob, 4));
    check(f.mixer.keyboard(f.context, MixerEntryKey::tab, true) &&
          f.mixer.entry().target() == target(InteractiveType::levelValueLabel, 1),
          "Tab commits and advances through visible sample controls");
    check(f.mixer.keyboard(f.context, MixerEntryKey::tab, true, true) &&
          f.mixer.entry().target() == target(InteractiveType::mixerValueLabel, 4),
          "Shift Tab returns to the previous visible value");
    const auto before = f.host.effects.size();
    check(f.mixer.keyboard(f.context, MixerEntryKey::commit, false) && f.host.effects.size() == before,
          "numeric key releases are consumed without committing");
    f.mixer.keyboard(f.context, MixerEntryKey::escape, true);
    check(!f.mixer.entry().target().valid() && !f.mixer.character('1') &&
          !f.mixer.keyboard(f.context, MixerEntryKey::escape, true),
          "Escape cancels entry and yields later overlay handling");
}

void knobAndWheelGestures()
{
    Fixture f;
    const auto pan = target(InteractiveType::globalMixerKnob, 1);
    check(f.mixer.globalMixerPress(f.context, pan, {100.0f, 100.0f}, KnobAdjustment::normal, {}),
          "global knob press starts a drag");
    check(f.host.effects.empty() && f.mixer.globalDragging(), "continuous global press opens no edit gesture");
    f.mixer.motion(f.context, {100.0f, 88.0f});
    f.mixer.releaseKnob(f.context);
    check(f.host.effects.size() == 1U, "global drag preserves its existing unbracketed parameter send");
    effect(f.host, 0, "set", kParameterGlobalPan, 0.2f);

    f.host.effects.clear();
    const auto slope = layout::globalMixerKnob(5);
    f.mixer.globalMixerPress(f.context, target(InteractiveType::globalMixerKnob, 5),
                            {slope.x + slope.width - 1.0f, slope.y}, KnobAdjustment::normal);
    f.mixer.releaseKnob(f.context);
    check(f.host.effects.size() == 3U, "slope drag sends balanced begin/value/end");
    effect(f.host, 0, "begin", kParameterGlobalFilterSlope);
    effect(f.host, 1, "set", kParameterGlobalFilterSlope, 2.0f);
    effect(f.host, 2, "end", kParameterGlobalFilterSlope);

    f.host.effects.clear();
    f.mixer.wheel(f.context, target(InteractiveType::globalMixerKnob, 2), 1.0f, KnobAdjustment::stepped);
    check(f.host.effects.size() == 3U, "global wheel sends a balanced gesture");
    effect(f.host, 0, "begin", kParameterGlobalTuneSemitones);
    effect(f.host, 1, "set", kParameterGlobalTuneSemitones, 1.0f);
    effect(f.host, 2, "end", kParameterGlobalTuneSemitones);
    f.host.effects.clear();
    f.mixer.wheel(f.context, target(InteractiveType::mixerKnob, 0), 1.0f, KnobAdjustment::normal);
    check(near(f.context.mixer.gainDecibels, 0.5f) && f.host.effects.size() == 1U &&
          f.host.effects[0].kind == "pad", "pad wheel updates mixer through the session");
    f.context.view = MixerValueEntryView::sampleEditor;
    f.mixer.sampleMixerPress(f.context, target(InteractiveType::mixerKnob, 1),
                            {100.0f, 100.0f}, KnobAdjustment::fine);
    f.mixer.motion(f.context, {100.0f, 40.0f});
    check(near(f.context.mixer.pan, 0.1f), "pad fine drag retains original start value and fine scale");
    f.mixer.releaseKnob(f.context);
    check(!f.mixer.sampleDragging(), "pad release commits and ends drag");
}

void staleSampleDragRoutesToGlobal()
{
    Fixture f;
    f.context.view = MixerValueEntryView::sampleEditor;
    f.mixer.sampleMixerPress(f.context, target(InteractiveType::mixerKnob, 0),
                            {100.0f, 100.0f}, KnobAdjustment::normal);
    check(f.mixer.sampleDragging(), "sample gesture starts in the editor");
    // CLEAR PADS historically leaves the drag indices intact while exiting the editor.
    f.mixer.closeMenu();
    f.mixer.cancelEntry();
    f.context.view = MixerValueEntryView::main;
    f.context.selectedPad = -1;
    f.host.effects.clear();
    check(!f.mixer.motion(f.context, {100.0f, 88.0f}) && !f.mixer.releaseKnob(f.context) &&
          f.host.effects.empty() && f.mixer.sampleDragging(),
          "stale sample gesture outside editor is ignored without committing an invalid pad");

    const auto slope = layout::globalMixerKnob(5);
    f.mixer.globalMixerPress(f.context, target(InteractiveType::globalMixerKnob, 5),
                            {slope.x, slope.y}, KnobAdjustment::normal);
    check(f.mixer.globalDragging() && f.mixer.sampleDragging(),
          "a new global drag can coexist with the preserved stale sample index");
    f.mixer.motion(f.context, {slope.x + slope.width - 1.0f, slope.y});
    f.mixer.releaseKnob(f.context);
    check(f.host.effects.size() == 4U && !f.mixer.globalDragging() && f.mixer.sampleDragging(),
          "global drag motion and release skip stale sample state and balance their gesture");
    effect(f.host, 0, "begin", kParameterGlobalFilterSlope);
    effect(f.host, 1, "set", kParameterGlobalFilterSlope, 0.0f);
    effect(f.host, 2, "set", kParameterGlobalFilterSlope, 2.0f);
    effect(f.host, 3, "end", kParameterGlobalFilterSlope);
    check(f.host.committedPad == -1, "global routing never invokes pad mixer commit");
}

void faderAndCapture()
{
    Fixture f;
    const auto bounds = layout::levelFader(0);
    const auto track = layout::levelFaderTrack(bounds);
    const sms::ui::Point atMaximum{bounds.x, track.y};
    const auto monitor = target(InteractiveType::levelFader, 0);
    f.mixer.faderPress(f.context, monitor, atMaximum, Clock::time_point{} + 1s);
    check(f.mixer.faderDragging() && f.host.effects.size() == 2U,
          "fader press begins gesture before its first value");
    effect(f.host, 0, "begin", kParameterMonitorGainDb);
    effect(f.host, 1, "set", kParameterMonitorGainDb, 12.0f);
    f.mixer.releaseFader();
    effect(f.host, 2, "end", kParameterMonitorGainDb);
    f.host.effects.clear();
    f.mixer.faderPress(f.context, monitor, atMaximum, Clock::time_point{} + 1100ms);
    check(f.mixer.entry().target() == target(InteractiveType::levelValueLabel, 0) &&
          !f.mixer.faderDragging() && f.host.effects.empty(), "second fader click opens numeric input");
    check(!f.mixer.releaseCaptured(MixerPointerButton::middle) && f.mixer.releaseCaptured() &&
          !f.mixer.releaseCaptured(), "middle release preserves one-shot numeric left capture");

    f.mixer.cancelEntry();
    check(f.mixer.resetControl(f.context, target(InteractiveType::globalMixerKnob, 5)),
          "middle reset recognizes global slope");
    check(f.host.effects.size() == 1U, "global middle reset preserves the original unbracketed send");
    effect(f.host, 0, "set", kParameterGlobalFilterSlope, 1.0f);
    check(f.mixer.releaseCaptured(MixerPointerButton::middle) &&
          !f.mixer.releaseCaptured(MixerPointerButton::middle), "middle reset capture is consumed exactly once");
    f.host.effects.clear();
    f.mixer.resetControl(f.context, monitor);
    check(f.host.effects.size() == 3U, "fader middle reset sends begin/value/end");
    effect(f.host, 0, "begin", kParameterMonitorGainDb);
    effect(f.host, 1, "set", kParameterMonitorGainDb, 0.0f);
    effect(f.host, 2, "end", kParameterMonitorGainDb);
}

void doubleClickHistory()
{
    Fixture f;
    f.context.view = MixerValueEntryView::sampleEditor;
    const auto knob = target(InteractiveType::mixerKnob, 0);
    const sms::ui::Point point{100.0f, 100.0f};
    f.mixer.sampleMixerPress(f.context, knob, point, KnobAdjustment::normal, Clock::time_point{} + 1s);
    f.mixer.releaseKnob(f.context);
    f.host.effects.clear();
    f.mixer.sampleMixerPress(f.context, knob, point, KnobAdjustment::normal, Clock::time_point{} + 1100ms);
    check(!f.mixer.sampleDragging() && f.mixer.entry().target() == target(InteractiveType::mixerValueLabel, 0) &&
          f.host.effects.empty() && f.mixer.releaseCaptured(),
          "pad knob double click opens numeric input and captures the second release");
    f.mixer.cancelEntryUnless(target(InteractiveType::mixerValueLabel, 0));
    check(f.mixer.entry().target().valid(), "clicking the current numeric label preserves entry");
    f.mixer.cancelEntryUnless(sms::ui::kNoInteractiveTarget);
    check(!f.mixer.entry().target().valid(), "outside click cancels numeric input");

    const auto pan = target(InteractiveType::globalMixerValueLabel, 1);
    f.mixer.globalMixerPress(f.context, pan, point, KnobAdjustment::normal, Clock::time_point{} + 2s);
    f.mixer.releaseKnob(f.context);
    check(!f.mixer.globalMixerPress(f.context, sms::ui::kNoInteractiveTarget, point,
                                   KnobAdjustment::normal, Clock::time_point{} + 2050ms),
          "non-mixer press is left for the input coordinator");
    f.mixer.globalMixerPress(f.context, pan, point, KnobAdjustment::normal, Clock::time_point{} + 2100ms);
    check(f.mixer.globalDragging() && !f.mixer.entry().target().valid(),
          "intervening outside click breaks the double-click sequence");
    f.mixer.releaseKnob(f.context);
    f.mixer.globalMixerPress(f.context, pan, point, KnobAdjustment::normal, Clock::time_point{} + 2200ms);
    check(!f.mixer.globalDragging() && f.mixer.entry().target() == pan && !f.mixer.releaseCaptured(),
          "value-label double click opens entry without knob pointer capture");
}

void rangeMenu()
{
    Fixture f;
    f.restore(kParameterGlobalPan, {GlobalValueMode::zeroSnap, -0.5f, 0.75f});
    f.open(kParameterGlobalPan);
    f.menu(InteractiveType::globalSettingsGroup, 2);
    f.menu(InteractiveType::globalRangeEntry, 0);
    check(std::string(f.mixer.entry().text()) == "-50", "range entry uses display percentages");
    f.text("25");
    check(f.mixer.commitEntry(f.context) && f.mixer.settings(kParameterGlobalPan).mode == GlobalValueMode::normal,
          "excluding zero through range editing changes Zero Snap to Normal");
    check(f.host.effects.size() == 1U && f.host.effects[0].kind == "save" &&
          f.context.globalRaw[1] == 0.0f, "range changes save settings and preserve raw host values");
    f.menu(InteractiveType::globalRangeEntry, 1);
    f.text("10");
    check(!f.mixer.commitEntry(f.context) && f.mixer.entry().target().valid() &&
          f.host.status == "Keep limits within the allowed range, with minimum below maximum",
          "invalid range stays open with useful status");
    f.text("60");
    f.mixer.keyboard(f.context, MixerEntryKey::tab, true);
    check(f.mixer.entry().target() == target(InteractiveType::globalRangeEntry, 0),
          "Tab commits a range limit and opens the opposite bound");
    f.host.effects.clear();
    f.menu(InteractiveType::globalRangeReset, 0, Clock::time_point{} + 1s);
    check(f.mixer.menu().rangeResetArmed && f.host.effects.empty(), "range RESET first click only arms");
    f.mixer.idle(Clock::time_point{} + 3s);
    check(!f.mixer.menu().rangeResetArmed, "range RESET confirmation expires at two seconds");
    f.menu(InteractiveType::globalRangeReset, 0, Clock::time_point{} + 4s);
    f.menu(InteractiveType::globalRangeReset, 0, Clock::time_point{} + 4100ms);
    check(f.host.effects.size() == 1U && f.mixer.settings(kParameterGlobalPan).minimum == -1.0f &&
          f.mixer.settings(kParameterGlobalPan).maximum == 1.0f,
          "confirmed range RESET restores native limits through one settings save");
    f.restore(kParameterGlobalPan, {GlobalValueMode::quantized, -0.5f, 0.5f});
    f.mixer.menuPress(MixerPointerButton::middle, target(InteractiveType::globalRangeEntry, 0), {}, {});
    check(f.mixer.settings(kParameterGlobalPan).minimum == -1.0f &&
          f.mixer.settings(kParameterGlobalPan).maximum == 0.5f &&
          f.mixer.settings(kParameterGlobalPan).mode == GlobalValueMode::quantized,
          "middle range reset restores only its bound and preserves mode");
    check(f.mixer.releaseCaptured(MixerPointerButton::middle), "range middle reset captures its release");
    f.mixer.hover(target(InteractiveType::globalSettingsGroup, 1));
    f.menu(InteractiveType::valueModeItem, static_cast<int>(GlobalValueMode::quantized));
    check(!f.mixer.menu().active(), "selecting the current value mode closes settings menu");
}

void focusAndModeCancellation()
{
    Fixture f;
    const auto monitor = layout::levelFader(0);
    f.mixer.faderPress(f.context, target(InteractiveType::levelFader, 0), {monitor.x, monitor.y}, {});
    f.open(kParameterGlobalPan);
    f.host.effects.clear();
    f.mixer.cancelScope(MixerCancelScope::focus);
    check(!f.mixer.menu().active() && !f.mixer.faderDragging(), "focus loss closes menu and ends fader");
    check(f.host.effects.size() == 1U, "focus loss balances the active fader gesture once");
    effect(f.host, 0, "end", kParameterMonitorGainDb);
    f.host.effects.clear();
    f.mixer.cancelScope(MixerCancelScope::focus);
    check(f.host.effects.empty(), "repeated focus loss emits no duplicate end gesture");

    const auto slope = layout::globalMixerKnob(5);
    f.mixer.globalMixerPress(f.context, target(InteractiveType::globalMixerKnob, 5),
                            {slope.x, slope.y}, KnobAdjustment::normal);
    f.mixer.beginEntry(f.context, target(InteractiveType::levelValueLabel, 0));
    f.host.effects.clear();
    f.mixer.cancelScope(MixerCancelScope::focus);
    check(f.mixer.globalDragging() && f.mixer.entry().target().valid() && f.host.effects.empty(),
          "focus loss preserves original global drag and non-range numeric behavior");
    f.mixer.cancelScope(MixerCancelScope::mode);
    check(!f.mixer.globalDragging() && !f.mixer.entry().target().valid() && f.host.effects.size() == 1U,
          "mode change cancels numeric entry and balances global slope gesture");
    effect(f.host, 0, "end", kParameterGlobalFilterSlope);
    f.mixer.sampleMixerPress(f.context, target(InteractiveType::mixerKnob, 0),
                            {100.0f, 100.0f}, KnobAdjustment::normal);
    f.mixer.cancelScope(MixerCancelScope::sample);
    check(!f.mixer.sampleDragging(), "leaving sample editor cancels its drag");
    f.open(kParameterGlobalPan);
    f.menu(InteractiveType::globalRangeEntry, 0);
    check(!f.mixer.releaseCaptured(MixerPointerButton::middle) && f.mixer.releaseCaptured(),
          "menu left capture survives an unrelated middle release");
    f.mixer.cancelScope(MixerCancelScope::all);
    check(!f.mixer.releaseCaptured() && !f.mixer.menu().active() && !f.mixer.entry().target().valid(),
          "full cancellation clears all interaction captures");
}
} // namespace

int main()
{
    defaultsAndState();
    numericEntry();
    keyboardNavigation();
    knobAndWheelGestures();
    staleSampleDragRoutesToGlobal();
    faderAndCapture();
    doubleClickHistory();
    rangeMenu();
    focusAndModeCancellation();
    std::cout << "Mixer interaction controller tests passed\n";
}
