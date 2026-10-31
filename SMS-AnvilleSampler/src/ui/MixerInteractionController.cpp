#include "MixerInteractionController.hpp"
#include "../plugin/StateKeys.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace midichopper::ui {
using namespace plugin;
namespace uiLayout = layout;
namespace {
constexpr auto kRangeResetTimeout = std::chrono::seconds(2);
}

void MixerInteractionController::beginEntry(const MixerInteractionContext& context, sms::ui::InteractiveTarget target)
{
    auto mixer = context.mixer;
    target = midichopper::ui::mixerValueEntryTargetForControl(target);
    if (!midichopper::ui::isMixerValueLabel(target) ||
        target.index < 0 || target.index >= 5)
        return;

    float displayedValue = 0.0f;
    int entryPad = -1;
    if (midichopper::ui::isTarget(target, midichopper::ui::InteractiveType::levelValueLabel)) {
        displayedValue = target.index == 0 ? context.monitorGain
            : effective(kParameterOutputGainDb, context.globalRaw[0]);
    } else if (midichopper::ui::isTarget(
            target, midichopper::ui::InteractiveType::globalMixerValueLabel)) {
        switch (target.index) {
        case 0: displayedValue = effective(kParameterOutputGainDb, context.globalRaw[0]); break;
        case 1: displayedValue = effective(kParameterGlobalPan, context.globalRaw[1]) * 100.0f; break;
        case 2: displayedValue = effective(kParameterGlobalTuneSemitones, context.globalRaw[2]); break;
        case 3: displayedValue = effective(kParameterGlobalLowpass, context.globalRaw[3]) * 100.0f; break;
        case 4: displayedValue = effective(kParameterGlobalHighpass, context.globalRaw[4]) * 100.0f; break;
        default: return;
        }
    } else {
        switch (target.index) {
        case 0: displayedValue = mixer.gainDecibels; break;
        case 1: displayedValue = mixer.pan * 100.0f; break;
        case 2: displayedValue = mixer.tuneSemitones; break;
        case 3: displayedValue = mixer.lowpass * 100.0f; break;
        case 4: displayedValue = mixer.highpass * 100.0f; break;
        default: return;
        }
        entryPad = context.selectedPad;
    }

    fEntry.begin(target, entryPad, displayedValue);
    fHost.setMixerStatus("");
    // Embedded hosts can leave keyboard focus on their own proxy window
    // after the double-click, so explicitly focus the DGL child view.
    fHost.focusMixer();
    fHost.repaintMixer();
}

bool MixerInteractionController::commitEntry(const MixerInteractionContext& context)
{
    auto mixer = context.mixer;
    const auto target = fEntry.target();
    if (midichopper::ui::isTarget(target,
            midichopper::ui::InteractiveType::globalRangeEntry) && fMenu.active()) {
        const auto parameter = fMenu.parameter;
        auto settings = this->settings(parameter);
        const auto value = fEntry.value(displayScale(parameter),
            -std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
        if (!value) {
            fHost.setMixerStatus("Enter a numeric range limit");
            return false;
        }
        if (target.index == 0) settings.minimum = *value;
        else settings.maximum = *value;
        if (settings.mode == GlobalValueMode::zeroSnap &&
            (settings.minimum > 0.0f || settings.maximum < 0.0f))
            settings.mode = GlobalValueMode::normal;
        if (!validGlobalValueSettings(parameter, settings)) {
            const auto range = parameterRange(parameter);
            if (settings.minimum < range.minimum || settings.maximum > range.maximum ||
                settings.minimum >= settings.maximum)
                fHost.setMixerStatus("Keep limits within the allowed range, with minimum below maximum");
            else if (settings.mode == GlobalValueMode::zeroSnap)
                fHost.setMixerStatus("Zero Snap requires a range that includes zero");
            else
                fHost.setMixerStatus("The range must include a quantized step");
            return false;
        }
        commitSettings(parameter, settings);
        fEntry.cancel();
        fHost.repaintMixer();
        return true;
    }
    if (!midichopper::ui::isMixerValueLabel(target) ||
        target.index < 0 || target.index >= 5)
        return false;

    float minimum = 0.0f;
    float maximum = 1.0f;
    float displayScale = (target.index == 1 || target.index == 3 ||
                          target.index == 4) ? 0.01f : 1.0f;
    std::uint32_t parameter = kParameterOutputGainDb;
    const bool level = midichopper::ui::isTarget(
        target, midichopper::ui::InteractiveType::levelValueLabel);
    const bool global = level || midichopper::ui::isTarget(
        target, midichopper::ui::InteractiveType::globalMixerValueLabel);
    if (level) {
        minimum = parameterRanges::outputGainDb.minimum;
        maximum = parameterRanges::outputGainDb.maximum;
        displayScale = 1.0f;
        parameter = levelParameter(target.index);
    } else if (global) {
        switch (target.index) {
        case 0:
            minimum = parameterRanges::outputGainDb.minimum;
            maximum = parameterRanges::outputGainDb.maximum;
            parameter = kParameterOutputGainDb;
            break;
        case 1:
            minimum = parameterRanges::globalPan.minimum;
            maximum = parameterRanges::globalPan.maximum;
            parameter = kParameterGlobalPan;
            break;
        case 2:
            minimum = parameterRanges::globalTuneSemitones.minimum;
            maximum = parameterRanges::globalTuneSemitones.maximum;
            parameter = kParameterGlobalTuneSemitones;
            break;
        case 3:
            parameter = kParameterGlobalLowpass;
            break;
        case 4:
            parameter = kParameterGlobalHighpass;
            break;
        default: return false;
        }
    } else {
        if (fEntry.pad() != context.selectedPad) {
            fEntry.cancel();
            fHost.repaintMixer();
            return false;
        }
        switch (target.index) {
        case 0:
            minimum = sms::dsp::kMinimumSampleGainDecibels;
            maximum = sms::dsp::kMaximumSampleGainDecibels;
            break;
        case 1:
            minimum = sms::dsp::kMinimumSamplePan;
            maximum = sms::dsp::kMaximumSamplePan;
            break;
        case 2:
            minimum = sms::dsp::kMinimumTuneSemitones;
            maximum = sms::dsp::kMaximumTuneSemitones;
            break;
        case 3:
        case 4:
            break;
        default: return false;
        }
    }

    if (global && globalControlIndex(parameter) < fSettings.size()) {
        const auto settings = this->settings(parameter);
        minimum = settings.minimum;
        maximum = settings.maximum;
    }
    const auto value = fEntry.value(displayScale, minimum, maximum);
    if (!value) {
        fHost.setMixerStatus("Enter a number within the control's range");
        return false;
    }

    fEntry.cancel();
    if (global) {
        fHost.editMixerParameter(parameter, true);
        fHost.setMixerParameter(parameter, globalControlIndex(parameter) < fSettings.size()
            ? rawGlobalValue(parameter, *value, settings(parameter)) : *value);
        fHost.editMixerParameter(parameter, false);
    } else {
        switch (target.index) {
        case 0: mixer.gainDecibels = *value; break;
        case 1: mixer.pan = *value; break;
        case 2: mixer.tuneSemitones = *value; break;
        case 3: mixer.lowpass = *value; break;
        case 4: mixer.highpass = *value; break;
        default: return false;
        }
        fHost.commitPadMixer(context.selectedPad, mixer);
    }
    fHost.repaintMixer();
    return true;
}

bool MixerInteractionController::resetControl(const MixerInteractionContext& context, const sms::ui::InteractiveTarget clicked)
{
    auto mixer = context.mixer;
    if (midichopper::ui::isTarget(clicked, midichopper::ui::InteractiveType::levelFader) ||
        midichopper::ui::isTarget(clicked, midichopper::ui::InteractiveType::levelValueLabel)) {
        setWheelValue(levelParameter(clicked.index), clicked.index == 0 ? 0.0f
            : rawGlobalValue(kParameterOutputGainDb, 0.0f,
                settings(kParameterOutputGainDb)));
        fHost.repaintMixer();
        fResetPointerCaptured = true;
        return true;
    }
    if (midichopper::ui::isTarget(
            clicked, midichopper::ui::InteractiveType::globalMixerKnob) ||
        midichopper::ui::isTarget(
            clicked, midichopper::ui::InteractiveType::globalMixerValueLabel)) {
        switch (clicked.index) {
        case 0: fHost.setMixerParameter(kParameterOutputGainDb, rawGlobalValue(kParameterOutputGainDb, 0.0f, settings(kParameterOutputGainDb))); break;
        case 1: fHost.setMixerParameter(kParameterGlobalPan, rawGlobalValue(kParameterGlobalPan, 0.0f, settings(kParameterGlobalPan))); break;
        case 2: fHost.setMixerParameter(kParameterGlobalTuneSemitones, rawGlobalValue(kParameterGlobalTuneSemitones, 0.0f, settings(kParameterGlobalTuneSemitones))); break;
        case 3: fHost.setMixerParameter(kParameterGlobalLowpass, rawGlobalValue(kParameterGlobalLowpass, 0.0f, settings(kParameterGlobalLowpass))); break;
        case 4: fHost.setMixerParameter(kParameterGlobalHighpass, rawGlobalValue(kParameterGlobalHighpass, 0.0f, settings(kParameterGlobalHighpass))); break;
        case 5: fHost.setMixerParameter(kParameterGlobalFilterSlope, 1.0f); break;
        case 6: fHost.setMixerParameter(kParameterGlobalDirty, 0.0f); break;
        default: return false;
        }
        fHost.repaintMixer();
        fResetPointerCaptured = true;
        return true;
    }
    if (midichopper::ui::isTarget(
            clicked, midichopper::ui::InteractiveType::mixerKnob) ||
        midichopper::ui::isTarget(
            clicked, midichopper::ui::InteractiveType::mixerValueLabel)) {
        midichopper::ui::resetMixerKnob(mixer, clicked.index);
        fHost.commitPadMixer(context.selectedPad, mixer);
        fHost.repaintMixer();
        fResetPointerCaptured = true;
        return true;
    }
    return false;
}

void MixerInteractionController::updateGlobalDrag(const sms::ui::Point position)
{
    const float x = position.x;
    const float y = position.y;
    if (fGlobalDrag == 5) {
        fHost.setMixerParameter(kParameterGlobalFilterSlope,
            midichopper::ui::slopeValueAtX(x, uiLayout::globalMixerKnob(5)));
        return;
    }
    if (fGlobalDrag < 0 || fGlobalDrag >= 5)
        return;
    float startValue = 0.0f;
    float steppedIncrement = 1.0f;
    std::uint32_t parameter = kParameterOutputGainDb;
    switch (fGlobalDrag) {
    case 0:
        startValue = fGlobalStart[0];
        parameter = kParameterOutputGainDb;
        break;
    case 1:
        startValue = fGlobalStart[1];
        steppedIncrement = 0.1f;
        parameter = kParameterGlobalPan;
        break;
    case 2:
        startValue = fGlobalStart[2];
        parameter = kParameterGlobalTuneSemitones;
        break;
    case 3:
        startValue = fGlobalStart[3];
        steppedIncrement = 0.05f;
        parameter = kParameterGlobalLowpass;
        break;
    case 4:
        startValue = fGlobalStart[4];
        steppedIncrement = 0.05f;
        parameter = kParameterGlobalHighpass;
        break;
    default:
        return;
    }
    fHost.setMixerParameter(parameter, midichopper::ui::globalKnobDraggedValue(
        parameter, startValue, fGlobalStartY, y, steppedIncrement,
        fGlobalAdjustment, settings(parameter)));
}

void MixerInteractionController::updateSampleDrag(const MixerInteractionContext& context, const sms::ui::Point position)
{
    auto mixer = context.mixer;
    const float x = position.x;
    const float y = position.y;
    if (fSampleDrag == 5) {
        mixer.filterSlope = midichopper::ui::slopeValueAtX(
            x, uiLayout::mixerKnob(5));
        fHost.commitPadMixer(context.selectedPad, mixer);
        fHost.repaintMixer();
        return;
    }
    if (fSampleDrag < 0 || fSampleDrag >= 5)
        return;
    float startValue = 0.0f;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float steppedIncrement = 1.0f;
    switch (fSampleDrag) {
    case 0:
        startValue = fSampleStart.gainDecibels;
        minimum = sms::dsp::kMinimumSampleGainDecibels;
        maximum = sms::dsp::kMaximumSampleGainDecibels;
        break;
    case 1:
        startValue = fSampleStart.pan;
        minimum = sms::dsp::kMinimumSamplePan;
        maximum = sms::dsp::kMaximumSamplePan;
        steppedIncrement = 0.1f;
        break;
    case 2:
        startValue = fSampleStart.tuneSemitones;
        minimum = sms::dsp::kMinimumTuneSemitones;
        maximum = sms::dsp::kMaximumTuneSemitones;
        break;
    case 3:
        startValue = fSampleStart.lowpass;
        steppedIncrement = 0.05f;
        break;
    case 4:
        startValue = fSampleStart.highpass;
        steppedIncrement = 0.05f;
        break;
    default:
        return;
    }
    const float adjusted = midichopper::ui::knobDraggedValue(
        startValue, fSampleStartY, y, minimum, maximum,
        steppedIncrement, fSampleAdjustment);
    switch (fSampleDrag) {
    case 0:
        mixer.gainDecibels = adjusted;
        break;
    case 1:
        mixer.pan = adjusted;
        break;
    case 2:
        mixer.tuneSemitones = adjusted;
        break;
    case 3: mixer.lowpass = adjusted; break;
    case 4: mixer.highpass = adjusted; break;
    default:
        return;
    }
    mixer = sms::dsp::sanitize(mixer);
    fHost.commitPadMixer(context.selectedPad, mixer);
    fHost.repaintMixer();
}

MixerInteractionController::MixerInteractionController(MixerInteractionHost& host,
                                                       const bool stateTransport) noexcept
    : fHost(host), fStateTransport(stateTransport) {}

GlobalValueSettings MixerInteractionController::settings(const std::uint32_t parameter) const noexcept
{
    const auto index = globalControlIndex(parameter);
    return index < fSettings.size() ? fSettings[index] : defaultGlobalValueSettings(parameter);
}

float MixerInteractionController::effective(const std::uint32_t parameter, const float raw) const noexcept
{
    return effectiveGlobalValue(parameter, raw, settings(parameter));
}

std::uint32_t MixerInteractionController::levelParameter(const int index) noexcept
{
    return index == 0 ? kParameterMonitorGainDb : kParameterOutputGainDb;
}

float MixerInteractionController::displayScale(const std::uint32_t parameter) noexcept
{
    return parameter == kParameterGlobalPan || parameter == kParameterGlobalLowpass ||
        parameter == kParameterGlobalHighpass ? 0.01f : 1.0f;
}

std::uint64_t MixerInteractionController::milliseconds(const Clock::time_point now) noexcept
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count());
}

bool MixerInteractionController::acceptState(const std::string_view key, const char* value)
{
    if (!fStateTransport || value == nullptr)
        return false;
    for (std::size_t i = 0; i < kGlobalControlStateKeys.size(); ++i) {
        if (key != kGlobalControlStateKeys[i])
            continue;
        if (decodeGlobalValueSettings(kGlobalValueControls[i].parameter, value, fSettings[i]))
            fHost.repaintMixer();
        return true;
    }
    return false;
}

void MixerInteractionController::commitSettings(const std::uint32_t parameter,
                                                 const GlobalValueSettings& value)
{
    const auto index = globalControlIndex(parameter);
    if (index >= fSettings.size() || !validGlobalValueSettings(parameter, value))
        return;
    fSettings[index] = value;
    fMenu.rangeResetArmed = false;
    fHost.setMixerStatus("");
    // The adapter persists defaults even when private-state transport is unavailable.
    fHost.saveGlobalMixerSettings(parameter, value);
    fHost.repaintMixer();
}

void MixerInteractionController::resetRange(const int bound)
{
    const auto value = fMenu.originalRangeSettings(settings(fMenu.parameter), bound);
    fEntry.cancel();
    commitSettings(fMenu.parameter, value);
}

bool MixerInteractionController::openMenu(const sms::ui::InteractiveTarget control,
                                           const sms::ui::Point position)
{
    const auto parameter = globalValueParameterForTarget(control);
    if (parameter == kParameterCount)
        return false;
    fEntry.cancel();
    fMenu.open(parameter, position);
    fHost.clearMixerHover();
    fHost.repaintMixer();
    return true;
}

void MixerInteractionController::closeMenu()
{
    if (!fMenu.active())
        return;
    if (isTarget(fEntry.target(), InteractiveType::globalRangeEntry))
        fEntry.cancel();
    fMenu.close();
    fHost.clearMixerHover();
    fHost.repaintMixer();
}

bool MixerInteractionController::menuPress(const MixerPointerButton button,
                                            const sms::ui::InteractiveTarget clicked,
                                            const sms::ui::Point position,
                                            const Clock::time_point now)
{
    if (!fMenu.active())
        return false;
    if (button == MixerPointerButton::middle && isTarget(clicked, InteractiveType::globalRangeEntry)) {
        resetRange(clicked.index);
        fResetPointerCaptured = true;
        return true;
    }
    if (button != MixerPointerButton::left) {
        closeMenu();
        return true;
    }
    fMenuPointerCaptured = true;
    if (isTarget(clicked, InteractiveType::globalSettingsGroup)) {
        fEntry.cancel();
        fMenu.select(clicked.index == 1 ? GlobalSettingsSubmenu::valueMode : GlobalSettingsSubmenu::range);
        fHost.repaintMixer();
    } else if (isTarget(clicked, InteractiveType::valueModeItem)) {
        auto value = settings(fMenu.parameter);
        const auto selected = static_cast<GlobalValueMode>(clicked.index);
        const bool closeAfterSelection = value.mode == selected;
        value.mode = selected;
        commitSettings(fMenu.parameter, value);
        if (closeAfterSelection)
            closeMenu();
    } else if (isTarget(clicked, InteractiveType::globalRangeReset)) {
        fEntry.cancel();
        if (fMenu.rangeResetArmed && now < fRangeResetDeadline) {
            resetRange();
        } else {
            fMenu.rangeResetArmed = true;
            fRangeResetDeadline = now + kRangeResetTimeout;
            fHost.repaintMixer();
        }
    } else if (isTarget(clicked, InteractiveType::globalRangeEntry)) {
        fMenu.rangeResetArmed = false;
        if (fEntry.target() != clicked) {
            const auto value = settings(fMenu.parameter);
            fEntry.begin(clicked, -1, (clicked.index == 0 ? value.minimum : value.maximum) /
                displayScale(fMenu.parameter));
            fHost.repaintMixer();
        }
    } else if (!fMenu.menu.bounds().contains(position) &&
               (fMenu.group == GlobalSettingsSubmenu::none || !fMenu.submenu.bounds().contains(position))) {
        closeMenu();
    }
    return true;
}

void MixerInteractionController::cancelEntryUnless(const sms::ui::InteractiveTarget clicked) noexcept
{
    if (fEntry.target().valid() && clicked != fEntry.target())
        fEntry.cancel();
}

bool MixerInteractionController::faderPress(const MixerInteractionContext& context,
                                            const sms::ui::InteractiveTarget clicked,
                                            const sms::ui::Point position,
                                            const Clock::time_point now)
{
    if (!isTarget(clicked, InteractiveType::levelFader) && !isTarget(clicked, InteractiveType::levelValueLabel))
        return false;
    const bool doubleClick = fDoubleClick.press(clicked, position, milliseconds(now));
    if (isTarget(clicked, InteractiveType::levelValueLabel)) {
        if (doubleClick)
            beginEntry(context, clicked);
    } else if (doubleClick) {
        beginEntry(context, clicked);
        fEntryPointerCaptured = true;
    } else {
        fFaderDrag = clicked.index;
        const auto bounds = layout::levelFader(clicked.index);
        const auto track = layout::levelFaderTrack(bounds);
        const auto valueSettings = settings(levelParameter(clicked.index));
        const auto range = ParameterRange{0.0f, valueSettings.minimum, valueSettings.maximum};
        const float value = clicked.index == 0 ? context.monitorGain
            : effective(kParameterOutputGainDb, context.globalRaw[0]);
        const float capY = track.y + track.height *
            (1.0f - (value - range.minimum) / (range.maximum - range.minimum));
        fFaderGrabY = clicked.index == 0
            ? (std::abs(position.y - capY) <= 17.0f ? position.y - capY : 0.0f)
            : globalLevelFaderGrabY(kParameterOutputGainDb, context.globalRaw[0],
                position.y, bounds, valueSettings);
        fHost.editMixerParameter(levelParameter(clicked.index), true);
        updateFader(position.y);
    }
    return true;
}

bool MixerInteractionController::sampleMixerPress(const MixerInteractionContext& context,
                                                  const sms::ui::InteractiveTarget clicked,
                                                  const sms::ui::Point position,
                                                  const KnobAdjustment adjustment,
                                                  const Clock::time_point now)
{
    const bool knob = isTarget(clicked, InteractiveType::mixerKnob);
    const bool label = isTarget(clicked, InteractiveType::mixerValueLabel);
    if ((knob || label) && clicked.index >= 5) {
        if (clicked.index == 5) {
            fSampleDrag = 5;
            updateSampleDrag(context, position);
        } else {
            auto mixer = context.mixer;
            mixer.dirty = mixer.dirty >= 0.5f ? 0.0f : 1.0f;
            fHost.commitPadMixer(context.selectedPad, sms::dsp::sanitize(mixer));
        }
        fHost.repaintMixer();
        return true;
    }
    if (label) {
        if (clicked == fEntry.target())
            return true;
        if (fDoubleClick.press(clicked, position, milliseconds(now))) {
            fSampleDrag = -1;
            beginEntry(context, clicked);
            return true;
        }
    } else if (knob) {
        if (fDoubleClick.press(clicked, position, milliseconds(now))) {
            fSampleDrag = -1;
            beginEntry(context, clicked);
            fEntryPointerCaptured = true;
            return true;
        }
    } else {
        static_cast<void>(fDoubleClick.press(sms::ui::kNoInteractiveTarget, position, milliseconds(now)));
        return false;
    }
    fSampleDrag = clicked.index;
    fSampleStartY = position.y;
    fSampleStart = context.mixer;
    fSampleAdjustment = adjustment;
    return true;
}

bool MixerInteractionController::globalMixerPress(const MixerInteractionContext& context,
                                                  const sms::ui::InteractiveTarget clicked,
                                                  const sms::ui::Point position,
                                                  const KnobAdjustment adjustment,
                                                  const Clock::time_point now)
{
    const bool knob = isTarget(clicked, InteractiveType::globalMixerKnob);
    const bool label = isTarget(clicked, InteractiveType::globalMixerValueLabel);
    if ((knob || label) && clicked.index >= 5) {
        if (clicked.index == 5) {
            fGlobalDrag = 5;
            fHost.editMixerParameter(kParameterGlobalFilterSlope, true);
            updateGlobalDrag(position);
        } else {
            fHost.editMixerParameter(kParameterGlobalDirty, true);
            fHost.setMixerParameter(kParameterGlobalDirty, context.globalRaw[6] >= 0.5f ? 0.0f : 1.0f);
            fHost.editMixerParameter(kParameterGlobalDirty, false);
        }
        fHost.repaintMixer();
        return true;
    }
    if (label) {
        if (clicked == fEntry.target())
            return true;
        if (fDoubleClick.press(clicked, position, milliseconds(now))) {
            fGlobalDrag = -1;
            beginEntry(context, clicked);
            return true;
        }
    } else if (knob) {
        if (fDoubleClick.press(clicked, position, milliseconds(now))) {
            fGlobalDrag = -1;
            beginEntry(context, clicked);
            fEntryPointerCaptured = true;
            return true;
        }
    } else {
        static_cast<void>(fDoubleClick.press(sms::ui::kNoInteractiveTarget, position, milliseconds(now)));
        return false;
    }
    fGlobalDrag = clicked.index;
    fGlobalStartY = position.y;
    fGlobalStart = context.globalRaw;
    fGlobalAdjustment = adjustment;
    return true;
}

void MixerInteractionController::setWheelValue(const std::uint32_t parameter, const float value)
{
    fHost.editMixerParameter(parameter, true);
    fHost.setMixerParameter(parameter, value);
    fHost.editMixerParameter(parameter, false);
}

void MixerInteractionController::updateFader(const float y)
{
    const auto parameter = levelParameter(fFaderDrag);
    auto valueSettings = this->settings(parameter);
    const float mapped = levelFaderValueAtY(y - fFaderGrabY, layout::levelFader(fFaderDrag),
        {0.0f, valueSettings.minimum, valueSettings.maximum});
    valueSettings.mode = GlobalValueMode::normal;
    fHost.setMixerParameter(parameter, parameter == kParameterMonitorGainDb ? mapped
        : rawGlobalValue(parameter, mapped, valueSettings));
}

bool MixerInteractionController::releaseCaptured(const MixerPointerButton button) noexcept
{
    if (button == MixerPointerButton::middle) {
        const bool captured = fResetPointerCaptured;
        fResetPointerCaptured = false;
        return captured;
    }
    if (button != MixerPointerButton::left)
        return false;
    if (fEntryPointerCaptured) {
        fEntryPointerCaptured = false;
        return true;
    }
    if (fResetPointerCaptured) {
        fResetPointerCaptured = false;
        return true;
    }
    if (fMenuPointerCaptured) {
        fMenuPointerCaptured = false;
        return true;
    }
    return false;
}

bool MixerInteractionController::releaseFader()
{
    if (fFaderDrag < 0)
        return false;
    fHost.editMixerParameter(levelParameter(fFaderDrag), false);
    fFaderDrag = -1;
    return true;
}

bool MixerInteractionController::releaseKnob(const MixerInteractionContext& context)
{
    if (context.view == MixerValueEntryView::sampleEditor && fSampleDrag >= 0) {
        fHost.commitPadMixer(context.selectedPad, sms::dsp::sanitize(context.mixer));
        fSampleDrag = -1;
        fHost.repaintMixer();
        return true;
    }
    if (fGlobalDrag >= 0) {
        if (fGlobalDrag == 5)
            fHost.editMixerParameter(kParameterGlobalFilterSlope, false);
        fGlobalDrag = -1;
        fHost.repaintMixer();
        return true;
    }
    return false;
}

bool MixerInteractionController::motion(const MixerInteractionContext& context, const sms::ui::Point position)
{
    if (fFaderDrag >= 0) {
        updateFader(position.y);
        return true;
    }
    if (context.view == MixerValueEntryView::sampleEditor && fSampleDrag >= 0) {
        updateSampleDrag(context, position);
        return true;
    }
    if (fGlobalDrag >= 0) {
        updateGlobalDrag(position);
        return true;
    }
    return false;
}

bool MixerInteractionController::wheel(const MixerInteractionContext& context,
                                         const sms::ui::InteractiveTarget hovered,
                                         const float delta, const KnobAdjustment adjustment)
{
    if (fMenu.active()) return true;
    auto mixer = context.mixer;
    if (midichopper::ui::isTarget(hovered, midichopper::ui::InteractiveType::levelFader) ||
        midichopper::ui::isTarget(hovered, midichopper::ui::InteractiveType::levelValueLabel)) {
        const auto range = parameterRanges::outputGainDb;
        setWheelValue(levelParameter(hovered.index),
            hovered.index == 0 ? midichopper::ui::knobWheelAdjustedValue(
                context.monitorGain, delta, 0.5f, 1.0f,
                range.minimum, range.maximum, adjustment)
            : midichopper::ui::globalKnobWheelAdjustedValue(
                kParameterOutputGainDb, context.globalRaw[0], delta, 0.5f, adjustment,
                settings(kParameterOutputGainDb)));
        return true;
    }

    if (midichopper::ui::isTarget(
            hovered, midichopper::ui::InteractiveType::mixerKnob)) {

        switch (hovered.index) {
        case 0:
            mixer.gainDecibels = midichopper::ui::knobWheelAdjustedValue(
                mixer.gainDecibels, delta, 0.5f, 1.0f,
                sms::dsp::kMinimumSampleGainDecibels,
                sms::dsp::kMaximumSampleGainDecibels, adjustment);
            break;
        case 1:
            mixer.pan = midichopper::ui::knobWheelAdjustedValue(
                mixer.pan, delta, 0.05f, 0.1f,
                sms::dsp::kMinimumSamplePan, sms::dsp::kMaximumSamplePan,
                adjustment);
            break;
        case 2:
            mixer.tuneSemitones = midichopper::ui::knobWheelAdjustedValue(
                mixer.tuneSemitones, delta, 0.25f, 1.0f,
                sms::dsp::kMinimumTuneSemitones,
                sms::dsp::kMaximumTuneSemitones, adjustment);
            break;
        case 3:
            mixer.lowpass = midichopper::ui::knobWheelAdjustedValue(
                mixer.lowpass, delta, 0.025f, 0.1f, 0.0f, 1.0f,
                adjustment);
            break;
        case 4:
            mixer.highpass = midichopper::ui::knobWheelAdjustedValue(
                mixer.highpass, delta, 0.025f, 0.1f, 0.0f, 1.0f,
                adjustment);
            break;
        case 5:
            mixer.filterSlope = std::clamp(mixer.filterSlope +
                static_cast<float>(delta > 0 ? 1 : -1), 0.0f, 2.0f);
            break;
        case 6:
            mixer.dirty = mixer.dirty >= 0.5f ? 0.0f : 1.0f;
            break;
        default:
            return false;
        }
        mixer = sms::dsp::sanitize(mixer);
        fHost.commitPadMixer(context.selectedPad, mixer);
        fHost.repaintMixer();
        return true;
    }
    if (midichopper::ui::isTarget(
            hovered, midichopper::ui::InteractiveType::globalMixerKnob)) {
        if (hovered.index >= 0 && hovered.index < 5) {
            const auto parameter = midichopper::ui::globalValueParameterForTarget(hovered);
            const std::array<float, 5> values{context.globalRaw[0], context.globalRaw[1], context.globalRaw[2],
                context.globalRaw[3], context.globalRaw[4]};
            constexpr std::array<float, 5> increments{0.5f, 0.05f, 0.25f, 0.025f, 0.025f};
            setWheelValue(parameter,
                midichopper::ui::globalKnobWheelAdjustedValue(parameter,
                    values[static_cast<std::size_t>(hovered.index)], delta,
                    increments[static_cast<std::size_t>(hovered.index)],
                    adjustment, settings(parameter)));
            return true;
        }
        switch (hovered.index) {
        case 5:
            setWheelValue(kParameterGlobalFilterSlope,
                std::clamp(context.globalRaw[5] + static_cast<float>(delta > 0 ? 1 : -1),
                    0.0f, 2.0f));
            break;
        case 6:
            setWheelValue(kParameterGlobalDirty,
                context.globalRaw[6] >= 0.5f ? 0.0f : 1.0f);
            break;
        default:
            return false;
        }
        return true;
    }
    return false;
}

bool MixerInteractionController::keyboard(const MixerInteractionContext& context,
                                           const MixerEntryKey key, const bool press,
                                           const bool backward)
{
    if (fEntry.target().valid()) {
        if (!press)
            return true;
        switch (key) {
        case MixerEntryKey::escape:
            fEntry.cancel();
            fHost.repaintMixer();
            break;
        case MixerEntryKey::commit:
            static_cast<void>(commitEntry(context));
            break;
        case MixerEntryKey::tab: {
            const auto current = fEntry.target();
            const auto next = nextMixerValueEntryTarget(current, context.view, backward);
            if (commitEntry(context)) {
                if (isTarget(current, InteractiveType::globalRangeEntry)) {
                    const auto entry = target(InteractiveType::globalRangeEntry, current.index == 0 ? 1 : 0);
                    const auto value = settings(fMenu.parameter);
                    fEntry.begin(entry, -1, (entry.index == 0 ? value.minimum : value.maximum) /
                        displayScale(fMenu.parameter));
                    fHost.repaintMixer();
                } else {
                    beginEntry(context, next);
                }
            }
            break;
        }
        case MixerEntryKey::backspace:
            fEntry.backspace();
            fHost.repaintMixer();
            break;
        case MixerEntryKey::deleteText:
            fEntry.clear();
            fHost.repaintMixer();
            break;
        case MixerEntryKey::other:
            break;
        }
        return true;
    }
    if (press && key == MixerEntryKey::escape && fMenu.active()) {
        closeMenu();
        return true;
    }
    return false;
}

bool MixerInteractionController::character(const std::uint32_t codepoint)
{
    if (!fEntry.target().valid())
        return false;
    if (fEntry.type(codepoint))
        fHost.repaintMixer();
    return true;
}

void MixerInteractionController::hover(const sms::ui::InteractiveTarget hovered)
{
    if (fMenu.active() && isTarget(hovered, InteractiveType::globalSettingsGroup)) {
        const auto group = hovered.index == 1 ? GlobalSettingsSubmenu::valueMode : GlobalSettingsSubmenu::range;
        if (group != fMenu.group) {
            fEntry.cancel();
            fMenu.select(group);
            fHost.repaintMixer();
        }
    }
}

void MixerInteractionController::idle(const Clock::time_point now)
{
    if (fMenu.rangeResetArmed && (now >= fRangeResetDeadline ||
            !fMenu.rangeResetEnabled(settings(fMenu.parameter)))) {
        fMenu.rangeResetArmed = false;
        fHost.repaintMixer();
    }
}

void MixerInteractionController::cancelScope(const MixerCancelScope scope)
{
    if (scope == MixerCancelScope::focus || scope == MixerCancelScope::mode || scope == MixerCancelScope::all)
        closeMenu();
    if (scope == MixerCancelScope::mode || scope == MixerCancelScope::all)
        fEntry.cancel();
    if (scope == MixerCancelScope::focus || scope == MixerCancelScope::all)
        static_cast<void>(releaseFader());
    if (scope == MixerCancelScope::sample || scope == MixerCancelScope::all)
        fSampleDrag = -1;
    if (scope == MixerCancelScope::global || scope == MixerCancelScope::mode || scope == MixerCancelScope::all) {
        if (fGlobalDrag == 5)
            fHost.editMixerParameter(kParameterGlobalFilterSlope, false);
        fGlobalDrag = -1;
    }
    if (scope == MixerCancelScope::all) {
        fEntryPointerCaptured = false;
        fResetPointerCaptured = false;
        fMenuPointerCaptured = false;
    }
}

} // namespace midichopper::ui
