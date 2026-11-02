#include "SamplerEngine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Whole-engine playback timing, not a timing assertion or a CTest test. Pin the
// process externally when comparing builds, and use the same compiler/options.
namespace {
using namespace midichopper;
constexpr unsigned kSourceFrames = 65536;

struct BenchmarkCase {
    const char* name;
    const char* workload;
    double sourceRate;
    double hostRate;
    float totalTune;
    unsigned voices;
    unsigned block = 128;
    bool captured = false;
    bool retrigger = false;
};

constexpr BenchmarkCase kCases[] = {
    {"native_4", "reasonable", 48000, 48000, 0, 4},
    {"native_8", "reasonable", 48000, 48000, 0, 8},
    {"44100_4", "reasonable", 44100, 48000, 0, 4},
    {"96k_8", "reasonable", 96000, 48000, 0, 8},
    {"96k_octave_8", "reasonable", 96000, 48000, 12, 8},
    {"96k_fractional_8", "reasonable", 96000, 48000, 7.3f, 8},
    {"capture48k_8", "reasonable", 48000, 48000, 0, 8, 128, true},
    {"96k_max_16", "extreme", 96000, 48000, 48, 16},
    {"import192k_max_16", "extreme", 192000, 48000, 48, 16},
    {"import384k_max_16", "extreme", 384000, 48000, 48, 16},
    {"capture192k_max_16", "extreme", 192000, 48000, 48, 16, 128, true},
    {"capture384k_max_16", "extreme", 384000, 48000, 48, 16, 128, true},
    {"host44100_max_16", "extreme", 96000, 44100, 48, 16},
    {"96k_max_block64", "extreme", 96000, 48000, 48, 16, 64},
    {"96k_max_block512", "extreme", 96000, 48000, 48, 16, 512},
    {"96k_max_retrigger", "extreme", 96000, 48000, 48, 16, 128, false, true},
};

struct Options {
    unsigned iterations = 200;
    unsigned warmups = 16;
    std::string label = "current";
    std::string caseName = "all";
    bool expectCap96 = false;
};

unsigned countArgument(const std::string& value, const bool allowZero) {
    std::size_t consumed = 0;
    const auto count = std::stoul(value, &consumed);
    if (consumed != value.size() || count > 1000000 || (!allowZero && count == 0))
        throw std::runtime_error("invalid callback count: " + value);
    return static_cast<unsigned>(count);
}

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void requireActive(const SamplerEngine& engine, const BenchmarkCase& test) {
    for (unsigned pad = 0; pad < test.voices; ++pad)
        require(engine.padMetadata(pad).active,
                std::string(test.name) + ": expected every voice to remain active");
}

void run(const BenchmarkCase& test, const Options& options) {
    // Bounded capture pools keep the background refill worker out of timings.
    const double initialRate = test.captured ? test.sourceRate : test.hostRate;
    // The bounded core reserves this duration per pad, not once for all voices.
    const double captureSeconds = (kSourceFrames + 4096.0) / initialRate;
    SamplerEngine engine(initialRate, captureSeconds);
    auto settings = engine.settings();
    settings.monitorInput = false;
    settings.playbackMode = PlaybackMode::Hold;
    settings.maxVoices = static_cast<std::uint8_t>(test.voices);
    settings.tuneSemitones = std::min(test.totalTune, 24.0f);
    settings.armed = test.captured;
    engine.setSettings(settings);

    PadData source;
    source.sampleRate = test.sourceRate;
    source.frames = kSourceFrames;
    source.stereo.resize(kSourceFrames * 2);
    std::vector<float> inputLeft(kSourceFrames), inputRight(kSourceFrames);
    std::vector<float> discardLeft(kSourceFrames), discardRight(kSourceFrames);
    for (unsigned frame = 0; frame < kSourceFrames; ++frame) {
        inputLeft[frame] = source.stereo[frame * 2] =
            static_cast<float>(0.2 * std::sin(frame * .07) + .01 * std::cos(frame * .193));
        inputRight[frame] = source.stereo[frame * 2 + 1] =
            static_cast<float>(.17 * std::cos(frame * .09));
    }
    std::vector<MidiEvent> notes;
    notes.reserve(test.voices);
    for (unsigned pad = 0; pad < test.voices; ++pad) {
        notes.emplace_back(0U, static_cast<std::uint8_t>(36 + pad), 127U, MidiEventType::NoteOn);
        if (test.captured)
            engine.process(inputLeft.data(), inputRight.data(), discardLeft.data(),
                           discardRight.data(), kSourceFrames, &notes.back(), 1);
        else
            require(engine.importPad(pad, source), std::string(test.name) + ": import failed");
    }
    if (test.captured) {
        settings.armed = false;
        engine.setSettings(settings);
        engine.process(nullptr, nullptr, discardLeft.data(), discardRight.data(), 0);
        engine.setSampleRate(test.hostRate);
    }

    const auto stored = engine.padMetadata(0);
    require(stored.occupied && stored.frames > 0 && stored.sampleRate > 0,
            std::string(test.name) + ": no stored sample");
    if (options.expectCap96)
        require(stored.sampleRate <= 96000.0,
                std::string(test.name) + ": storage exceeds requested 96 kHz cap");
    // Conversion may change the frame count, but must preserve recording time.
    require(std::abs(stored.frames / stored.sampleRate - kSourceFrames / test.sourceRate)
                <= 2.0 / stored.sampleRate,
            std::string(test.name) + ": stored duration changed");
    for (unsigned pad = 0; pad < test.voices; ++pad) {
        const auto metadata = engine.padMetadata(pad);
        require(metadata.occupied && !metadata.recording && metadata.frames == stored.frames &&
                    metadata.sampleRate == stored.sampleRate,
                std::string(test.name) + ": inconsistent stored pads");
        engine.setPadPlaybackSettings(pad, {}); // Entire stored sample, unity sustain.
        sms::dsp::SampleMixerSettings mixer;
        mixer.tuneSemitones = std::max(0.0f, test.totalTune - 24.0f);
        engine.setPadMixerSettings(pad, mixer);
    }

    std::vector<float> left(test.block), right(test.block);
    engine.process(nullptr, nullptr, left.data(), right.data(), test.block,
                   notes.data(), static_cast<std::uint32_t>(notes.size()));
    for (unsigned i = 0; i < options.warmups; ++i)
        engine.process(nullptr, nullptr, left.data(), right.data(), test.block);
    requireActive(engine, test);

    std::vector<double> timings(options.iterations);
    double checksum = 0.0;
    for (auto& elapsed : timings) {
        const auto before = std::chrono::steady_clock::now();
        engine.process(nullptr, nullptr, left.data(), right.data(), test.block,
                       test.retrigger ? notes.data() : nullptr,
                       test.retrigger ? static_cast<std::uint32_t>(notes.size()) : 0U);
        elapsed = std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - before).count();
        // Validate every rendered frame outside the timed section.
        for (unsigned frame = 0; frame < test.block; ++frame) {
            if (!std::isfinite(left[frame]) || !std::isfinite(right[frame]))
                throw std::runtime_error(std::string(test.name) + ": non-finite output");
            checksum += static_cast<double>(left[frame]) + right[frame];
        }
    }
    requireActive(engine, test);
    std::sort(timings.begin(), timings.end());
    const double budget = test.block * 1e6 / test.hostRate;
    const double step = stored.sampleRate / test.hostRate * std::exp2(test.totalTune / 12.0);
    const auto p95Index = static_cast<std::size_t>(std::ceil(timings.size() * .95)) - 1;
    const double median = timings.size() % 2 ? timings[timings.size() / 2] :
        (timings[timings.size() / 2 - 1] + timings[timings.size() / 2]) * .5;
    std::cout << options.label << ',' << test.name << ',' << test.workload << ','
              << (test.captured ? "capture" : "import") << ',' << test.sourceRate << ','
              << stored.sampleRate << ',' << kSourceFrames << ',' << stored.frames << ','
              << test.hostRate << ',' << test.totalTune << ',' << step << ',' << test.voices << ','
              << test.block << ',' << options.warmups << ',' << options.iterations << ','
              << median << ',' << timings[p95Index] << ',' << timings.back() << ',' << budget << ','
              << std::count_if(timings.begin(), timings.end(),
                               [budget](const double time) { return time > budget; })
              << ',' << checksum << '\n' << std::flush;
}
} // namespace

int main(const int argc, char** argv) {
    try {
        Options options;
        for (int i = 1; i < argc; ++i) {
            const std::string option = argv[i];
            if (option == "--expect-cap96") options.expectCap96 = true;
            else if (option == "--help") {
                std::cout << "Usage: resampling_benchmarks [--iterations N] [--warmups N] "
                             "[--case NAME|all] [--label NAME] [--expect-cap96]\n"
                             "Outputs CSV. Imports/recording/setup are excluded from timing; "
                             "the retrigger case includes note-on processing.\n";
                return 0;
            } else {
                require(i + 1 < argc, "missing value for " + option);
                const std::string value = argv[++i];
                if (option == "--iterations") options.iterations = countArgument(value, false);
                else if (option == "--warmups") options.warmups = countArgument(value, true);
                else if (option == "--case") options.caseName = value;
                else if (option == "--label") options.label = value;
                else throw std::runtime_error("unknown option: " + option);
            }
        }
        require(options.label.find_first_of(",\r\n\"") == std::string::npos,
                "label must not contain CSV delimiters");
        std::cout << std::setprecision(12)
                  << "label,case,workload,storage,input_rate,stored_rate,input_frames,stored_frames,"
                     "host_rate,total_tune,step,voices,block,warmups,iterations,median_us,p95_us,"
                     "max_us,budget_us,over_budget,checksum\n";
        bool found = false;
        for (const auto& test : kCases)
            if (options.caseName == "all" || options.caseName == test.name) {
                run(test, options);
                found = true;
            }
        require(found, "unknown case: " + options.caseName);
    } catch (const std::exception& error) {
        std::cerr << "resampling benchmark: " << error.what() << '\n';
        return 1;
    }
}
