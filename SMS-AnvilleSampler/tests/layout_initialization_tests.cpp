#include "LayoutPreference.hpp"
#include "BankModePreference.hpp"
#include "Parameters.hpp"
#include "src/DistrhoPluginInternal.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

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
    {
        PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
        layout(plugin, 0, 0);
        bankMode(plugin, factoryBank, factoryBank);
    }
    for (const int remembered : {1, 2}) {
        check(LayoutPreference::write(path, remembered), "prepare remembered startup layout");
        PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
        layout(plugin, remembered, remembered);
        plugin.setParameterValue(kParameterPadLayout, 0.0f);
        layout(plugin, 0, remembered);
        check(LayoutPreference::read(path) == remembered, "host override does not change user preference");
        check(LayoutPreference::write(path, remembered == 1 ? 2 : 1), "change preference after construction");
        layout(plugin, 0, remembered);
    }
    for (const int remembered : {0, 1}) {
        check(LayoutPreference::write(path, 2), "prepare independent startup layout");
        check(BankModePreference::write(bankPath, remembered), "prepare remembered bank mode");
        PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
        layout(plugin, 2, 2);
        bankMode(plugin, remembered, remembered);
        for (const int hostMode : {0, 1}) {
            plugin.setParameterValue(kParameterMidiBankMode, static_cast<float>(hostMode));
            bankMode(plugin, hostMode, remembered);
            layout(plugin, 2, 2);
            check(BankModePreference::read(bankPath) == remembered, "host bank change does not persist");
            check(LayoutPreference::read(path) == 2, "host bank change preserves layout preference");
        }
        check(BankModePreference::write(bankPath, 1 - remembered), "change bank preference after construction");
        bankMode(plugin, 1, remembered);
        layout(plugin, 2, 2);
        check(LayoutPreference::write(path, 1), "change layout preference independently");
        layout(plugin, 2, 2);
        bankMode(plugin, 1, remembered);
    }
    check(LayoutPreference::write(path, 2), "prepare preference for dummy metadata scan");
    check(BankModePreference::write(bankPath, 0), "prepare nonfactory bank mode for dummy scan");
    DISTRHO::d_nextPluginIsDummy = true;
    {
        PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
        layout(plugin, 0, 0);
        bankMode(plugin, factoryBank, factoryBank);
        check(BankModePreference::read(bankPath) == 0, "dummy scan preserves bank mode preference");
        check(LayoutPreference::read(path) == 2, "dummy metadata scan preserves user preference");
    }
    DISTRHO::d_nextPluginIsDummy = false;
}
} // namespace

int main()
{
    try { initialization(); }
    catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "Startup preference initialization tests passed\n";
}
