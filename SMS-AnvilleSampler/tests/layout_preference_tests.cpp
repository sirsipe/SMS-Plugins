#include "LayoutPreference.hpp"
#include "BankModePreference.hpp"
#include "SkinCatalog.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
using midichopper::plugin::LayoutPreference;
using midichopper::plugin::BankModePreference;

void check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Fixture {
    fs::path root;
    Fixture()
    {
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            root = fs::temp_directory_path() / sms::io::pathFromUtf8(
                "sms-layout-Ääni-雪-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(attempt));
            std::error_code error;
            if (fs::create_directory(root, error)) return;
            check(!error, "create isolated Unicode temporary directory");
        }
        throw std::runtime_error("unique temporary directory");
    }
    ~Fixture() { std::error_code error; fs::remove_all(root, error); }
    static void file(const fs::path& path, const std::string& contents)
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        check(static_cast<bool>(stream), "write fixture bytes");
    }
};

void preferences()
{
    Fixture fixture;
    const auto path = fixture.root / sms::io::pathFromUtf8("設定-🔨") / "defaults.ini";
    check(LayoutPreference::read(path) == 0, "missing preference defaults to 16 pads");
    check(LayoutPreference::read({}) == 0, "empty path defaults to 16 pads");
    constexpr const char* expected[] = {"16", "12", "8"};
    for (int layout = 0; layout < 3; ++layout) {
        check(LayoutPreference::write(path, layout), "write each supported layout");
        check(LayoutPreference::read(path) == layout, "native Unicode preference roundtrip");
        check(sms::io::DefaultsFile::get(path, "layout") == expected[layout],
              "persist pad count, not parameter index");
    }
    for (const int invalid : {-1, 3, 100}) {
        check(!LayoutPreference::write(path, invalid), "reject invalid layout index");
        check(LayoutPreference::read(path) == 2, "invalid writes preserve remembered layout");
    }
    const std::string malformed[] = {
        "", "layout=", "layout=2\n", "layout=0\n", "layout=08\n",
        "layout=unknown\n", "other=8\n", std::string("layout=8") + '\0',
        "layout=8\nfuture=" + std::string(64 * 1024, 'x')};
    for (const auto& contents : malformed) {
        Fixture::file(path, contents);
        check(LayoutPreference::read(path) == 0, "malformed or oversized preference defaults to 16 pads");
    }
    check(!LayoutPreference::write({}, 1), "empty destination fails");
    check(!LayoutPreference::write(fixture.root, 1), "directory destination fails");
    const auto blocker = fixture.root / "not-a-directory";
    Fixture::file(blocker, "unchanged");
    check(!LayoutPreference::write(blocker / "defaults.ini", 1), "non-directory parent fails");
    check(LayoutPreference::read(fixture.root) == 0, "directory source defaults safely");

    const auto config = LayoutPreference::configPath();
    check(config == sms::ui::SkinPreference::configPath(), "skin and layout use same defaults file");
    check(config == BankModePreference::configPath(), "bank and layout use same defaults file");
    if (!config.empty()) {
        check(config.filename() == "defaults.ini", "preferences share defaults.ini");
        check(config.parent_path().filename() == "SMS-AnvilleSampler", "plugin-specific config folder");
    }
}
void bankModePreferences()
{
    Fixture fixture;
    const auto directory = fixture.root / sms::io::pathFromUtf8("設定-🔨");
    const auto path = directory / "defaults.ini";
    const int factory = static_cast<int>(midichopper::kDefaultMidiBankMode);
    check(factory == 1, "factory bank mode is All Banks");
    check(BankModePreference::read(path) == factory, "missing bank preference uses factory mode");
    check(BankModePreference::read({}) == factory, "empty bank preference path uses factory mode");
    check(LayoutPreference::write(path, 2), "prepare sibling layout preference");
    check(sms::ui::SkinPreference::write(path, "雪 🔨"), "prepare sibling skin preference");
    check(sms::io::DefaultsFile::set(path, "future_option", "future value"), "prepare unknown future preference");
    constexpr const char* expected[] = {"selected", "all"};
    for (int mode = 0; mode < 2; ++mode) {
        check(BankModePreference::write(path, mode), "write supported bank mode through Unicode path");
        check(BankModePreference::read(path) == mode, "remember each bank mode");
        check(sms::io::DefaultsFile::get(path, "bank_mode") == expected[mode],
              "bank preference persists stable symbolic mode");
        check(LayoutPreference::read(path) == 2, "bank write leaves layout key unchanged");
        check(sms::ui::SkinPreference::read(path) == "雪 🔨", "bank write preserves skin key");
        check(sms::io::DefaultsFile::get(path, "future_option") == "future value",
              "bank write preserves unknown future key");
    }
    check(BankModePreference::write(path, 0), "remember nonfactory bank mode");
    check(LayoutPreference::write(path, 1), "update sibling layout preference");
    check(sms::ui::SkinPreference::write(path, ""), "persist Default skin in shared file");
    check(BankModePreference::read(path) == 0, "layout and skin writes leave bank preference unchanged");
    check(LayoutPreference::read(path) == 1, "skin write preserves layout key");
    check(sms::io::DefaultsFile::get(path, "future_option") == "future value",
          "layout and skin writes preserve unknown future key");
    check(sms::io::DefaultsFile::read(path).count("skin") == 1, "Default retains explicit empty skin key");
    for (const int invalid : {-1, 2, 100}) {
        check(!BankModePreference::write(path, invalid), "reject invalid bank mode index");
        check(BankModePreference::read(path) == 0, "invalid bank writes preserve preference");
    }
    const std::string malformed[] = {
        "", "bank_mode=", "bank_mode=0\n", "bank_mode=Selected\n", "bank_mode=unknown\n",
        "other=selected\n", std::string("bank_mode=selected") + '\0',
        "bank_mode=selected\nfuture=" + std::string(64 * 1024, 'x')};
    for (const auto& contents : malformed) {
        Fixture::file(path, contents);
        check(BankModePreference::read(path) == factory, "invalid or oversized bank preference uses factory mode");
    }
    check(!BankModePreference::write({}, 0), "empty bank destination fails");
    check(!BankModePreference::write(directory, 0), "bank directory destination fails");
    const auto blocker = fixture.root / "blocker";
    Fixture::file(blocker, "unchanged");
    check(!BankModePreference::write(blocker / "defaults.ini", 0), "bank non-directory parent fails");
    check(BankModePreference::read(directory) == factory, "bank directory source uses factory mode");
}

} // namespace

int main()
{
    try { preferences(); bankModePreferences(); }
    catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
    std::cout << "Startup preference tests passed\n";
}
