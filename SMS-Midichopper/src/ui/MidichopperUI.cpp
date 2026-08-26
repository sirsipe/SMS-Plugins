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
 * Optional state keys sent by the DSP/plugin side:
 *   pad_mask       16 chars (0/1, or . / x / R / P); hex (0x...) is also accepted
 *   pad_status     16 chars, where R means recording and P means playing
 *   current_pad    decimal pad index, or -1 when no chop is active
 *   selected_pad   decimal pad index (mirrors parameter 5)
 *   status         short human-readable status line
 *   pad_waveform   whitespace/comma separated amplitudes in [-1, 1]
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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

constexpr uint kInitialWidth = 960;
constexpr uint kInitialHeight = 680;
constexpr uint kPadCount = 16;
constexpr uint kBaseNoteDefault = 36;

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
          fHasWaveform(false)
    {
        fPadState.fill('0');
        fPadStatus.fill('0');
        fStatus[0] = '\0';
        fWaveform.fill(0.0f);

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
            fPadState[static_cast<size_t>(index - 12)] = value >= 0.5f ? '1' : '0';
            repaint();
            return;
        }
        if (index >= 28 && index < 44)
        {
            const int pad = static_cast<int>(index - 28);
            fPadStatus[static_cast<size_t>(pad)] = value >= 0.5f ? '1' : '0';
            if (fArm && value >= 0.5f)
                fCurrentPad = pad;
            else if (fCurrentPad == pad && value < 0.5f)
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
        else if (std::strcmp(key, "pad_waveform") == 0)
            parseWaveform(value);
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
        drawPadPanel();
        drawControlPanel();
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
    bool fHasWaveform;
    std::array<char, kPadCount> fPadState;
    std::array<char, kPadCount> fPadStatus;
    std::array<float, 48> fWaveform;
    char fStatus[160];

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
        constexpr float gx = 46.0f;
        constexpr float gy = 164.0f;
        constexpr float gap = 10.0f;
        constexpr float pw = 132.0f;
        constexpr float ph = 78.0f;
        if (x < gx || y < gy)
            return -1;
        const int col = static_cast<int>((x - gx) / (pw + gap));
        const int row = static_cast<int>((y - gy) / (ph + gap));
        if (col < 0 || col >= 4 || row < 0 || row >= 4)
            return -1;
        const float px = gx + col * (pw + gap);
        const float py = gy + row * (ph + gap);
        return hit(x, y, px, py, pw, ph) ? row * 4 + col : -1;
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

        constexpr float gx = 46;
        constexpr float gy = 164;
        constexpr float gap = 10;
        constexpr float pw = 132;
        constexpr float ph = 78;
        for (int i = 0; i < 16; ++i)
        {
            const int col = i % 4;
            const int row = i / 4;
            const float x = gx + col * (pw + gap);
            const float y = gy + row * (ph + gap);
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
            beginPath();
            for (size_t i = 0; i < fWaveform.size(); ++i)
            {
                const float px = x + w * static_cast<float>(i) /
                    static_cast<float>(fWaveform.size() - 1);
                const float py = y + h * (0.5f - fWaveform[i] * 0.43f);
                if (i == 0)
                    moveTo(px, py);
                else
                    lineTo(px, py);
            }
            strokeColor(fCurrentPad >= 0 ? kAmber : kCyan);
            strokeWidth(1.5f);
            stroke();
        }
    }

    void drawControlPanel()
    {
        drawPanel(670, 96, 266, 516);
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(13);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(kMuted);
        text(690, 120, "WORKFLOW", nullptr);

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
        if (fCurrentPad >= 0)
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
        fillColor(fCurrentPad >= 0 ? kAmber : kMuted);
        text(38, 642, textValue, nullptr);

        char details[96];
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

    void parseWaveform(const char* value)
    {
        fWaveform.fill(0.0f);
        fHasWaveform = false;
        if (value == nullptr)
            return;
        const char* cursor = value;
        for (size_t i = 0; i < fWaveform.size(); ++i)
        {
            while (*cursor == ',' || *cursor == ' ' || *cursor == '\t' || *cursor == '\n')
                ++cursor;
            if (*cursor == '\0')
                break;
            char* end = nullptr;
            const float sample = std::strtof(cursor, &end);
            if (end == cursor)
                break;
            fWaveform[i] = std::clamp(sample, -1.0f, 1.0f);
            fHasWaveform = true;
            cursor = end;
        }
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
