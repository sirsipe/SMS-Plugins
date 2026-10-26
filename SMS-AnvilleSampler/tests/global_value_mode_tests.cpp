#include "GlobalValueMode.hpp"
#include "KnobPreference.hpp"
#include "IO/FileIO.hpp"
#include "CaptureActionProtocol.hpp"
#include "Audio/RealtimeLatestMailbox.hpp"
#include "StateCodec.hpp"
#include "StateKeys.hpp"
#include "PluginUiBridge.hpp"
#include "src/DistrhoPluginInternal.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace midichopper::plugin;
using DISTRHO::PluginExporter;

void check(const bool condition, const char* const message)
{
    if (!condition) throw std::runtime_error(message);
}

void close(const float actual, const float expected, const char* const message)
{
    check(std::isfinite(actual) && std::abs(actual - expected) < 1.0e-6f, message);
}

void helpers()
{
    check(globalControlIndex(kParameterMonitorGainDb) == 5U &&
          globalValueStep(kParameterMonitorGainDb) == 0.0f, "IN is excluded");
    for (const auto& control : kGlobalValueControls) {
        const auto p = control.parameter;
        const auto range = parameterRange(p);
        auto settings = defaultGlobalValueSettings(p);
        check(validGlobalValueSettings(p, settings), "full range Normal is valid");
        close(effectiveGlobalValue(p, 0.37f, settings), 0.37f, "default is exact identity");
        for (const float invalid : {std::numeric_limits<float>::quiet_NaN(),
                                  std::numeric_limits<float>::infinity()})
            close(effectiveGlobalValue(p, invalid, settings), range.defaultValue, "sanitize raw");
        settings.mode = GlobalValueMode::quantized;
        close(effectiveGlobalValue(p, control.step * 1.49f, settings), control.step, "nearest grid");
        close(effectiveGlobalValue(p, control.step * 1.51f, settings), control.step * 2, "nearest grid up");
        if (range.minimum < 0) {
            close(effectiveGlobalValue(p, -control.step * 1.51f, settings), -control.step * 2,
                  "negative quantization");
            check(!std::signbit(effectiveGlobalValue(p, -0.01f, settings)), "unsigned zero");
        }
        GlobalValueSettings restored;
        check(decodeGlobalValueSettings(p, encodeGlobalValueSettings(settings), restored) &&
              restored.mode == settings.mode && restored.minimum == settings.minimum &&
              restored.maximum == settings.maximum, "codec roundtrips exactly");
    }
    constexpr auto tune = kParameterGlobalTuneSemitones;
    GlobalValueSettings narrowed{GlobalValueMode::quantized, -12.0f, 12.0f};
    std::array<bool, 25> seen{};
    for (int midi = 0; midi < 128; ++midi) {
        const float raw = -24.0f + 48.0f * static_cast<float>(midi) / 127.0f;
        const auto value = effectiveGlobalValue(tune, raw, narrowed);
        seen[static_cast<int>(value) + 12] = true;
    }
    check(std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }),
          "all narrowed semitones reachable with MIDI127");
    GlobalValueSettings positive{GlobalValueMode::quantized, 0.2f, 1.2f};
    check(validGlobalValueSettings(tune, positive), "one in-range grid point is sufficient");
    close(effectiveGlobalValue(tune, -24.0f, positive), 1.0f, "lower quantized endpoint stays on grid");
    positive.maximum = 0.9f;
    check(!validGlobalValueSettings(tune, positive), "range with no grid point is invalid");
    positive.minimum = 0.99999988f;
    positive.maximum = 0.99999994f;
    check(!validGlobalValueSettings(tune, positive), "grid validation never uses a permissive epsilon");
    GlobalValueSettings fractionalGrid{GlobalValueMode::quantized, 0.3f, 0.4f};
    check(validGlobalValueSettings(kParameterGlobalPan, fractionalGrid), "float grid endpoints are inclusive");
    close(effectiveGlobalValue(kParameterGlobalPan, -1.0f, fractionalGrid), 0.3f,
          "represented lower grid endpoint remains available");
    GlobalValueSettings destination{GlobalValueMode::quantized, -12, 12};
    for (const auto* invalid : {"", "KC2;0;-12;12", "KC1;3;-12;12", "KC1;-1;-12;12",
         "KC1;0;nan;12", "KC1;0;-12;inf", "KC1;0;12;-12", "KC1;0;0;0",
         "KC1;0;-25;12", "KC1;0;-12;25", "KC1;2;1;12", "KC1;1;.2;.9",
         "KC1;0;-12;12;", "KC1;0; -12;12", "KC1;0;-12;12junk", "KC1;0;;12"}) {
        check(!decodeGlobalValueSettings(tune, invalid, destination), "invalid codec rejected");
        check(destination.mode == GlobalValueMode::quantized && destination.minimum == -12 &&
              destination.maximum == 12, "decode failure is transactional");
    }
    for (const auto settings : {GlobalValueSettings{GlobalValueMode::zeroSnap, -12, 12},
          GlobalValueSettings{GlobalValueMode::zeroSnap, -0.01f, 12},
          GlobalValueSettings{GlobalValueMode::zeroSnap, -12, 0.01f},
          GlobalValueSettings{GlobalValueMode::zeroSnap, 0, 12},
          GlobalValueSettings{GlobalValueMode::zeroSnap, -12, 0}}) {
        check(validGlobalValueSettings(tune, settings), "snap accepts asymmetric and zero endpoint");
        for (int i = 0; i <= 100; ++i) {
            const float displayed = settings.minimum + (settings.maximum - settings.minimum) * i / 100.0f;
            const float raw = rawGlobalValue(tune, displayed, settings);
            check(std::abs(effectiveGlobalValue(tune, raw, settings) - displayed) < 0.0001f,
                  "snap inverse returns audible units");
        }
        close(effectiveGlobalValue(tune, -24, settings), settings.minimum, "snap minimum reachable");
        close(effectiveGlobalValue(tune, 24, settings), settings.maximum, "snap maximum reachable");
        close(effectiveGlobalValue(tune, rawGlobalValue(tune, 0, settings), settings), 0,
              "snap zero inverse selects center of plateau");
    }
    GlobalValueSettings snap{GlobalValueMode::zeroSnap, -12, 12};
    for (const float mapped : {-1.2f, 0.0f, 1.2f}) {
        const float raw = mapped * 2;
        close(effectiveGlobalValue(tune, raw, snap), 0, "snap plateau includes boundaries");
        check(std::abs(effectiveGlobalValue(tune, raw - 0.0001f, snap)) < 0.0001f &&
              std::abs(effectiveGlobalValue(tune, raw + 0.0001f, snap)) < 0.0001f,
              "snap branches continuous at plateau");
    }
    GlobalValueSettings normal{GlobalValueMode::normal, -7.25f, 8.125f};
    check(decodeGlobalValueSettings(tune, encodeGlobalValueSettings(normal), destination) &&
          destination.minimum == normal.minimum && destination.maximum == normal.maximum,
          "fractional negative bounds survive codec");
    for (float raw : {-24.0f, -3.4f, 0.0f, 24.0f})
        check(std::abs(rawGlobalValue(tune, mappedGlobalValue(tune, raw, normal), normal) - raw) < 0.00001f,
              "normal narrowed range inversion");
}

void metadata()
{
    PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
    check(plugin.getParameterCount() == 71U, "settings add no DAW parameters");
    for (std::size_t i = 0; i < kGlobalValueControls.size(); ++i) {
        const auto defaults = defaultGlobalValueSettings(kGlobalValueControls[i].parameter);
        const auto encoded = encodeGlobalValueSettings(defaults);
        check(plugin.getStateValue(kGlobalControlStateKeys[i]) == encoded.c_str(), "durable defaults");
        bool found = false;
        for (std::uint32_t state = 0; state < plugin.getStateCount(); ++state) {
            if (plugin.getStateKey(state) != kGlobalControlStateKeys[i]) continue;
            found = true;
            check(plugin.getStateHints(state) == 0U &&
                  plugin.getStateDefaultValue(state) == encoded.c_str(), "private durable metadata");
        }
        check(found, "state key registered");
        auto changed = defaults;
        changed.mode = GlobalValueMode::zeroSnap;
        plugin.setState(kGlobalControlStateKeys[i], encodeGlobalValueSettings(changed).c_str());
        plugin.setState(kGlobalControlStateKeys[i], "KC1;0;0;0");
        check(plugin.getStateValue(kGlobalControlStateKeys[i]) == encodeGlobalValueSettings(changed).c_str(),
              "bad restore leaves durable settings intact");
        for (std::size_t j = i + 1; j < kGlobalValueControls.size(); ++j)
            check(plugin.getStateValue(kGlobalControlStateKeys[j]) == encodeGlobalValueSettings(
                defaultGlobalValueSettings(kGlobalValueControls[j].parameter)).c_str(), "independent settings");
    }
}

struct Environment {
    std::string name;
    std::string previous;
    bool existed;
    explicit Environment(const char* key)
        : name(key), previous(std::getenv(key) ? std::getenv(key) : ""),
          existed(std::getenv(key) != nullptr) {}
    void set(const std::filesystem::path& path)
    {
        check(::setenv(name.c_str(), path.c_str(), 1) == 0, "isolate knob configuration");
    }
    ~Environment()
    {
        if (existed) ::setenv(name.c_str(), previous.c_str(), 1);
        else ::unsetenv(name.c_str());
    }
};

struct PreferenceFixture {
    std::filesystem::path root;
    Environment home{"HOME"};
    Environment config{"XDG_CONFIG_HOME"};
    PreferenceFixture()
    {
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            root = std::filesystem::temp_directory_path() / sms::io::pathFromUtf8(
                "sms-knob-defaults-雪-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(attempt));
            std::error_code error;
            if (std::filesystem::create_directory(root, error)) break;
            check(!error && attempt < 99, "create isolated knob configuration directory");
        }
        home.set(root);
        config.set(root / "config");
        const auto path = KnobPreference::configPath();
        check(!path.empty() && path.lexically_relative(root).native().find("..") != 0,
              "knob preference path stays inside isolated configuration");
        DISTRHO::d_nextPluginIsDummy = false;
    }
    ~PreferenceFixture()
    {
        DISTRHO::d_nextPluginIsDummy = true;
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

using SettingsArray = decltype(defaultGlobalValueSettings());

void checkSettings(const PluginExporter& plugin, const SettingsArray& current,
                   const SettingsArray& initial)
{
    for (std::size_t i = 0; i < current.size(); ++i) {
        const auto encoded = encodeGlobalValueSettings(current[i]);
        check(plugin.getStateValue(kGlobalControlStateKeys[i]) == encoded.c_str(),
              "current knob settings follow startup snapshot or host restore");
        bool found = false;
        for (std::uint32_t state = 0; state < plugin.getStateCount(); ++state) {
            if (plugin.getStateKey(state) != kGlobalControlStateKeys[i]) continue;
            found = true;
            check(plugin.getStateHints(state) == 0U && plugin.getStateDefaultValue(state) ==
                  encodeGlobalValueSettings(initial[i]).c_str(),
                  "private knob metadata retains immutable startup snapshot");
        }
        check(found, "knob metadata registered");
    }
}

void preferences()
{
    PreferenceFixture fixture;
    const auto path = KnobPreference::configPath();
    const auto factory = defaultGlobalValueSettings();
    {
        PluginExporter missing(nullptr, nullptr, nullptr, nullptr);
        checkSettings(missing, factory, factory);
        check(!std::filesystem::exists(path), "missing preferences are never created on startup");
    }
    check(sms::io::DefaultsFile::set(path, "unrelated.setting", "keep"), "seed unrelated default");
    auto remembered = factory;
    for (std::size_t i = 0; i < remembered.size(); ++i) {
        remembered[i].mode = i % 2 ? GlobalValueMode::zeroSnap : GlobalValueMode::quantized;
        remembered[i].minimum *= 0.5f;
        remembered[i].maximum *= 0.5f;
        check(KnobPreference::write(path, kGlobalValueControls[i].parameter, remembered[i]),
              "write each valid knob preference");
    }
    const auto saved = sms::io::readBytes(path, 65536U);
    check(!saved.empty() && sms::io::DefaultsFile::get(path, "unrelated.setting") == "keep",
          "knob writes retain unrelated defaults");
    check(!KnobPreference::write(path, kParameterMonitorGainDb, remembered[0]) &&
          !KnobPreference::write(path, kParameterOutputGainDb,
                                {GlobalValueMode::normal, 1.0f, 1.0f}),
          "unsupported knobs and invalid settings never write");
    check(sms::io::readBytes(path, 65536U) == saved, "rejected writes leave file unchanged");
    PluginExporter first(nullptr, nullptr, nullptr, nullptr);
    PluginExporter second(nullptr, nullptr, nullptr, nullptr);
    checkSettings(first, remembered, remembered);
    checkSettings(second, remembered, remembered);
    for (std::size_t i = 0; i < factory.size(); ++i)
        first.setState(kGlobalControlStateKeys[i], encodeGlobalValueSettings(factory[i]).c_str());
    checkSettings(first, factory, remembered);
    check(sms::io::readBytes(path, 65536U) == saved,
          "host factory state overrides preference without writing defaults");
    auto edited = factory;
    for (std::size_t i = 0; i < edited.size(); ++i) {
        edited[i].mode = GlobalValueMode::zeroSnap;
        first.setState(kGlobalControlStateKeys[i], encodeGlobalValueSettings(edited[i]).c_str());
    }
    checkSettings(first, edited, remembered);
    checkSettings(second, remembered, remembered);
    check(sms::io::readBytes(path, 65536U) == saved,
          "host nonfactory state never writes defaults");
    for (std::size_t i = 0; i < edited.size(); ++i)
        check(KnobPreference::write(path, kGlobalValueControls[i].parameter, edited[i]),
              "explicit settings choice persists new-instance default");
    checkSettings(first, edited, remembered);
    checkSettings(second, remembered, remembered);
    {
        PluginExporter fresh(nullptr, nullptr, nullptr, nullptr);
        checkSettings(fresh, edited, edited);
    }
    DISTRHO::d_nextPluginIsDummy = true;
    {
        const auto before = sms::io::readBytes(path, 65536U);
        PluginExporter dummy(nullptr, nullptr, nullptr, nullptr);
        checkSettings(dummy, factory, factory);
        check(sms::io::readBytes(path, 65536U) == before, "dummy discovery preserves preferences");
    }
    DISTRHO::d_nextPluginIsDummy = false;
    constexpr const char* invalid[]{"KC1;0;0;0", "KC1;9;-1;1", "KC1;1;nan;1",
                                    "KC1;0;-999;1", "KC1;0;0;1;extra"};
    for (std::size_t i = 0; i < factory.size(); ++i)
        check(sms::io::DefaultsFile::set(path, KnobPreference::keys[i], invalid[i]),
              "prepare malformed preference per knob");
    {
        PluginExporter malformed(nullptr, nullptr, nullptr, nullptr);
        checkSettings(malformed, factory, factory);
    }
    check(KnobPreference::write(path, kParameterGlobalPan, remembered[1]),
          "one valid knob coexists with malformed entries");
    auto mixed = factory;
    mixed[1] = remembered[1];
    PluginExporter partial(nullptr, nullptr, nullptr, nullptr);
    checkSettings(partial, mixed, mixed);
}

constexpr std::uint32_t kFrames = 128U;
using Audio = std::array<std::array<float, kFrames>, 2>;

Audio render(PluginExporter& plugin, const bool trigger = false)
{
    const Audio input{};
    Audio output{};
    const float* inputs[]{input[0].data(), input[1].data()};
    float* outputs[]{output[0].data(), output[1].data()};
    DISTRHO::MidiEvent note{};
    note.size = 3U;
    note.data[0] = 0x90U;
    note.data[1] = midichopper::kDefaultBaseMidiNote;
    note.data[2] = 127U;
    plugin.run(inputs, outputs, kFrames, trigger ? &note : nullptr, trigger ? 1U : 0U);
    return output;
}

float difference(const Audio& left, const Audio& right)
{
    float result = 0.0f;
    for (std::size_t channel = 0; channel < left.size(); ++channel)
        for (std::size_t frame = 0; frame < kFrames; ++frame) {
            check(std::isfinite(left[channel][frame]) && std::isfinite(right[channel][frame]),
                  "global modes produce finite audio");
            result = std::max(result, std::abs(left[channel][frame] - right[channel][frame]));
        }
    return result;
}

void configure(PluginExporter& plugin)
{
    plugin.setParameterValue(kParameterInputMonitor, 0.0f);
    plugin.setParameterValue(kParameterOutputGainDb, -3.37f);
    plugin.setParameterValue(kParameterGlobalPan, 0.263f);
    plugin.setParameterValue(kParameterGlobalTuneSemitones, 2.37f);
    plugin.setParameterValue(kParameterGlobalLowpass, 0.263f);
    plugin.setParameterValue(kParameterGlobalHighpass, 0.163f);
    midichopper::PadData pad;
    pad.frames = 32768U;
    pad.stereo.resize(pad.frames * 2U);
    for (std::uint32_t frame = 0; frame < pad.frames; ++frame) {
        pad.stereo[frame * 2U] = 0.3f * std::sin(static_cast<float>(frame) * 0.19f) +
                                  0.12f * std::sin(static_cast<float>(frame) * 0.073f);
        pad.stereo[frame * 2U + 1U] = 0.27f * std::cos(static_cast<float>(frame) * 0.137f);
    }
    const auto audio = encodePadState(pad, 48000U);
    plugin.setState(kPadStateKeys[0].c_str(), audio.c_str());
    sms::dsp::SampleMixerSettings mixer;
    mixer.pan = 0.073f;
    mixer.tuneSemitones = 0.37f;
    mixer.lowpass = 0.043f;
    mixer.highpass = 0.017f;
    const auto settings = encodeMixerSettings(mixer);
    plugin.setState(kPadMixerStateKeys[0].c_str(), settings.c_str());
}

void audioModes()
{
    constexpr float automated[]{-5.63f, -0.263f, -2.63f, 0.363f, 0.263f};
    for (std::size_t i = 0; i < kGlobalValueControls.size(); ++i) {
        for (const auto mode : {GlobalValueMode::normal, GlobalValueMode::quantized, GlobalValueMode::zeroSnap}) {
            PluginExporter actual(nullptr, nullptr, nullptr, nullptr);
            PluginExporter expected(nullptr, nullptr, nullptr, nullptr);
            configure(actual); configure(expected);
            const auto parameter = kGlobalValueControls[i].parameter;
            auto settings = defaultGlobalValueSettings(parameter);
            settings.mode = mode;
            // Narrow each control's range, preserving zero for Snap.
            settings.minimum *= 0.5f;
            settings.maximum *= 0.5f;
            const auto encoded = encodeGlobalValueSettings(settings);
            actual.setState(kGlobalControlStateKeys[i], encoded.c_str());
            const float raw = actual.getParameterValue(parameter);
            expected.setParameterValue(parameter, effectiveGlobalValue(parameter, raw, settings));
            check(difference(render(actual, true), render(expected, true)) < 1.0e-6f,
                  "restored mapping and mode apply before first audio");
            check(actual.getParameterValue(parameter) == raw, "raw value preserved");
            if (mode == GlobalValueMode::zeroSnap) {
                const float zero = rawGlobalValue(parameter, 0.0f, settings);
                actual.setParameterValue(parameter, zero);
                expected.setParameterValue(parameter, 0.0f);
                check(difference(render(actual), render(expected)) < 1.0e-6f,
                      "snap plateau audibly selects zero on an active voice");
            }
            actual.setParameterValue(parameter, automated[i]);
            expected.setParameterValue(parameter, effectiveGlobalValue(parameter, automated[i], settings));
            check(difference(render(actual), render(expected)) < 1.0e-6f, "automated raw maps live");
            const auto defaults = encodeGlobalValueSettings(defaultGlobalValueSettings(parameter));
            actual.setState(kGlobalControlStateKeys[i], defaults.c_str());
            expected.setParameterValue(parameter, automated[i]);
            check(difference(render(actual), render(expected)) < 1.0e-6f,
                  "return to full Normal restores precision on active voices");
            check(actual.getParameterValue(parameter) == automated[i], "automation remains raw");
        }
    }
}

void startupPreferenceAudio()
{
    PreferenceFixture fixture;
    const auto path = KnobPreference::configPath();
    auto settings = defaultGlobalValueSettings();
    for (std::size_t i = 0; i < settings.size(); ++i) {
        settings[i].mode = GlobalValueMode::quantized;
        settings[i].minimum *= 0.5f;
        settings[i].maximum *= 0.5f;
        check(KnobPreference::write(path, kGlobalValueControls[i].parameter, settings[i]),
              "prepare startup DSP defaults");
    }
    PluginExporter actual(nullptr, nullptr, nullptr, nullptr);
    DISTRHO::d_nextPluginIsDummy = true;
    PluginExporter expected(nullptr, nullptr, nullptr, nullptr);
    configure(actual);
    configure(expected);
    for (std::size_t i = 0; i < settings.size(); ++i) {
        const auto parameter = kGlobalValueControls[i].parameter;
        expected.setParameterValue(parameter,
            effectiveGlobalValue(parameter, actual.getParameterValue(parameter), settings[i]));
    }
    check(difference(render(actual, true), render(expected, true)) < 1.0e-6f,
          "startup preferences seed audio-owned settings before first callback");
}

struct EchoFixture {
    PluginExporter* plugin = nullptr;
    int publications = 0;
    static bool echo(void* opaque, const char* key, const char* value)
    {
        auto& fixture = *static_cast<EchoFixture*>(opaque);
        ++fixture.publications;
        // LV2 synchronously reenters the adapter after accepting a DSP update.
        fixture.plugin->setState(key, value);
        return true;
    }
};

void stateTransport()
{
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
    PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
    auto* bridge = static_cast<DISTRHO::PluginUiBridge*>(plugin.getInstancePointer());
    auto firstView = bridge->uiMessageCursor();
    auto secondView = firstView;
    const auto settings = GlobalValueSettings{GlobalValueMode::zeroSnap, -8.0f, 3.0f};
    const auto encoded = encodeGlobalValueSettings(settings);
    plugin.setState(kGlobalControlStateKeys[0], encoded.c_str());
    for (auto* cursor : {&firstView, &secondView}) {
        UiMessageBus::Message message;
        bool skipped = true;
        check(bridge->readUiMessage(*cursor, message, skipped) && !skipped &&
              std::string(message.key.data()) == kGlobalControlStateKeys[0] &&
              std::string(message.value.data()) == encoded,
              "VST3 fallback returns accepted config independently to multiple views");
        check(!bridge->readUiMessage(*cursor, message, skipped), "config published exactly once");
    }
#else
    EchoFixture fixture;
    PluginExporter plugin(&fixture, nullptr, nullptr, EchoFixture::echo);
    fixture.plugin = &plugin;
    const auto settings = GlobalValueSettings{GlobalValueMode::zeroSnap, -8.0f, 3.0f};
    const auto encoded = encodeGlobalValueSettings(settings);
    plugin.setState(kGlobalControlStateKeys[0], encoded.c_str());
    check(fixture.publications == 1 && plugin.getStateValue(kGlobalControlStateKeys[0]) == encoded.c_str(),
          "LV2 publication echo does not recursively apply the accepted update");
#endif
}

void clearPadsPreservesSettings()
{
    PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
    configure(plugin);
    std::array<float, kParameterCount> controls{};
    for (std::uint32_t index = 0; index < kParameterCount; ++index) {
        if (!plugin.isParameterInput(index) || plugin.isParameterTrigger(index) ||
            (plugin.getParameterHints(index) & DISTRHO::kParameterIsHidden) != 0)
            continue;
        const auto range = parameterRange(index);
        // Keep PLAY selected so processing the worker request cannot capture new audio.
        const float value = index == kParameterMode ? 0.0f :
            (range.defaultValue == range.maximum ? range.minimum : range.maximum);
        plugin.setParameterValue(index, value);
        controls[index] = plugin.getParameterValue(index);
    }
    auto settings = defaultGlobalValueSettings();
    for (std::size_t i = 0; i < kGlobalValueControls.size(); ++i) {
        settings[i].mode = GlobalValueMode::quantized;
        plugin.setState(kGlobalControlStateKeys[i], encodeGlobalValueSettings(settings[i]).c_str());
    }
    plugin.setState(kPadColorStateKeys[0].c_str(), "3");
    plugin.setState(kPadEditStateKeys[0].c_str(), "SP1;0.25;0.75;0;0;1;0.2");
    check(!plugin.getStateValue(kPadStateKeys[0].c_str()).isEmpty(), "pad audio exists before clearing");
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
    auto* bridge = static_cast<DISTRHO::PluginUiBridge*>(plugin.getInstancePointer());
    auto cursor = bridge->uiMessageCursor();
    bool completed = false;
#else
    bool completed = true; // LV2 handles this request synchronously.
#endif
    plugin.setState(kCaptureActionRequestKey, "reset");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    // State snapshots take the workflow mutex held by the clear worker while
    // it awaits audio. Poll its final UI publication so audio can keep running.
    while (!completed && std::chrono::steady_clock::now() < deadline) {
        static_cast<void>(render(plugin));
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        UiMessageBus::Message message;
        bool skipped = false;
        while (bridge->readUiMessage(cursor, message, skipped)) {
            check(!skipped, "CLEAR PADS completion publication remains available");
            if (std::string(message.key.data()) == kPadFileStatusKey) {
                check(std::string(message.value.data()) == "Pads cleared", "CLEAR PADS succeeds");
                completed = true;
            }
        }
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(completed, "CLEAR PADS finishes before assertions");
    for (std::size_t i = 0; i < kGlobalValueControls.size(); ++i)
        check(plugin.getStateValue(kGlobalControlStateKeys[i]) ==
              encodeGlobalValueSettings(settings[i]).c_str(), "CLEAR PADS preserves private knob settings");
    for (std::uint32_t index = 0; index < kParameterCount; ++index)
        if (plugin.isParameterInput(index) && !plugin.isParameterTrigger(index) &&
            (plugin.getParameterHints(index) & DISTRHO::kParameterIsHidden) == 0)
            check(plugin.getParameterValue(index) == controls[index],
                  "CLEAR PADS preserves every persistent global control including layout/bank mode");
    check(plugin.getStateValue(kPadColorStateKeys[0].c_str()) == "0", "CLEAR PADS clears pad colors");
    check(plugin.getStateValue(kPadStateKeys[0].c_str()).isEmpty(), "CLEAR PADS clears pad PCM");
    check(plugin.getStateValue(kPadEditStateKeys[0].c_str()) == "SP1;0;1;0;0;1;0",
          "CLEAR PADS clears per-pad playback edits");
    check(plugin.getStateValue(kPadMixerStateKeys[0].c_str()) ==
          encodeMixerSettings(sms::dsp::SampleMixerSettings{}).c_str(),
          "CLEAR PADS clears per-pad mixer edits");
}

void coherentMailbox()
{
    using Settings = decltype(defaultGlobalValueSettings());
    sms::audio::RealtimeLatestMailbox<Settings> mailbox;
    std::atomic<bool> finished{false};
    std::thread writer([&] {
        for (int iteration = 0; iteration < 10000; ++iteration) {
            Settings value{};
            for (auto& control : value) control.minimum = static_cast<float>(iteration);
            mailbox.publish(value);
        }
        finished.store(true, std::memory_order_release);
    });
    bool coherent = true;
    while (!finished.load(std::memory_order_acquire)) {
        Settings value;
        if (mailbox.tryConsume(value))
            for (const auto& control : value)
                coherent = coherent && control.minimum == value[0].minimum;
    }
    writer.join();
    check(coherent, "all-control snapshots remain coherent under concurrent publication");
}
} // namespace

int main()
{
    try {
        DISTRHO::d_nextBufferSize = kFrames;
        DISTRHO::d_nextSampleRate = 48000.0;
        DISTRHO::d_nextPluginIsDummy = true; // Factory defaults, independent of user preferences.
        helpers(); preferences(); metadata(); audioModes(); startupPreferenceAudio(); stateTransport();
        clearPadsPreservesSettings(); coherentMailbox();
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "Global value mode helper, metadata, and adapter audio tests passed\n";
}
