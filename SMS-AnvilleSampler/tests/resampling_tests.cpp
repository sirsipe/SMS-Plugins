#include "DSP/SincResampler.hpp"
#include "Audio/WavCodec.hpp"
#include "SamplerEngine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <numbers>
#include <string_view>
#include <vector>

namespace {
void check(const bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

constexpr double pi = std::numbers::pi;

void frequencyResponse() {
    sms::dsp::SincResampler resampler;
    // Source tones and expected output are analytic, independent of the FIR.
    // Check steady state, excluding boundary extension and note envelopes.
    for (const double step : {0.25, 0.5, 44100.0 / 48000.0, 1.1, 1.5, 2.0, 4.0, 16.0, 64.0, 256.0}) {
        resampler.configure(step);
        constexpr std::uint32_t count = 256U;
        constexpr double offset = 8192.375;
        const auto frames = static_cast<std::uint32_t>(offset + count * step + 8192);
        for (const double normalized : {0.1, 0.35, 0.501, 0.65, 0.95}) {
            const bool stopband = normalized > 0.5;
            if (!stopband || step > 1.0) {
                const double frequency = normalized / std::max(1.0, step);
                if (frequency >= 0.5) continue;
                std::vector<float> input(frames);
                for (std::uint32_t index = 0; index < frames; ++index)
                    input[index] = static_cast<float>(std::sin(2 * pi * frequency * index));
                double energy = 0.0, error = 0.0;
                for (std::uint32_t frame = 0; frame < count; ++frame) {
                    const double position = offset + frame * step;
                    const auto output = resampler.read(position, 0U, frames, false,
                        [&](const std::uint32_t index) noexcept {
                            return sms::dsp::StereoFrame{input[index], -0.5f * input[index]};
                        });
                    check(std::abs(output.right + 0.5f * output.left) < 1.0e-7f,
                          "sinc keeps stereo channels independent with identical timing");
                    energy += output.left * output.left;
                    const double expected = std::sin(2 * pi * frequency * position);
                    error += (output.left - expected) * (output.left - expected);
                }
                if (stopband) {
                    check(std::sqrt(energy / count) < 0.002,
                          "downsampling suppresses tones above destination Nyquist by >50 dB");
                } else {
                    check(std::sqrt(error / count) < 0.002,
                          "resampling preserves passband tone amplitude and phase");
                }
            }
        }
    }
}

void unityContinuity() {
    sms::dsp::SincResampler resampler;
    const auto reader = [](const std::uint32_t frame) noexcept {
        return sms::dsp::StereoFrame{static_cast<float>(std::cos(2 * pi * 0.45 * frame)),
                                    static_cast<float>(std::cos(2 * pi * 0.49 * frame))};
    };
    resampler.configure(1.0);
    const auto native = resampler.read(1000.0, 0U, 2000U, false, reader);
    for (const double delta : {1.0e-4, 1.0e-5, 1.0e-6}) {
        resampler.configure(1.0 + delta);
        const auto shifted = resampler.read(1000.0, 0U, 2000U, false, reader);
        check(std::abs(native.left - shifted.left) < 25.0 * delta &&
              std::abs(native.right - shifted.right) < 25.0 * delta,
              "crossing unity introduces filtering continuously as rate changes shrink");
    }
}

void boundedRegions() {
    sms::dsp::SincResampler resampler;
    for (const double step : {0.5, 1.0, 1.5, 16.0, 256.0, 1.0e300}) {
        resampler.configure(step);
        for (const bool loop : {false, true})
          for (const std::uint32_t length : {1U, 2U, 3U, 31U})
            for (const double fraction : {0.0, 0.25, 0.999}) {
                std::uint32_t calls = 0U;
                const auto result = resampler.read(100.0 + fraction, 100U, 100U + length, loop,
                    [&](const std::uint32_t index) noexcept {
                        check(index >= 100U && index < 100U + length,
                              "filter never reads outside the selected slice");
                        ++calls;
                        return sms::dsp::StereoFrame{0.25f, -0.75f};
                    });
                check(result.left == 0.25f && result.right == -0.75f,
                      "phase normalization preserves DC on tiny regions and extreme rates");
                check(calls <= 8193U, "extreme rates have bounded filter work");
            }
    }
}

midichopper::PadData tone(const double rate, const std::uint32_t frames) {
    midichopper::PadData source;
    source.sampleRate = rate;
    source.frames = frames;
    source.stereo.resize(static_cast<std::size_t>(frames) * 2U);
    for (std::uint32_t index = 0; index < frames; ++index) {
        source.stereo[index * 2U] = static_cast<float>(std::sin(2 * pi * 0.4 * index));
        source.stereo[index * 2U + 1U] = static_cast<float>(std::cos(2 * pi * 0.05 * index));
    }
    return source;
}

void capturedStereoReads() {
    midichopper::SampleStoragePool pool{3U};
    auto* storage = pool.acquireCapture();
    check(storage != nullptr, "acquire mapped source for stereo FIR reads");
    for (std::uint32_t block = 0; block < 3U; ++block)
        check(pool.ensureCaptureBlock(*storage, block), "map source across three capture blocks");
    for (std::uint32_t frame = 0; frame < 3072U; ++frame)
        pool.writeCapture(*storage, frame, static_cast<float>(frame), -2.0f * frame);
    midichopper::SampleStoragePool::StereoReader reader{pool, storage};
    for (const bool reverse : {false, true})
        for (std::uint32_t index = 0; index < 3072U; ++index) {
            const auto frame = reverse ? 3071U - index : index;
            const auto sample = reader(frame);
            check(sample[0] == pool.sample(storage, frame, 0U) &&
                  sample[1] == pool.sample(storage, frame, 1U),
                  "paired FIR reads cross capture mappings exactly in either direction");
        }
    for (const auto frame : {1023U, 0U, 2048U, 1024U, 3071U, 3072U, 0U}) {
        const auto sample = reader(frame);
        check(sample[0] == pool.sample(storage, frame, 0U) &&
              sample[1] == pool.sample(storage, frame, 1U),
              "FIR reader follows loop jumps and safely returns silence for unmapped blocks");
    }
}

void playbackImportAndExport() {
    constexpr std::uint32_t frames = 4096U;
    const auto source = tone(48000.0, frames);
    // Explicit capacity forces the otherwise source-rate-preserving import
    // path to downsample off audio. Compare its entire output with playback.
    midichopper::SampleStoragePool pool{1U};
    const auto* imported = pool.prepareImport(source, 24000.0, frames / 2U);
    check(imported && imported->frames == frames / 2U && imported->sampleRate == 24000.0,
          "capacity fallback converts to the host rate without changing duration");
    midichopper::SamplerEngine engine{24000.0, 1.0};
    check(engine.importPad(0U, source), "install source-rate-preserving playback pad");
    auto settings = engine.settings();
    settings.monitorInput = false;
    engine.setSettings(settings);
    std::array<float, frames / 2U> left{}, right{};
    const midichopper::MidiEvent on{0U, 36U, 127U, midichopper::MidiEventType::NoteOn};
    engine.process(nullptr, nullptr, left.data(), right.data(), left.size(), &on, 1U);
    double aliasEnergy = 0.0;
    for (std::uint32_t frame = 0; frame < frames / 2U; ++frame) {
        check(left[frame] == pool.sample(imported, frame, 0U) &&
              right[frame] == pool.sample(imported, frame, 1U),
              "live playback and import conversion share filtering and alignment");
        if (frame >= 64U && frame < frames / 2U - 64U)
            aliasEnergy += left[frame] * left[frame];
    }
    check(std::sqrt(aliasEnergy / (frames / 2U - 128U)) < 0.001,
          "live playback and fallback import reject an aliased high-frequency tone");

    // Processed export applies pad tune, reverse and trims using the same
    // boundary policy as playback, including fractional samples at the end.
    for (const float tune : {-12.0f, 12.0f, 24.0f})
      for (const bool reverse : {false, true}) {
        midichopper::SamplerEngine tuned{48000.0, 1.0};
        check(tuned.importPad(0U, source), "import export comparison source");
        sms::dsp::SamplePlaybackSettings region;
        region.start = 0.25f;
        region.end = 0.75f;
        tuned.setPadPlaybackSettings(0U, region);
        sms::dsp::SampleMixerSettings mixer;
        mixer.tuneSemitones = tune;
        mixer.reverse = reverse ? 1.0f : 0.0f;
        tuned.setPadMixerSettings(0U, mixer);
        sms::audio::WavAudio wav;
        wav.sampleRate = 48000U;
        wav.frames = source.frames;
        wav.stereo = source.stereo;
        const auto rendered = sms::audio::renderProcessedStereo(wav, region, mixer);
        check(rendered.frames == static_cast<std::uint32_t>((frames / 2U) / std::exp2(tune / 12.0)),
              "processed export retains pitch-dependent duration");
        std::vector<float> liveLeft(rendered.frames + 1U), liveRight(rendered.frames + 1U);
        tuned.process(nullptr, nullptr, liveLeft.data(), liveRight.data(), liveLeft.size(), &on, 1U);
        for (std::uint32_t frame = 0; frame < rendered.frames; ++frame) {
            check(liveLeft[frame] == rendered.stereo[frame * 2U] &&
                  liveRight[frame] == rendered.stereo[frame * 2U + 1U],
                  "processed export and pad playback agree through both trimmed edges");
        }
        check(liveLeft.back() == 0.0f && liveRight.back() == 0.0f && !tuned.anyPlaybackActive(),
              "sinc adds no output tail or trigger latency");
    }
}

void benchmark() {
    constexpr std::uint32_t blockSize = 128U;
    for (const bool captured : {false, true})
    for (const double step : {1.0, 44100.0 / 48000.0, 1.01, 1.5, 4.0, 14.7, 16.0}) {
        midichopper::SamplerEngine engine{48000.0, 3.0};
        const auto source = tone(48000.0 * step, 4096U);
        auto settings = engine.settings();
        settings.monitorInput = false;
        settings.playbackMode = midichopper::PlaybackMode::Hold;
        settings.maxVoices = 16U;
        engine.setSettings(settings);
        std::array<midichopper::MidiEvent, 16> events;
        for (std::uint32_t pad = 0; pad < events.size(); ++pad) {
            events[pad] = {0U, static_cast<std::uint8_t>(36U + pad), 127U,
                          midichopper::MidiEventType::NoteOn};
            if (!captured) check(engine.importPad(pad, source), "install benchmark source");
        }
        if (captured) {
            settings.armed = true;
            engine.setSettings(settings);
            std::array<float, 4096U> inputLeft{}, inputRight{}, outputLeft{}, outputRight{};
            for (std::uint32_t frame = 0; frame < inputLeft.size(); ++frame) {
                inputLeft[frame] = source.stereo[frame * 2U];
                inputRight[frame] = source.stereo[frame * 2U + 1U];
            }
            for (const auto& event : events)
                engine.process(inputLeft.data(), inputRight.data(), outputLeft.data(), outputRight.data(),
                               inputLeft.size(), &event, 1U);
            settings.armed = false;
            settings.tuneSemitones = static_cast<float>(std::clamp(12.0 * std::log2(step), -24.0, 24.0));
            engine.setSettings(settings);
            engine.process(nullptr, nullptr, outputLeft.data(), outputRight.data(), 0U);
            for (std::uint32_t pad = 0; pad < events.size(); ++pad) {
                check(engine.padMetadata(pad).frames == 4096U, "benchmark captured all 16 pads");
                sms::dsp::SampleMixerSettings mixer;
                mixer.tuneSemitones = static_cast<float>(12.0 * std::log2(step) - settings.tuneSemitones);
                engine.setPadMixerSettings(pad, mixer);
            }
        }
        std::array<float, blockSize> left{}, right{};
        engine.process(nullptr, nullptr, left.data(), right.data(), blockSize, events.data(), events.size());
        std::array<double, 100> times;
        for (auto& time : times) {
            const auto before = std::chrono::steady_clock::now();
            engine.process(nullptr, nullptr, left.data(), right.data(), blockSize);
            time = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - before).count();
        }
        std::sort(times.begin(), times.end());
        std::cout << (captured ? "captured" : "imported") << ": 16 stereo voices, 48kHz/128, step=" << step
                  << ": median=" << times[50] << "us p95=" << times[95]
                  << "us; budget=" << blockSize * 1.0e6 / 48000.0 << "us\n";
    }
}
} // namespace

int main(const int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--benchmark") {
        benchmark();
        return 0;
    }
    frequencyResponse();
    unityContinuity();
    boundedRegions();
    capturedStereoReads();
    playbackImportAndExport();
    std::cout << "Resampling spectral, boundary, and integration checks passed\n";
}
