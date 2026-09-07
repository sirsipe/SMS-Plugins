/*
 * SMS-Midichopper - DPF/NanoVG user interface
 *
 * The UI deliberately talks to the plug-in through the small DPF contract below,
 * rather than including the sampler engine.  That keeps this file reusable by a
 * future VST3/CLAP wrapper.
 *
 * Parameter contract (the DSP/plugin side should use the same indices):
 *   0 arm (0 = play, 1 = sequential chop)
 *   1 first destination pad (1 .. 16; the UI stores it as 0 .. 15)
 *   2 pre-roll in milliseconds (0 .. 100)
 *   3 capture mode (0 = sequential boundaries, 1 = fixed length)
 *   4 fixed record length in seconds (0.01 .. 30)
 *   5 playback mode (0 = one-shot, 1 = gated)
 *   6 monitor input (0/1)
 *   7 MIDI base note (0 .. 112)
 *   8 output gain in dB (-24 .. 12)
 *   9 finalize current chop (momentary)
 *  10 undo last chop (momentary)
 *  11 clear all pads (momentary)
 *
 * State contract used by the sample editor:
 *   pad_edit_01..16  compact non-destructive cut-point and ADSR settings
 *   waveform_request selected pad index sent from UI to DSP
 *   waveform_data    compact 128-bin min/max summary returned by DSP
 *
 * The UI sends C1 + pad as MIDI note-on/off in PLAY mode only. In ARM mode a
 * clicked pad selects the first destination and the next incoming MIDI note is
 * expected to define each sequential chop boundary.
 */

// DPF's OpenGL backend can expose either its basic widget or NanoVG widget.
// This UI intentionally requests NanoVG so it remains a single source file;
// the plugin metadata may also define this macro for other UI translation units.
#ifndef DISTRHO_UI_USE_NANOVG
# define DISTRHO_UI_USE_NANOVG 1
#endif
#include "DistrhoUI.hpp"

#include "Audio/WaveformSummary.hpp"
#include "DSP/SamplePlaybackSettings.hpp"
#include "State/SamplePlaybackSettingsCodec.hpp"
#include "UI/Geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

constexpr uint kInitialWidth = 960;
constexpr uint kInitialHeight = 680;
constexpr uint kPadCount = 16;
constexpr uint kBaseNoteDefault = 36;

constexpr std::array<const char*, kPadCount> kPadEditStateKeys{{
    "pad_edit_01", "pad_edit_02", "pad_edit_03", "pad_edit_04",
    "pad_edit_05", "pad_edit_06", "pad_edit_07", "pad_edit_08",
    "pad_edit_09", "pad_edit_10", "pad_edit_11", "pad_edit_12",
    "pad_edit_13", "pad_edit_14", "pad_edit_15", "pad_edit_16",
}};

enum Parameter : uint32_t {
    kArm = 0,
    kStartPad,
    kPreRoll,
    kRecordMode,
    kFixedLength,
    kPlaybackMode,
    kMonitor,
    kBaseNote,
    kGain,
    kFinalize,
    kUndo,
    kClearAll,
};

const DGL_NAMESPACE::Color kBackground(14, 17, 24);
const DGL_NAMESPACE::Color kPanel(22, 27, 37);
const DGL_NAMESPACE::Color kPanelRaised(29, 35, 47);
const DGL_NAMESPACE::Color kBorder(51, 61, 78);
const DGL_NAMESPACE::Color kText(232, 238, 247);
const DGL_NAMESPACE::Color kMuted(143, 155, 177);
const DGL_NAMESPACE::Color kCyan(65, 214, 203);
const DGL_NAMESPACE::Color kAmber(255, 175, 84);
const DGL_NAMESPACE::Color kRed(238, 101, 112);

} // namespace

class MidichopperUI final : public UI
{
public:
    MidichopperUI()
        : UI(kInitialWidth, kInitialHeight),
          fArm(false),
          fRecordMode(0.0f),
          fFixedLength(1.0f),
          fPlaybackMode(0.0f),
          fMonitor(1.0f),
          fStartPad(0),
          fPreRoll(0.0f),
          fBaseNote(kBaseNoteDefault),
          fGain(0.0f),
          fSelectedPad(0),
          fCurrentPad(-1),
          fPressedPad(-1),
          fPressedActionParameter(-1),
          fClearArmed(false),
          fClearTicks(0),
          fEditorMode(false),
          fDragTarget(-1),
          fDragStartX(0.0f),
          fDragStartY(0.0f),
          fHasWaveform(false)
    {
        fPadState.fill('0');
        fPadStatus.fill('0');
        fStatus[0] = '\0';

#ifndef DGL_NO_SHARED_RESOURCES
        loadSharedResources();
#endif
        // This UI performs its own aspect-preserving resize transform.  DPF's
        // automatic scaling must stay disabled here: a top-level onMouse()
        // override receives native window coordinates, while automatic scaling
        // changes the widget's drawing coordinates.  Combining the two makes
        // hit targets drift on HiDPI displays and in hosts such as REAPER.
        setGeometryConstraints(700, 500, false, false);
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= 12 && index < 28)
        {
            const auto pad = static_cast<std::size_t>(index - 12);
            const bool wasOccupied = fPadState[pad] != '0';
            const bool isOccupied = value >= 0.5f;
            fPadState[pad] = isOccupied ? '1' : '0';
            if (fEditorMode && pad == static_cast<std::size_t>(fSelectedPad) &&
                wasOccupied != isOccupied)
                requestWaveform();
            repaint();
            return;
        }
        if (index >= 28 && index < 44)
        {
            const int pad = static_cast<int>(index - 28);
            const bool wasActive = fPadStatus[static_cast<std::size_t>(pad)] != '0';
            const bool isActive = value >= 0.5f;
            fPadStatus[static_cast<std::size_t>(pad)] = isActive ? '1' : '0';
            if (fEditorMode && isActive && !wasActive && pad != fSelectedPad)
                selectEditorPad(pad);
            if (fArm && isActive)
                fCurrentPad = pad;
            else if (fCurrentPad == pad && !isActive)
                fCurrentPad = -1;
            repaint();
            return;
        }
        switch (index)
        {
        case kArm:         fArm = value >= 0.5f; break;
        case kRecordMode:  fRecordMode = value; break;
        case kFixedLength: fFixedLength = value; break;
        case kPlaybackMode: fPlaybackMode = value; break;
        case kMonitor:     fMonitor = value; break;
        case kStartPad:
            fStartPad = clampPad(value - 1.0f);
            if (!fEditorMode)
                fSelectedPad = fStartPad;
            break;
        case kPreRoll:     fPreRoll = value; break;
        case kBaseNote:    fBaseNote = clampNote(value); break;
        case kGain:        fGain = value; break;
        default: return;
        }
        repaint();
    }

#if DISTRHO_PLUGIN_WANT_STATE
    void stateChanged(const char* key, const char* value) override
    {
        if (key == nullptr || value == nullptr)
            return;

        if (std::strcmp(key, "pad_mask") == 0)
            parsePadMask(value, fPadState);
        else if (std::strcmp(key, "pad_status") == 0)
            copyChars(fPadStatus, value);
        else if (std::strcmp(key, "current_pad") == 0)
            fCurrentPad = parsePad(value, -1);
        else if (std::strcmp(key, "selected_pad") == 0)
            fSelectedPad = clampPad(std::strtof(value, nullptr));
        else if (std::strcmp(key, "status") == 0)
            copyString(fStatus, value);
        else if (std::strcmp(key, "waveform_data") == 0)
        {
            sms::audio::WaveformSummary summary;
            if (sms::audio::decodeWaveformSummary(value, summary) &&
                summary.pad == static_cast<std::uint32_t>(fSelectedPad))
            {
                fWaveform = summary;
                fHasWaveform = summary.frames != 0U;
            }
        }
        else if (parseEditorState(key, value))
        {
        }
        else
            return;
        repaint();
    }
#endif

    void uiIdle() override
    {
        // Keep the two-step destructive action time-limited even when the host
        // does not repaint the UI for other reasons.
        if (fClearTicks > 0)
        {
            --fClearTicks;
            if (fClearTicks == 0)
            {
                fClearArmed = false;
                repaint();
            }
        }
    }

    void onNanoDisplay() override
    {
        const float w = static_cast<float>(getWidth());
        const float h = static_cast<float>(getHeight());
        const LayoutTransform layout = layoutTransform(w, h);

        beginPath();
        rect(0.0f, 0.0f, w, h);
        fillColor(kBackground);
        fill();

        save();
        translate(layout.offsetX, layout.offsetY);
        scale(layout.scale, layout.scale);
        drawHeader();
        if (fEditorMode)
        {
            drawSampleEditor();
            drawEditorPadPanel();
        }
        else
        {
            drawPadPanel();
            drawControlPanel();
        }
        drawFooter();
        restore();

    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        // Recompute the same transform used for painting.  Do not depend on a
        // cached resize/display value: hosts may deliver the first click before
        // the next repaint after changing the embedded window size.
        const LayoutTransform layout = layoutTransform(static_cast<float>(getWidth()),
                                                       static_cast<float>(getHeight()));
        const float x = (static_cast<float>(ev.pos.getX()) - layout.offsetX) / layout.scale;
        const float y = (static_cast<float>(ev.pos.getY()) - layout.offsetY) / layout.scale;

        if (ev.press)
        {
            if (fEditorMode)
            {
                if (hit(x, y, 690, 112, 222, 32))
                {
                    fEditorMode = false;
                    fDragTarget = -1;
                    repaint();
                    return true;
                }
                const int editorPad = editorPadGrid().hit({x, y});
                if (editorPad >= 0)
                {
                    selectEditorPad(editorPad);
                    return true;
                }
                const float startX = 46.0f + 586.0f * fEditorSettings.start;
                const float endX = 46.0f + 586.0f * fEditorSettings.end;
                if (hit(x, y, 46, 158, 586, 222))
                {
                    fDragTarget = std::abs(x - startX) <= std::abs(x - endX) ? 0 : 1;
                    updateEditorDrag(x, y);
                    return true;
                }
                if (hit(x, y, 46, 448, 250, 120))
                {
                    fDragTarget = hitEnvelopeHandle(x, y);
                    if (fDragTarget >= 0)
                    {
                        fDragStartX = x;
                        fDragStartY = y;
                        fDragStartSettings = fEditorSettings;
                        return true;
                    }
                }
                for (int target = 2; target <= 5; ++target)
                {
                    const float rowY = 448.0f + static_cast<float>(target - 2) * 32.0f;
                    if (hit(x, y, 330, rowY, 280, 24))
                    {
                        fDragTarget = target;
                        updateEditorDrag(x, y);
                        return true;
                    }
                }
                return false;
            }

            if (hit(x, y, 690, 108, 222, 28))
            {
                fEditorMode = true;
                fHasWaveform = false;
                requestWaveform();
                repaint();
                return true;
            }
            const int pad = hitPad(x, y);
            if (pad >= 0)
            {
                fSelectedPad = pad;
                if (fArm)
                {
                    setControlValue(kStartPad, static_cast<float>(pad + 1));
                    setLocalStatus("Press any pad to start");
                }
                else
                {
                    fPressedPad = pad;
#if DISTRHO_PLUGIN_WANT_MIDI_INPUT
                    sendNote(0, static_cast<uint8_t>(fBaseNote + pad), 127);
#endif
                }
                repaint();
                return true;
            }

            if (hit(x, y, 690, 145, 108, 42))
            {
                setControlValue(kArm, 0.0f);
                return true;
            }
            if (hit(x, y, 804, 145, 108, 42))
            {
                setControlValue(kArm, 1.0f);
                return true;
            }
            if (hit(x, y, 690, 220, 106, 38))
            {
                setControlValue(kRecordMode, 0.0f);
                return true;
            }
            if (hit(x, y, 804, 220, 108, 38))
            {
                setControlValue(kRecordMode, 1.0f);
                return true;
            }
            if (hit(x, y, 690, 298, 222, 36))
            {
                const float t = std::clamp((x - 690.0f) / 222.0f, 0.0f, 1.0f);
                setControlValue(kFixedLength, 0.01f + t * 29.99f);
                return true;
            }
            if (hit(x, y, 690, 370, 106, 38))
            {
                setControlValue(kPlaybackMode, 0.0f);
                return true;
            }
            if (hit(x, y, 804, 370, 108, 38))
            {
                setControlValue(kPlaybackMode, 1.0f);
                return true;
            }
            if (hit(x, y, 690, 426, 222, 25))
            {
                const float t = std::clamp((x - 690.0f) / 222.0f, 0.0f, 1.0f);
                setControlValue(kPreRoll, t * 100.0f);
                return true;
            }
            if (hit(x, y, 690, 454, 222, 36))
            {
                setControlValue(kMonitor, fMonitor >= 0.5f ? 0.0f : 1.0f);
                return true;
            }
            if (hit(x, y, 690, 515, 70, 34))
            {
                setParameterValue(kFinalize, 1.0f);
                fPressedActionParameter = kFinalize;
                setLocalStatus("Chop finalized");
                return true;
            }
            if (hit(x, y, 766, 515, 70, 34))
            {
                setParameterValue(kUndo, 1.0f);
                fPressedActionParameter = kUndo;
                setLocalStatus("Last chop undone");
                return true;
            }
            if (hit(x, y, 842, 515, 70, 34))
            {
                if (!fClearArmed)
                {
                    fClearArmed = true;
                    fClearTicks = 120;
                    setLocalStatus("Click CLEAR again to confirm");
                }
                else
                {
                    setParameterValue(kClearAll, 1.0f);
                    fPressedActionParameter = kClearAll;
                    fClearArmed = false;
                    fClearTicks = 0;
                    setLocalStatus("Pads cleared");
                }
                repaint();
                return true;
            }
        }
        else if (fEditorMode && fDragTarget >= 0)
        {
            commitEditorSettings();
            fDragTarget = -1;
            repaint();
            return true;
        }
        else if (fPressedPad >= 0)
        {
#if DISTRHO_PLUGIN_WANT_MIDI_INPUT
            sendNote(0, static_cast<uint8_t>(fBaseNote + fPressedPad), 0);
#endif
            fPressedPad = -1;
            repaint();
            return true;
        }
        else if (fPressedActionParameter >= 0)
        {
            setParameterValue(static_cast<uint32_t>(fPressedActionParameter), 0.0f);
            fPressedActionParameter = -1;
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (!fEditorMode || fDragTarget < 0)
            return false;
        const LayoutTransform layout = layoutTransform(static_cast<float>(getWidth()),
                                                       static_cast<float>(getHeight()));
        const float x = (static_cast<float>(ev.pos.getX()) - layout.offsetX) / layout.scale;
        const float y = (static_cast<float>(ev.pos.getY()) - layout.offsetY) / layout.scale;
        updateEditorDrag(x, y);
        return true;
    }

private:
    struct LayoutTransform {
        float scale;
        float offsetX;
        float offsetY;
    };

    static LayoutTransform layoutTransform(const float width, const float height)
    {
        const float scale = std::max(0.1f,
            std::min(width / static_cast<float>(kInitialWidth),
                     height / static_cast<float>(kInitialHeight)));
        return {
            scale,
            (width - static_cast<float>(kInitialWidth) * scale) * 0.5f,
            (height - static_cast<float>(kInitialHeight) * scale) * 0.5f,
        };
    }

    bool fArm;
    float fRecordMode;
    float fFixedLength;
    float fPlaybackMode;
    float fMonitor;
    int fStartPad;
    float fPreRoll;
    int fBaseNote;
    float fGain;
    int fSelectedPad;
    int fCurrentPad;
    int fPressedPad;
    int fPressedActionParameter;
    bool fClearArmed;
    int fClearTicks;
    bool fEditorMode;
    int fDragTarget;
    float fDragStartX;
    float fDragStartY;
    bool fHasWaveform;
    std::array<char, kPadCount> fPadState;
    std::array<char, kPadCount> fPadStatus;
    sms::dsp::SamplePlaybackSettings fEditorSettings{};
    sms::dsp::SamplePlaybackSettings fDragStartSettings{};
    sms::audio::WaveformSummary fWaveform{};
    char fStatus[160];

    struct EnvelopeGraphGeometry {
        float left;
        float right;
        float top;
        float bottom;
        float attackX;
        float decayX;
        float releaseX;
        float attackHandleX;
        float decayHandleX;
        float releaseHandleX;
        float sustainY;
        float durationSeconds;
    };

    static bool hit(float x, float y, float rx, float ry, float rw, float rh)
    {
        return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
    }

    static int clampPad(float value)
    {
        return std::clamp(static_cast<int>(std::lround(value)), 0, static_cast<int>(kPadCount - 1));
    }

    static int clampNote(float value)
    {
        return std::clamp(static_cast<int>(std::lround(value)), 0, 127);
    }

    int hitPad(float x, float y) const
    {
        return mainPadGrid().hit({x, y});
    }

    static sms::ui::PadGridLayout mainPadGrid()
    {
        return {{46.0f, 164.0f, 558.0f, 342.0f}, 4, 4, 10.0f};
    }

    static sms::ui::PadGridLayout editorPadGrid()
    {
        return {{690.0f, 170.0f, 222.0f, 360.0f}, 2, 8, 6.0f};
    }

    float editorRegionSeconds() const
    {
        if (fWaveform.frames != 0U && fWaveform.sampleRate > 1.0)
            return std::max(1.0e-4f,
                (fEditorSettings.end - fEditorSettings.start) *
                static_cast<float>(fWaveform.frames) / static_cast<float>(fWaveform.sampleRate));
        return 5.0f;
    }

    EnvelopeGraphGeometry envelopeGraphGeometry(const float x = 46.0f,
                                                 const float y = 448.0f,
                                                 const float w = 250.0f,
                                                 const float h = 120.0f) const
    {
        const float duration = editorRegionSeconds();
        const float left = x + 10.0f;
        const float right = x + w - 10.0f;
        const float top = y + 10.0f;
        const float bottom = y + h - 16.0f;
        const float release = std::min(fEditorSettings.releaseSeconds, duration * 0.5f);
        const float releaseStart = duration - release;
        const float attackEnd = std::min(fEditorSettings.attackSeconds, releaseStart);
        const float decayEnd = std::min(fEditorSettings.attackSeconds +
                                        fEditorSettings.decaySeconds, releaseStart);
        const auto toX = [left, right, duration](const float seconds) noexcept {
            return left + (right - left) * std::clamp(seconds / duration, 0.0f, 1.0f);
        };
        const float attackX = toX(attackEnd);
        const float decayX = toX(decayEnd);
        const float releaseX = toX(releaseStart);
        constexpr float handleGap = 12.0f;
        const float attackHandleX = std::clamp(attackX, left, right - handleGap * 2.0f);
        const float decayHandleX = std::clamp(decayX,
                                              attackHandleX + handleGap,
                                              right - handleGap);
        const float releaseHandleX = std::clamp(releaseX,
                                                decayHandleX + handleGap, right);
        return {left, right, top, bottom, attackX, decayX, releaseX,
                attackHandleX, decayHandleX, releaseHandleX,
                bottom - (bottom - top) * fEditorSettings.sustainLevel, duration};
    }

    int hitEnvelopeHandle(const float x, const float y) const
    {
        const auto graph = envelopeGraphGeometry();
        const std::array<sms::ui::Point, 3> points{{
            {graph.attackHandleX, graph.top},
            {graph.decayHandleX, graph.sustainY},
            {graph.releaseHandleX, graph.sustainY},
        }};
        int nearest = -1;
        float nearestDistance = 15.0f * 15.0f;
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            const float dx = x - points[index].x;
            const float dy = y - points[index].y;
            const float distance = dx * dx + dy * dy;
            if (distance <= nearestDistance)
            {
                nearest = static_cast<int>(index) + 6;
                nearestDistance = distance;
            }
        }
        return nearest;
    }

    void drawHeader()
    {
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(26.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(kText);
        text(32, 26, "SMS-MIDICHOPPER", nullptr);
        fontSize(12.0f);
        fillColor(kMuted);
        text(34, 58, "SEQUENTIAL CHOP  /  LIVE SAMPLE WORKSTATION", nullptr);

        // A compact arm/play indicator remains visible while the pad grid is used.
        beginPath();
        circle(878, 43, 6);
        fillColor(fArm ? kAmber : kCyan);
        fill();
        fontSize(13.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(fArm ? kAmber : kCyan);
        text(912, 43, fArm ? "ARMED" : "PLAY", nullptr);
    }

    void drawPanel(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 12);
        fillColor(kPanel);
        fill();
        strokeColor(kBorder);
        strokeWidth(1.0f);
        stroke();
    }

    void drawPadPanel()
    {
        drawPanel(24, 96, 630, 516);
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(13);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(kMuted);
        text(46, 120, fArm ? "DESTINATION PADS" : "PAD BANK", nullptr);
        fontSize(11);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(kMuted);
        text(632, 120, fArm ? "CLICK A PAD TO SET START" : "CLICK TO PLAY", nullptr);
        drawWaveform(46, 137, 586, 18);

        const auto grid = mainPadGrid();
        for (int i = 0; i < 16; ++i)
        {
            const auto cell = grid.cell(i);
            const float x = cell.x;
            const float y = cell.y;
            const float pw = cell.width;
            const float ph = cell.height;
            const bool occupied = fPadState[static_cast<size_t>(i)] != '0' && fPadState[static_cast<size_t>(i)] != '.';
            const bool active = fPadStatus[static_cast<size_t>(i)] != '0';
            const bool recording = (fArm && active) || fCurrentPad == i;
            const bool playing = (!fArm && active) || fPressedPad == i;
            const bool selected = fSelectedPad == i || fStartPad == i;
            const DGL_NAMESPACE::Color base = recording ? kAmber : (occupied ? kCyan : kPanelRaised);

            beginPath();
            roundedRect(x, y, pw, ph, 9);
            fillColor(base.withAlpha(occupied || recording ? 0.22f : 0.9f));
            fill();
            if (playing)
            {
                beginPath();
                roundedRect(x + 3, y + 3, pw - 6, ph - 6, 7);
                fillColor(kCyan.withAlpha(0.20f));
                fill();
            }
            strokeColor(selected ? kAmber : base.withAlpha(0.55f));
            strokeWidth(selected ? 2.0f : 1.0f);
            stroke();

            char padNumber[8];
            std::snprintf(padNumber, sizeof(padNumber), "%02d", i + 1);
            fontSize(12);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(selected ? kAmber : kMuted);
            text(x + 12, y + 10, padNumber, nullptr);

            char note[16];
            midiName(fBaseNote + i, note, sizeof(note));
            fontSize(20);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(occupied || recording ? kText : kMuted);
            text(x + pw * 0.5f, y + ph * 0.54f, note, nullptr);

            fontSize(10);
            textAlign(ALIGN_RIGHT | ALIGN_BOTTOM);
            fillColor(recording ? kAmber : (playing ? kCyan : kMuted));
            text(x + pw - 10, y + ph - 9,
                 recording ? "RECORDING" : (playing ? "PLAYING" : (occupied ? "READY" : "EMPTY")), nullptr);
        }
    }

    void drawWaveform(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 4);
        fillColor(kBackground);
        fill();
        beginPath();
        moveTo(x, y + h * 0.5f);
        lineTo(x + w, y + h * 0.5f);
        strokeColor(kBorder);
        strokeWidth(1.0f);
        stroke();

        if (fHasWaveform)
        {
            const float displayScale = waveformDisplayScale();
            for (size_t i = 0; i < sms::audio::kWaveformBins; ++i)
            {
                const float px = x + w * (static_cast<float>(i) + 0.5f) /
                    static_cast<float>(sms::audio::kWaveformBins);
                beginPath();
                moveTo(px, y + h * (0.5f - fWaveform.maximum[i] * displayScale * 0.43f));
                lineTo(px, y + h * (0.5f - fWaveform.minimum[i] * displayScale * 0.43f));
                strokeColor(fCurrentPad >= 0 ? kAmber : kCyan);
                strokeWidth(1.2f);
                stroke();
            }
        }
    }

    float waveformDisplayScale() const
    {
        float peak = 0.0f;
        for (std::size_t bin = 0; bin < sms::audio::kWaveformBins; ++bin)
            peak = std::max({peak, std::abs(fWaveform.minimum[bin]),
                             std::abs(fWaveform.maximum[bin])});
        return peak > 1.0e-6f ? 1.0f / peak : 1.0f;
    }

    void drawSampleEditor()
    {
        drawPanel(24, 96, 630, 516);
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(13);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(kMuted);
        char heading[48];
        std::snprintf(heading, sizeof(heading), "SAMPLE EDITOR  /  PAD %02d", fSelectedPad + 1);
        text(46, 120, heading, nullptr);

        const float x = 46.0f;
        const float y = 158.0f;
        const float w = 586.0f;
        const float h = 222.0f;
        beginPath();
        roundedRect(x, y, w, h, 8);
        fillColor(kBackground);
        fill();
        strokeColor(kBorder);
        stroke();

        beginPath();
        moveTo(x, y + h * 0.5f);
        lineTo(x + w, y + h * 0.5f);
        strokeColor(kBorder.withAlpha(0.7f));
        strokeWidth(1.0f);
        stroke();

        if (fHasWaveform)
        {
            const float displayScale = waveformDisplayScale();
            // Keep the source visible as context behind the processed preview.
            for (std::size_t bin = 0; bin < sms::audio::kWaveformBins; ++bin)
            {
                const float normalized = (static_cast<float>(bin) + 0.5f) /
                                         static_cast<float>(sms::audio::kWaveformBins);
                const float px = x + normalized * w;
                beginPath();
                moveTo(px, y + h * (0.5f - fWaveform.maximum[bin] * displayScale * 0.44f));
                lineTo(px, y + h * (0.5f - fWaveform.minimum[bin] * displayScale * 0.44f));
                strokeColor(kMuted.withAlpha(0.24f));
                strokeWidth(1.4f);
                stroke();
            }

            // Redraw the active region through the ADSR so edits are reflected
            // immediately without hiding the original sample shape.
            for (std::size_t bin = 0; bin < sms::audio::kWaveformBins; ++bin)
            {
                const float normalized = (static_cast<float>(bin) + 0.5f) /
                                         static_cast<float>(sms::audio::kWaveformBins);
                if (normalized < fEditorSettings.start || normalized > fEditorSettings.end)
                    continue;
                const float gain = waveformEnvelopeGain(normalized);
                const float px = x + normalized * w;
                beginPath();
                moveTo(px, y + h * (0.5f - fWaveform.maximum[bin] * displayScale * gain * 0.44f));
                lineTo(px, y + h * (0.5f - fWaveform.minimum[bin] * displayScale * gain * 0.44f));
                strokeColor(kCyan);
                strokeWidth(2.3f);
                stroke();
            }
            char scaleLabel[32];
            if (displayScale > 1.05f)
                std::snprintf(scaleLabel, sizeof(scaleLabel), "DISPLAY  x%.1f", displayScale);
            else
                std::snprintf(scaleLabel, sizeof(scaleLabel), "DISPLAY  1:1");
            fontSize(9);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fillColor(kMuted.withAlpha(0.8f));
            text(x + w - 10.0f, y + 30.0f, scaleLabel, nullptr);
        }
        else
        {
            fontSize(14);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(kMuted);
            text(x + w * 0.5f, y + h * 0.5f, "EMPTY PAD", nullptr);
        }

        const float startX = x + w * fEditorSettings.start;
        const float endX = x + w * fEditorSettings.end;
        beginPath();
        rect(x, y, std::max(0.0f, startX - x), h);
        rect(endX, y, std::max(0.0f, x + w - endX), h);
        fillColor(kBackground.withAlpha(0.64f));
        fill();
        drawCutHandle(startX, y, h, "START");
        drawCutHandle(endX, y, h, "END");

        char region[112];
        const auto startFrame = static_cast<std::uint32_t>(fEditorSettings.start * fWaveform.frames);
        const auto endFrame = static_cast<std::uint32_t>(fEditorSettings.end * fWaveform.frames);
        std::snprintf(region, sizeof(region), "REGION  %u — %u frames     %.1f%% of source",
                      startFrame, endFrame,
                      (fEditorSettings.end - fEditorSettings.start) * 100.0f);
        fontSize(11);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(kMuted);
        text(46, 392, region, nullptr);

        fontSize(11);
        fillColor(kMuted);
        text(46, 426, "AMPLITUDE ENVELOPE", nullptr);
        fontSize(9);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(kMuted.withAlpha(0.8f));
        text(296, 428, "DRAG NODES", nullptr);
        drawEnvelopeGraph(46, 448, 250, 120);
        drawEditorSlider(2, 448, "ATTACK", fEditorSettings.attackSeconds);
        drawEditorSlider(3, 480, "DECAY", fEditorSettings.decaySeconds);
        drawEditorSlider(4, 512, "SUSTAIN", fEditorSettings.sustainLevel);
        drawEditorSlider(5, 544, "RELEASE", fEditorSettings.releaseSeconds);
    }

    void drawCutHandle(const float x, const float y, const float height, const char* const label)
    {
        beginPath();
        moveTo(x, y);
        lineTo(x, y + height);
        strokeColor(kAmber);
        strokeWidth(2.0f);
        stroke();
        beginPath();
        roundedRect(x - 16.0f, y + 7.0f, 32.0f, 16.0f, 4.0f);
        fillColor(kAmber);
        fill();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(7.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(kBackground);
        text(x, y + 15.0f, label, nullptr);
    }

    float waveformEnvelopeGain(const float sourcePosition) const
    {
        const float regionWidth = fEditorSettings.end - fEditorSettings.start;
        if (regionWidth <= 0.0f || fWaveform.frames == 0U || fWaveform.sampleRate <= 1.0)
            return 0.0f;
        const float regionPosition = std::clamp(
            (sourcePosition - fEditorSettings.start) / regionWidth, 0.0f, 1.0f);
        const float regionSeconds = regionWidth * static_cast<float>(fWaveform.frames) /
                                    static_cast<float>(fWaveform.sampleRate);
        const float time = regionPosition * regionSeconds;

        const auto adsLevelAt = [this](const float stageTime) noexcept {
            const float attack = fEditorSettings.attackSeconds;
            const float decay = fEditorSettings.decaySeconds;
            if (attack > 0.0f && stageTime < attack)
                return std::clamp(stageTime / attack, 0.0f, 1.0f);
            if (decay > 0.0f && stageTime < attack + decay)
            {
                const float progress = std::clamp((stageTime - attack) / decay, 0.0f, 1.0f);
                return 1.0f + (fEditorSettings.sustainLevel - 1.0f) * progress;
            }
            return fEditorSettings.sustainLevel;
        };

        // One-shot playback schedules release before the cut end. The engine
        // caps release to half of a short region so an attack cannot be erased.
        const float release = std::min(fEditorSettings.releaseSeconds, regionSeconds * 0.5f);
        const float releaseStart = regionSeconds - release;
        if (release > 0.0f && time >= releaseStart)
        {
            const float releaseLevel = adsLevelAt(releaseStart);
            const float progress = std::clamp((time - releaseStart) / release, 0.0f, 1.0f);
            return releaseLevel * (1.0f - progress);
        }
        return adsLevelAt(time);
    }

    void drawEnvelopeGraph(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 7);
        fillColor(kBackground);
        fill();
        strokeColor(kBorder);
        stroke();

        const auto graph = envelopeGraphGeometry(x, y, w, h);
        const float attackWidth = graph.attackX - graph.left;
        const float decayWidth = graph.decayX - graph.attackX;
        const float sustainWidth = graph.releaseX - graph.decayX;
        const float releaseWidth = graph.right - graph.releaseX;

        // Subtle guides make the stage proportions and sustain level easier to read.
        beginPath();
        moveTo(graph.left, graph.sustainY);
        lineTo(graph.right, graph.sustainY);
        strokeColor(kBorder.withAlpha(0.45f));
        strokeWidth(1.0f);
        stroke();

        // A translucent area gives the envelope shape visual weight.
        beginPath();
        moveTo(graph.left, graph.bottom);
        bezierTo(graph.left + attackWidth * 0.55f, graph.bottom,
                 graph.attackX - attackWidth * 0.12f,
                 graph.top + (graph.bottom - graph.top) * 0.18f,
                 graph.attackX, graph.top);
        bezierTo(graph.attackX + decayWidth * 0.18f,
                 graph.top + (graph.sustainY - graph.top) * 0.62f,
                 graph.decayX - decayWidth * 0.25f, graph.sustainY,
                 graph.decayX, graph.sustainY);
        lineTo(graph.releaseX, graph.sustainY);
        bezierTo(graph.releaseX + releaseWidth * 0.18f,
                 graph.sustainY + (graph.bottom - graph.sustainY) * 0.62f,
                 graph.right - releaseWidth * 0.25f, graph.bottom,
                 graph.right, graph.bottom);
        closePath();
        fillColor(kCyan.withAlpha(0.10f));
        fill();

        beginPath();
        moveTo(graph.left, graph.bottom);
        bezierTo(graph.left + attackWidth * 0.55f, graph.bottom,
                 graph.attackX - attackWidth * 0.12f,
                 graph.top + (graph.bottom - graph.top) * 0.18f,
                 graph.attackX, graph.top);
        bezierTo(graph.attackX + decayWidth * 0.18f,
                 graph.top + (graph.sustainY - graph.top) * 0.62f,
                 graph.decayX - decayWidth * 0.25f, graph.sustainY,
                 graph.decayX, graph.sustainY);
        lineTo(graph.releaseX, graph.sustainY);
        bezierTo(graph.releaseX + releaseWidth * 0.18f,
                 graph.sustainY + (graph.bottom - graph.sustainY) * 0.62f,
                 graph.right - releaseWidth * 0.25f, graph.bottom,
                 graph.right, graph.bottom);
        strokeColor(kCyan);
        strokeWidth(2.0f);
        stroke();

        const std::array<sms::ui::Point, 3> anchors{{
            {graph.attackX, graph.top},
            {graph.decayX, graph.sustainY},
            {graph.releaseX, graph.sustainY},
        }};
        const std::array<sms::ui::Point, 3> points{{
            {graph.attackHandleX, graph.top},
            {graph.decayHandleX, graph.sustainY},
            {graph.releaseHandleX, graph.sustainY},
        }};
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            if (std::abs(points[index].x - anchors[index].x) < 0.5f)
                continue;
            beginPath();
            moveTo(anchors[index].x, anchors[index].y);
            lineTo(points[index].x, points[index].y);
            strokeColor(kMuted.withAlpha(0.45f));
            strokeWidth(1.0f);
            stroke();
        }
        for (std::size_t index = 0; index < points.size(); ++index)
        {
            beginPath();
            circle(points[index].x, points[index].y, 5.0f);
            fillColor(index == 0U ? kAmber : kCyan);
            fill();
            strokeColor(kBackground);
            strokeWidth(1.5f);
            stroke();
        }

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(7.0f);
        textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
        fillColor(kMuted.withAlpha(0.8f));
        text(graph.left + attackWidth * 0.5f, y + h - 2.0f, "A", nullptr);
        text(graph.attackX + decayWidth * 0.5f, y + h - 2.0f, "D", nullptr);
        text(graph.decayX + sustainWidth * 0.5f, y + h - 2.0f, "S", nullptr);
        text(graph.releaseX + releaseWidth * 0.5f, y + h - 2.0f, "R", nullptr);

        char duration[24];
        std::snprintf(duration, sizeof(duration), "%.2f s", graph.durationSeconds);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(kMuted.withAlpha(0.65f));
        text(graph.right, graph.top + 2.0f, duration, nullptr);
    }

    void drawEditorSlider(const int target, const float y, const char* const label, const float value)
    {
        const bool sustain = target == 4;
        const float normalized = sustain ? value : std::sqrt(std::clamp(value / 5.0f, 0.0f, 1.0f));
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(9);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(kMuted);
        text(330, y, label, nullptr);
        char display[24];
        if (sustain)
            std::snprintf(display, sizeof(display), "%d%%", static_cast<int>(std::lround(value * 100.0f)));
        else if (value < 1.0f)
            std::snprintf(display, sizeof(display), "%d ms", static_cast<int>(std::lround(value * 1000.0f)));
        else
            std::snprintf(display, sizeof(display), "%.2f s", value);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(kText);
        text(610, y, display, nullptr);
        drawSlider(420, y + 14.0f, 190, normalized, sustain ? kCyan : kAmber);
    }

    void drawEditorPadPanel()
    {
        drawPanel(670, 96, 266, 516);
        drawSegment(690, 112, 222, 32, "MAIN VIEW", true, kCyan);
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(11);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(kMuted);
        text(690, 153, "SELECT PAD", nullptr);

        const auto grid = editorPadGrid();
        for (int pad = 0; pad < 16; ++pad)
        {
            const auto cell = grid.cell(pad);
            const bool occupied = fPadState[static_cast<std::size_t>(pad)] != '0' &&
                                  fPadState[static_cast<std::size_t>(pad)] != '.';
            const bool active = fPadStatus[static_cast<std::size_t>(pad)] != '0';
            const bool selected = pad == fSelectedPad;
            beginPath();
            roundedRect(cell.x, cell.y, cell.width, cell.height, 6);
            fillColor((active ? kAmber : (occupied ? kCyan : kPanelRaised)).withAlpha(
                occupied || active ? 0.20f : 0.9f));
            fill();
            strokeColor(selected ? kAmber : (occupied ? kCyan.withAlpha(0.55f) : kBorder));
            strokeWidth(selected ? 2.0f : 1.0f);
            stroke();

            char padLabel[12];
            std::snprintf(padLabel, sizeof(padLabel), "%02d", pad + 1);
            fontSize(11);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(selected ? kAmber : kText);
            text(cell.x + 9, cell.y + cell.height * 0.5f, padLabel, nullptr);
            char note[16];
            midiName(fBaseNote + pad, note, sizeof(note));
            fontSize(10);
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            fillColor(occupied ? kCyan : kMuted);
            text(cell.x + cell.width - 9, cell.y + cell.height * 0.5f, note, nullptr);
        }

        fontSize(10);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(kMuted);
        text(690, 548, "Cut points and ADSR are stored per pad.", nullptr);
        text(690, 564, "Edits are non-destructive.", nullptr);
    }

    void drawControlPanel()
    {
        drawPanel(670, 96, 266, 516);
        drawSegment(690, 108, 222, 28, "SAMPLE EDITOR", false, kAmber);
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(13);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(kMuted);

        drawSegment(690, 145, 108, 42, "PLAY", !fArm, kCyan);
        drawSegment(804, 145, 108, 42, "ARM", fArm, kAmber);

        fontSize(11);
        fillColor(kMuted);
        text(690, 204, "CAPTURE MODE", nullptr);
        drawSegment(690, 220, 106, 38, "SEQUENTIAL", fRecordMode < 0.5f, kCyan);
        drawSegment(804, 220, 108, 38, "FIXED", fRecordMode >= 0.5f, kCyan);

        fontSize(11);
        fillColor(kMuted);
        text(690, 278, "FIXED LENGTH", nullptr);
        const float length = std::clamp(fFixedLength, 0.01f, 30.0f);
        drawSlider(690, 298, 222, length / 30.0f, kCyan);
        char lengthText[32];
        std::snprintf(lengthText, sizeof(lengthText), "%.2f s", length);
        fontSize(12);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(kText);
        text(912, 278, lengthText, nullptr);

        fontSize(11);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(kMuted);
        text(690, 352, "PLAYBACK", nullptr);
        drawSegment(690, 370, 106, 38, "ONE SHOT", fPlaybackMode < 0.5f, kCyan);
        drawSegment(804, 370, 108, 38, "GATE", fPlaybackMode >= 0.5f, kCyan);

        fontSize(11);
        fillColor(kMuted);
        text(690, 412, "PRE-ROLL", nullptr);
        char preRollText[24];
        std::snprintf(preRollText, sizeof(preRollText), "%d ms", static_cast<int>(std::lround(std::clamp(fPreRoll, 0.0f, 100.0f))));
        fontSize(11);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(kText);
        text(912, 412, preRollText, nullptr);
        drawSlider(690, 429, 222, std::clamp(fPreRoll / 100.0f, 0.0f, 1.0f), kAmber);

        const char* monitor = fMonitor >= 0.5f ? "MONITOR  ON" : "MONITOR  OFF";
        drawSegment(690, 454, 222, 36, monitor, fMonitor >= 0.5f, kCyan);

        fontSize(11);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(kMuted);
        text(690, 497, "CHOP", nullptr);
        drawAction(690, 515, 70, 34, "FINALIZE", kCyan, false);
        drawAction(766, 515, 70, 34, "UNDO", kAmber, false);
        drawAction(842, 515, 70, 34, fClearArmed ? "CONFIRM" : "CLEAR", kRed, fClearArmed);
    }

    void drawSegment(float x, float y, float w, float h, const char* label, bool active,
                     const DGL_NAMESPACE::Color& accent)
    {
        beginPath();
        roundedRect(x, y, w, h, 7);
        fillColor(active ? accent.withAlpha(0.20f) : kPanelRaised);
        fill();
        strokeColor(active ? accent : kBorder);
        strokeWidth(active ? 1.5f : 1.0f);
        stroke();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(11);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(active ? accent : kMuted);
        text(x + w * 0.5f, y + h * 0.5f, label, nullptr);
    }

    void drawSlider(float x, float y, float w, float value, const DGL_NAMESPACE::Color& accent)
    {
        beginPath();
        roundedRect(x, y, w, 6, 3);
        fillColor(kPanelRaised);
        fill();
        beginPath();
        roundedRect(x, y, w * std::clamp(value, 0.0f, 1.0f), 6, 3);
        fillColor(accent.withAlpha(0.8f));
        fill();
        beginPath();
        circle(x + w * std::clamp(value, 0.0f, 1.0f), y + 3, 8);
        fillColor(accent);
        fill();
    }

    void drawAction(float x, float y, float w, float h, const char* label,
                    const DGL_NAMESPACE::Color& accent, bool active)
    {
        beginPath();
        roundedRect(x, y, w, h, 6);
        fillColor(active ? accent.withAlpha(0.28f) : kPanelRaised);
        fill();
        strokeColor(accent.withAlpha(active ? 1.0f : 0.65f));
        stroke();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(9);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(active ? accent : kMuted);
        text(x + w * 0.5f, y + h * 0.5f, label, nullptr);
    }

    void drawFooter()
    {
        char liveStatus[80];
        const char* textValue = nullptr;
        if (fEditorMode)
        {
            std::snprintf(liveStatus, sizeof(liveStatus),
                          "Editing Pad %02d — drag cut handles or envelope controls",
                          fSelectedPad + 1);
            textValue = liveStatus;
        }
        else if (fCurrentPad >= 0)
        {
            std::snprintf(liveStatus, sizeof(liveStatus), "Chopping to Pad %02d — press any pad for next slice",
                          fCurrentPad + 1);
            textValue = liveStatus;
        }
        else
        {
            const bool bankFull = std::all_of(fPadState.begin(), fPadState.end(),
                [](const char state) { return state != '0' && state != '.'; });
            textValue = bankFull ? "Bank full — finalize, undo, or clear to continue" :
                (fStatus[0] != '\0' ? fStatus : (fArm ? "Press any pad to start" : "Ready to play"));
        }
        beginPath();
        roundedRect(24, 626, 912, 32, 7);
        fillColor(kPanelRaised);
        fill();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(12);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(fEditorMode ? kCyan : (fCurrentPad >= 0 ? kAmber : kMuted));
        text(38, 642, textValue, nullptr);

        char details[96];
        if (fEditorMode)
            std::snprintf(details, sizeof(details), "REGION %.1f%% — %.1f%%   SUSTAIN %d%%",
                          fEditorSettings.start * 100.0f, fEditorSettings.end * 100.0f,
                          static_cast<int>(std::lround(fEditorSettings.sustainLevel * 100.0f)));
        else
            std::snprintf(details, sizeof(details), "START %02d   PRE %d ms   GAIN %+.1f dB",
                          fStartPad + 1, static_cast<int>(std::lround(fPreRoll)),
                          std::clamp(fGain, -24.0f, 12.0f));
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(kMuted);
        text(922, 642, details, nullptr);
    }

    void setLocalStatus(const char* status)
    {
        copyString(fStatus, status);
        repaint();
    }

    void setControlValue(const uint32_t parameter, const float value)
    {
        // Hosts are not required to echo a UI-originated parameter change back
        // to this UI immediately. Keep the visible state responsive, then send
        // the exact same value to the plugin.
        parameterChanged(parameter, value);
        setParameterValue(parameter, value);
    }

    void requestWaveform()
    {
#if DISTRHO_PLUGIN_WANT_STATE
        char pad[8];
        std::snprintf(pad, sizeof(pad), "%d", fSelectedPad);
        setState("waveform_request", pad);
#endif
    }

    void selectEditorPad(const int pad)
    {
        fSelectedPad = clampPad(static_cast<float>(pad));
        fEditorSettings = {};
        fHasWaveform = false;
        requestWaveform();
        repaint();
    }

    void commitEditorSettings()
    {
#if DISTRHO_PLUGIN_WANT_STATE
        const std::string encoded = sms::state::encodeSamplePlaybackSettings(fEditorSettings);
        setState(kPadEditStateKeys[static_cast<std::size_t>(fSelectedPad)], encoded.c_str());
#endif
    }

    bool parseEditorState(const char* const key, const char* const value)
    {
        for (std::size_t pad = 0; pad < kPadEditStateKeys.size(); ++pad)
        {
            if (std::strcmp(key, kPadEditStateKeys[pad]) != 0)
                continue;
            sms::dsp::SamplePlaybackSettings decoded;
            if (sms::state::decodeSamplePlaybackSettings(value, decoded) &&
                pad == static_cast<std::size_t>(fSelectedPad))
                fEditorSettings = decoded;
            return true;
        }
        return false;
    }

    void updateEditorDrag(const float x, const float y)
    {
        if (fDragTarget == 0 || fDragTarget == 1)
        {
            constexpr float minimumWidth = 0.002f;
            const float normalized = std::clamp((x - 46.0f) / 586.0f, 0.0f, 1.0f);
            if (fDragTarget == 0)
                fEditorSettings.start = std::min(normalized, fEditorSettings.end - minimumWidth);
            else
                fEditorSettings.end = std::max(normalized, fEditorSettings.start + minimumWidth);
        }
        else if (fDragTarget >= 2 && fDragTarget <= 5)
        {
            const float normalized = std::clamp((x - 420.0f) / 190.0f, 0.0f, 1.0f);
            const float seconds = normalized * normalized * 5.0f;
            switch (fDragTarget)
            {
            case 2: fEditorSettings.attackSeconds = seconds; break;
            case 3: fEditorSettings.decaySeconds = seconds; break;
            case 4: fEditorSettings.sustainLevel = normalized; break;
            case 5: fEditorSettings.releaseSeconds = seconds; break;
            default: break;
            }
        }
        else if (fDragTarget >= 6 && fDragTarget <= 8)
        {
            const auto graph = envelopeGraphGeometry();
            const float secondsDelta = (x - fDragStartX) /
                                       (graph.right - graph.left) * graph.durationSeconds;
            const float sustainDelta = (fDragStartY - y) /
                                       (graph.bottom - graph.top);
            const float effectiveRelease = std::min(fEditorSettings.releaseSeconds,
                                                    graph.durationSeconds * 0.5f);
            const float releaseStart = graph.durationSeconds - effectiveRelease;
            switch (fDragTarget)
            {
            case 6:
                fEditorSettings.attackSeconds = std::clamp(
                    fDragStartSettings.attackSeconds + secondsDelta,
                    0.0f, std::min(30.0f, releaseStart));
                break;
            case 7:
                fEditorSettings.decaySeconds = std::clamp(
                    fDragStartSettings.decaySeconds + secondsDelta, 0.0f,
                    std::min(30.0f, std::max(0.0f,
                        releaseStart - fEditorSettings.attackSeconds)));
                fEditorSettings.sustainLevel = std::clamp(
                    fDragStartSettings.sustainLevel + sustainDelta, 0.0f, 1.0f);
                break;
            case 8:
                fEditorSettings.releaseSeconds = std::clamp(
                    fDragStartSettings.releaseSeconds - secondsDelta, 0.0f,
                    std::min(30.0f, graph.durationSeconds * 0.5f));
                fEditorSettings.sustainLevel = std::clamp(
                    fDragStartSettings.sustainLevel + sustainDelta, 0.0f, 1.0f);
                break;
            default:
                break;
            }
        }
        fEditorSettings = sms::dsp::sanitize(fEditorSettings);
        repaint();
    }

    static void copyString(char (&destination)[160], const char* source)
    {
        std::strncpy(destination, source, sizeof(destination) - 1);
        destination[sizeof(destination) - 1] = '\0';
    }

    template <size_t N>
    static void copyChars(std::array<char, N>& destination, const char* source)
    {
        for (size_t i = 0; i < N; ++i)
            destination[i] = source[i] == '\0' ? '0' : source[i];
    }

    static int parsePad(const char* value, int fallback)
    {
        if (value == nullptr || *value == '\0')
            return fallback;
        return clampPad(std::strtof(value, nullptr));
    }

    template <size_t N>
    static void parsePadMask(const char* value, std::array<char, N>& destination)
    {
        destination.fill('0');
        if (value == nullptr)
            return;
        if (std::strncmp(value, "0x", 2) == 0 || std::strncmp(value, "0X", 2) == 0)
        {
            const unsigned long mask = std::strtoul(value + 2, nullptr, 16);
            for (size_t i = 0; i < N && i < 16; ++i)
                destination[i] = (mask & (1UL << i)) != 0 ? '1' : '0';
            return;
        }
        copyChars(destination, value);
    }

    void midiName(int note, char* destination, size_t destinationSize) const
    {
        static constexpr const char* names[] = {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
        if (note < 0 || note > 127)
        {
            std::snprintf(destination, destinationSize, "--");
            return;
        }
        std::snprintf(destination, destinationSize, "%s%d", names[note % 12], note / 12 - 1);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidichopperUI)
};

UI* createUI()
{
    return new MidichopperUI();
}

END_NAMESPACE_DISTRHO
