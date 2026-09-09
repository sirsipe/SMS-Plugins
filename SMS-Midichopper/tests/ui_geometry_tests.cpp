#include "Audio/WaveformSummary.hpp"
#include "DSP/SamplePlaybackSettings.hpp"
#include "MidichopperLayout.hpp"
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
    waveformGeometry();
    std::cout << "UI geometry tests passed\n";
}
