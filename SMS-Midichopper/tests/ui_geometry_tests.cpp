#include "Audio/WaveformSummary.hpp"
#include "ContextMenu.hpp"
#include "DSP/SamplePlaybackSettings.hpp"
#include "Interaction.hpp"
#include "MidichopperLayout.hpp"
#include "LevelMeter.hpp"
#include "PadLayout.hpp"
#include "UI/Geometry.hpp"
#include "WaveformEditor.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

void check(const bool condition, const char* const message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void padLayouts()
{
    const sms::ui::BankedPadLayout sixteen(0);
    check(sixteen.visiblePadCount() == 16 && sixteen.columns() == 4 && sixteen.rows() == 4,
          "four-by-four arrangement");
    check(sixteen.visualIndex(0) == 12 && sixteen.localIndex(12) == 0,
          "pad numbering starts at the lower-left");

    const sms::ui::BankedPadLayout twelve(1);
    check(twelve.visiblePadCount() == 12 && twelve.columns() == 3 && twelve.rows() == 4,
          "three-by-four arrangement");
    check(twelve.visualIndex(11) == 2 && twelve.localIndex(2) == 11,
          "three-column arrangement is reversible");

    const sms::ui::BankedPadLayout eight(2);
    const auto grid = eight.grid({10.0f, 20.0f, 400.0f, 200.0f}, 10.0f);
    check(eight.visiblePadCount() == 8 && eight.columns() == 4 && eight.rows() == 2,
          "four-by-two arrangement");
    check(grid.hit({15.0f, 25.0f}) == 0 && eight.localIndex(0) == 4,
          "grid hit testing composes with pad mapping");
}

void hamburgerMenuGeometry()
{
    namespace menu = midichopper::ui::layout;
    const auto lastLayout = menu::menuOption(2);
    const auto firstMidiMode = menu::midiBankModeOption(0);
    const auto lastMidiMode = menu::midiBankModeOption(1);
    check(firstMidiMode.y > lastLayout.y + lastLayout.height,
          "MIDI bank choices follow pad-layout choices without overlap");
    check(menu::menuPanel.contains({firstMidiMode.x + firstMidiMode.width * 0.5f,
                                    firstMidiMode.y + firstMidiMode.height * 0.5f}) &&
          menu::menuPanel.contains({lastMidiMode.x + lastMidiMode.width * 0.5f,
                                    lastMidiMode.y + lastMidiMode.height * 0.5f}),
          "MIDI bank choices remain inside the hamburger panel");
}

void contextMenuGeometry()
{
    const sms::ui::Rect canvas{0.0f, 0.0f, 960.0f, 680.0f};
    const sms::ui::ContextMenuGeometry middle({200.0f, 300.0f}, 2, canvas);
    check(middle.bounds().x == 200.0f && middle.bounds().y == 300.0f,
          "context menu preserves an in-bounds anchor");
    check(middle.hit({middle.item(0).x + 1.0f, middle.item(0).y + 1.0f}) == 0 &&
          middle.hit({middle.item(1).x + 1.0f, middle.item(1).y + 1.0f}) == 1,
          "context menu items have stable hit identities");
    check(middle.hit({0.0f, 0.0f}) == -1,
          "context menu rejects points outside its items");

    const sms::ui::ContextMenuGeometry edge({955.0f, 675.0f}, 2, canvas);
    check(edge.bounds().x + edge.bounds().width <= canvas.x + canvas.width &&
          edge.bounds().y + edge.bounds().height <= canvas.y + canvas.height,
          "context menu clamps to the logical canvas");

    sms::ui::HoverState hover;
    check(hover.target() == sms::ui::kNoInteractiveTarget && hover.update(0) &&
          !hover.update(0) && hover.clear() && !hover.clear(),
          "hover state reports only target transitions");
}

void levelMeterGeometry()
{
    namespace meter = sms::ui::meter;
    check(meter::activeSegmentCount(0.0f) == 0U,
          "silence lights no meter segments");
    check(meter::activeSegmentCount(1.0f) == meter::defaultSegmentCount,
          "full scale lights every meter segment");
    check(meter::activeSegmentCount(2.0f) == meter::defaultSegmentCount &&
          meter::activeSegmentCount(INFINITY) == meter::defaultSegmentCount &&
          meter::activeSegmentCount(NAN) == 0U,
          "meter counts clamp overloads and reject non-finite values");
    check(std::abs(meter::amplitudeToDb(0.1f) + 20.0f) < 1.0e-5f,
          "meter uses logarithmic decibel mapping");
    check(!meter::visibleLevelChanged(0.40f, 0.41f) &&
          meter::visibleLevelChanged(0.40f, 0.80f),
          "meter repaints only when a visible LED boundary changes");

    const meter::StereoGeometry narrow({2.0f, 10.0f, 24.0f, 510.0f});
    const auto narrowLeft = narrow.channel(0U);
    const auto narrowRight = narrow.channel(1U);
    check(narrowLeft.width > 0.0f && narrowRight.x > narrowLeft.x + narrowLeft.width,
          "narrow stereo meter columns do not overlap");
    const auto bottom = narrow.segment(0U, 0U);
    const auto top = narrow.segment(0U, meter::defaultSegmentCount - 1U);
    check(top.y >= 10.0f && bottom.y + bottom.height <= 520.0f && top.y < bottom.y,
          "vertical meter segments stay inside tall bounds");

    const meter::StereoGeometry wide({5.0f, 7.0f, 80.0f, 120.0f}, 12U);
    const auto wideLeft = wide.channel(0U);
    const auto wideRight = wide.channel(1U);
    check(wideLeft.width > narrowLeft.width && wideRight.x + wideRight.width <= 85.0f,
          "meter geometry adapts to wider and shorter bounds");
    const meter::StereoGeometry tiny({0.0f, 0.0f, 8.0f, 10.0f});
    const auto tinyTop = tiny.segment(0U, meter::defaultSegmentCount - 1U);
    const auto tinyBottom = tiny.segment(0U, 0U);
    check(tinyTop.y >= 0.0f && tinyTop.height > 0.0f &&
          tinyBottom.y + tinyBottom.height <= 10.0f,
          "meter segments stay inside genuinely short bounds");
    check(meter::segmentZone(19U) == meter::Zone::green &&
          meter::segmentZone(20U) == meter::Zone::yellow &&
          meter::segmentZone(26U) == meter::Zone::red,
          "LED zones transition at minus 18 and minus 6 dB");
}

void waveformGeometry()
{
    sms::audio::WaveformSummary summary;
    summary.frames = 48000U;
    summary.sampleRate = 48000.0;
    summary.minimum[0] = -0.25f;
    summary.maximum[0] = 0.5f;
    sms::dsp::SamplePlaybackSettings settings;
    settings.start = 0.25f;
    settings.end = 0.75f;
    settings.attackSeconds = 0.1f;
    settings.decaySeconds = 0.1f;
    settings.sustainLevel = 0.5f;
    settings.releaseSeconds = 0.1f;

    check(std::abs(sms::ui::waveform::displayScale(summary) - 2.0f) < 1.0e-6f,
          "waveform display normalizes its largest peak");
    check(std::abs(sms::ui::waveform::regionDuration(summary, settings) - 0.5f) < 1.0e-6f,
          "selected region duration uses frame count and sample rate");

    const auto geometry = sms::ui::waveform::envelopeGeometry(
        {0.0f, 0.0f, 250.0f, 120.0f}, summary, settings);
    check(geometry.attackX < geometry.decayX && geometry.decayX < geometry.releaseX,
          "envelope stages remain ordered");
    check(sms::ui::waveform::hitEnvelopeHandle(
              {geometry.attackHandleX, geometry.top}, geometry) ==
              sms::ui::waveform::EditTarget::attackNode,
          "attack handle can be hit");
    check(sms::ui::waveform::envelopeGain(settings.start, summary, settings) == 0.0f,
          "envelope begins at zero");
    check(std::abs(sms::ui::waveform::envelopeGain(settings.end, summary, settings)) < 1.0e-6f,
          "scheduled release reaches zero at the cut end");

    sms::ui::waveform::updateRegion(
        settings, sms::ui::waveform::EditTarget::regionStart, 60.0f,
        {0.0f, 0.0f, 100.0f, 40.0f});
    check(std::abs(settings.start - 0.6f) < 1.0e-6f,
          "region dragging uses the supplied waveform bounds");

    const sms::ui::Rect slider{20.0f, 10.0f, 280.0f, 24.0f};
    const auto track = sms::ui::waveform::envelopeSliderTrack(slider);
    sms::ui::waveform::updateEnvelopeSlider(
        settings, sms::ui::waveform::EditTarget::attackSlider,
        track.x + track.width * 0.5f, slider);
    check(std::abs(settings.attackSeconds - 1.25f) < 1.0e-6f,
          "envelope slider drawing and interaction share one mapping");
}

} // namespace

int main()
{
    padLayouts();
    hamburgerMenuGeometry();
    contextMenuGeometry();
    levelMeterGeometry();
    waveformGeometry();
    std::cout << "UI geometry tests passed\n";
}
