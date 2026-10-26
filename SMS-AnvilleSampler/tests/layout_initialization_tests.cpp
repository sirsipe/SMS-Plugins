#include "LayoutPreference.hpp"
#include "IO/FileIO.hpp"
#include "BankModePreference.hpp"
#include "Parameters.hpp"
#include "CaptureActionProtocol.hpp"
#include "GlobalValueMode.hpp"
#include "StateKeys.hpp"
#include "src/DistrhoPluginInternal.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
namespace fs = std::filesystem;
using midichopper::plugin::LayoutPreference;
using midichopper::plugin::BankModePreference;
using midichopper::plugin::kParameterMidiBankMode;
using midichopper::plugin::kParameterPadLayout;
using DISTRHO::PluginExporter;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

// Restore even on a failed assertion; every preference write stays under root.
struct Environment {
    std::string name;
    std::string previous;
    bool existed;
    explicit Environment(const char* key)
        : name(key), previous(std::getenv(key) ? std::getenv(key) : ""),
          existed(std::getenv(key) != nullptr) {}
    void set(const fs::path& path)
    {
        check(::setenv(name.c_str(), path.c_str(), 1) == 0, "isolate configuration environment");
    }
    ~Environment()
    {
        if (existed) ::setenv(name.c_str(), previous.c_str(), 1);
        else ::unsetenv(name.c_str());
    }
};
struct Fixture {
    fs::path root;
    Environment home{"HOME"};
    Environment config{"XDG_CONFIG_HOME"};
    Fixture()
    {
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            root = fs::temp_directory_path() / sms::io::pathFromUtf8(
                "sms-layout-init-雪-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(attempt));
            std::error_code error;
            if (fs::create_directory(root, error)) break;
            check(!error, "create isolated initialization directory");
            if (attempt == 99) throw std::runtime_error("unique initialization directory");
        }
        home.set(root);
        config.set(root / "config");
        const auto path = LayoutPreference::configPath();
        check(!path.empty() && path.lexically_relative(root).native().find("..") != 0,
              "preference path remains inside isolated configuration");
        DISTRHO::d_nextBufferSize = 512;
        DISTRHO::d_nextSampleRate = 44100.0;
        DISTRHO::d_nextPluginIsDummy = false;
    }
    ~Fixture()
    {
        DISTRHO::d_nextPluginIsDummy = false;
        std::error_code error;
        fs::remove_all(root, error);
    }
};
void layout(const PluginExporter& plugin, const int current, const int initial)
{
    check(plugin.getParameterValue(kParameterPadLayout) == static_cast<float>(current),
          "current pad layout follows startup choice or host override");
    check(plugin.getParameterDefault(kParameterPadLayout) == static_cast<float>(initial),
          "metadata default retains constructor layout");
    const auto& ranges = plugin.getParameterRanges(kParameterPadLayout);
    check(ranges.def == static_cast<float>(initial) && ranges.min == 0.0f && ranges.max == 2.0f,
          "layout metadata preserves enum range and initial default");
}
void bankMode(const PluginExporter& plugin, const int current, const int initial)
{
    check(plugin.getParameterValue(kParameterMidiBankMode) == static_cast<float>(current),
          "current bank mode follows startup choice or host override");
    check(plugin.getParameterDefault(kParameterMidiBankMode) == static_cast<float>(initial),
          "bank mode metadata retains constructor default");
    const auto& ranges = plugin.getParameterRanges(kParameterMidiBankMode);
    check(ranges.def == static_cast<float>(initial) && ranges.min == 0.0f && ranges.max == 1.0f,
          "bank mode metadata preserves enum range and initial default");
}

void initialization()
{
    Fixture fixture;
    const auto path = LayoutPreference::configPath();
    const auto bankPath = BankModePreference::configPath();
    const int factoryBank = static_cast<int>(midichopper::kDefaultMidiBankMode);
    check(path == bankPath, "startup preferences share one defaults file");
    {
        PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
        layout(plugin, 0, 0);
        bankMode(plugin, factoryBank, factoryBank);
        check(!fs::exists(path), "construction does not create a defaults file");
    }
    for (const int remembered : {1, 2}) {
        check(LayoutPreference::write(path, remembered), "prepare remembered startup layout");
        PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
        layout(plugin, remembered, remembered);
        const auto storedDefaults = sms::io::DefaultsFile::read(path);
        plugin.setParameterValue(kParameterPadLayout, 0.0f);
        layout(plugin, 0, remembered);
        check(sms::io::DefaultsFile::read(path) == storedDefaults,
              "host layout override does not write any defaults");
        check(LayoutPreference::write(path, remembered == 1 ? 2 : 1), "change preference after construction");
        layout(plugin, 0, remembered);
    }
    for (const int remembered : {0, 1}) {
        check(LayoutPreference::write(path, 2), "prepare sibling startup layout");
        check(BankModePreference::write(bankPath, remembered), "prepare remembered bank mode");
        PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
        layout(plugin, 2, 2);
        bankMode(plugin, remembered, remembered);
        const auto storedDefaults = sms::io::DefaultsFile::read(path);
        for (const int hostMode : {0, 1}) {
            plugin.setParameterValue(kParameterMidiBankMode, static_cast<float>(hostMode));
            bankMode(plugin, hostMode, remembered);
            layout(plugin, 2, 2);
            check(sms::io::DefaultsFile::read(path) == storedDefaults,
                  "host bank change does not write any defaults");
            check(LayoutPreference::read(path) == 2, "host bank change preserves layout preference");
        }
        check(BankModePreference::write(bankPath, 1 - remembered), "change bank preference after construction");
        bankMode(plugin, 1, remembered);
        layout(plugin, 2, 2);
        check(LayoutPreference::write(path, 1), "change sibling layout preference");
        layout(plugin, 2, 2);
        bankMode(plugin, 1, remembered);
    }
    check(sms::io::DefaultsFile::set(path, "future_option", "keep"), "prepare future sibling key");
    check(LayoutPreference::write(path, 2), "prepare preference for dummy metadata scan");
    check(BankModePreference::write(bankPath, 0), "prepare nonfactory bank mode for dummy scan");
    DISTRHO::d_nextPluginIsDummy = true;
    {
        PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
        layout(plugin, 0, 0);
        bankMode(plugin, factoryBank, factoryBank);
        check(BankModePreference::read(bankPath) == 0, "dummy scan preserves bank mode preference");
        check(LayoutPreference::read(path) == 2, "dummy metadata scan preserves user preference");
        check(sms::io::DefaultsFile::get(path, "future_option") == "keep", "dummy metadata preserves future key");
    }
    DISTRHO::d_nextPluginIsDummy = false;
}

void clearPads()
{
    using namespace midichopper::plugin;
    Fixture fixture;
    const auto path = LayoutPreference::configPath();
    check(LayoutPreference::write(path, 2) && BankModePreference::write(path, 0),
          "prepare startup defaults before clearing pads");
    check(sms::io::DefaultsFile::set(path, "future_option", "keep"), "prepare unknown clear sibling");
    const auto storedDefaults = sms::io::DefaultsFile::read(path);
    PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
    std::array<float, kParameterCount> controls{};
    std::array<bool, kParameterCount> persistent{};
    for (std::uint32_t index = 0; index < kParameterCount; ++index) {
        persistent[index] = plugin.isParameterInput(index) && !plugin.isParameterTrigger(index) &&
            (plugin.getParameterHints(index) & DISTRHO::kParameterIsHidden) == 0;
        if (!persistent[index]) continue;
        const auto range = parameterRange(index);
        plugin.setParameterValue(index,
            range.defaultValue == range.maximum ? range.minimum : range.maximum);
        controls[index] = plugin.getParameterValue(index);
    }
    std::array<std::string, kGlobalControlStateKeys.size()> settings{};
    for (std::size_t i = 0; i < settings.size(); ++i) {
        const auto range = parameterRange(kGlobalValueControls[i].parameter);
        settings[i] = encodeGlobalValueSettings({GlobalValueMode::quantized,
            range.minimum * 0.5f, range.maximum * 0.5f});
        plugin.setState(kGlobalControlStateKeys[i], settings[i].c_str());
    }
    plugin.setState(kPadColorStateKeys[63].c_str(), "3");
    const auto seeded = [&] {
        for (std::size_t i = 0; i < settings.size(); ++i)
            if (plugin.getStateValue(kGlobalControlStateKeys[i]) != settings[i].c_str()) return false;
        return plugin.getStateValue(kPadColorStateKeys[63].c_str()) == "3";
    };
    // VST3 serializes state requests on its control worker; LV2 is inline here.
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!seeded() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(seeded(), "seed nonfactory global knob settings and pad color");
    plugin.setState(kCaptureActionRequestKey, "reset"); // Retain the released private request name.
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (plugin.getStateValue(kPadColorStateKeys[63].c_str()) != "0" &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(plugin.getStateValue(kPadColorStateKeys[63].c_str()) == "0", "clear pads clears saved pad colors");
    check(plugin.getStateValue(kCaptureActionRequestKey).isEmpty(), "clear requests are never persisted");
    for (std::uint32_t index = 0; index < kParameterCount; ++index)
        if (persistent[index])
            check(plugin.getParameterValue(index) == controls[index], "clear pads preserves every global control");
    for (std::size_t i = 0; i < settings.size(); ++i)
        check(plugin.getStateValue(kGlobalControlStateKeys[i]) == settings[i].c_str(),
              "clear pads preserves global knob modes and ranges");
    check(sms::io::DefaultsFile::read(path) == storedDefaults, "clear pads preserves all saved startup defaults");
}

} // namespace

int main()
{
    try { initialization(); clearPads(); }
    catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "Startup preference initialization tests passed\n";
}
