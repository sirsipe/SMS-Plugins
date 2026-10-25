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
    const auto path = fixture.root / sms::io::pathFromUtf8("設定-🔨") / "selected-layout";
    check(LayoutPreference::read(path) == 0, "missing preference defaults to 16 pads");
    check(LayoutPreference::read({}) == 0, "empty path defaults to 16 pads");
    constexpr const char* expected[] = {
        "SMS-LAYOUT-PREFERENCE 1\n16\n", "SMS-LAYOUT-PREFERENCE 1\n12\n",
        "SMS-LAYOUT-PREFERENCE 1\n8\n"};
    for (int layout = 0; layout < 3; ++layout) {
        check(LayoutPreference::write(path, layout), "write each supported layout");
        check(LayoutPreference::read(path) == layout, "native Unicode preference roundtrip");
        const auto bytes = sms::io::readBytes(path, 64);
        check(std::string(bytes.begin(), bytes.end()) == expected[layout],
              "persist versioned pad count, not parameter index");
    }
    for (const int invalid : {-1, 3, 100}) {
        check(!LayoutPreference::write(path, invalid), "reject invalid layout index");
        check(LayoutPreference::read(path) == 2, "invalid writes preserve remembered layout");
    }
    const std::string malformed[] = {
        "", "8\n", "SMS-LAYOUT-PREFERENCE 2\n8\n", "SMS-LAYOUT-PREFERENCE 1\n2\n",
        "SMS-LAYOUT-PREFERENCE 1\n0\n", "SMS-LAYOUT-PREFERENCE 1\n08\n",
        "SMS-LAYOUT-PREFERENCE 1\n8", "SMS-LAYOUT-PREFERENCE 1\r\n8\r\n",
        "SMS-LAYOUT-PREFERENCE 1\n 8\n", "SMS-LAYOUT-PREFERENCE 1\n8\nextra",
        "SMS-LAYOUT-PREFERENCE 1\n8\n\n", std::string(expected[2]) + '\0',
        std::string(64, 'x'), std::string(expected[2]) + std::string(65, 'x')};
    for (const auto& contents : malformed) {
        Fixture::file(path, contents);
        check(LayoutPreference::read(path) == 0, "malformed or oversized preference defaults to 16 pads");
    }
    check(!LayoutPreference::write({}, 1), "empty destination fails");
    check(!LayoutPreference::write(fixture.root, 1), "directory destination fails");
    const auto blocker = fixture.root / "not-a-directory";
    Fixture::file(blocker, "unchanged");
    check(!LayoutPreference::write(blocker / "selected-layout", 1), "non-directory parent fails");
    check(LayoutPreference::read(fixture.root) == 0, "directory source defaults safely");

    const auto config = LayoutPreference::configPath();
    const auto skin = sms::ui::SkinPreference::configPath();
    check(config.empty() == skin.empty(), "skin and layout agree on available config root");
    if (!config.empty()) {
        check(config.parent_path() == skin.parent_path(), "layout preference is skin preference sibling");
        check(config.filename() == "selected-layout", "layout uses independent filename");
        check(config.parent_path().filename() == "SMS-AnvilleSampler", "plugin-specific config folder");
    }
}
void bankModePreferences()
{
    Fixture fixture;
    const auto directory = fixture.root / sms::io::pathFromUtf8("設定-🔨");
    const auto path = directory / "selected-bank-mode";
    const auto layoutPath = directory / "selected-layout";
    const int factory = static_cast<int>(midichopper::kDefaultMidiBankMode);
    check(factory == 1, "factory bank mode is All Banks");
    check(BankModePreference::read(path) == factory, "missing bank preference uses factory mode");
    check(BankModePreference::read({}) == factory, "empty bank preference path uses factory mode");
    check(LayoutPreference::write(layoutPath, 2), "prepare independent layout preference");
    constexpr const char* expected[] = {
        "SMS-BANK-MODE-PREFERENCE 1\nselected\n", "SMS-BANK-MODE-PREFERENCE 1\nall\n"};
    for (int mode = 0; mode < 2; ++mode) {
        check(BankModePreference::write(path, mode), "write supported bank mode through Unicode path");
        check(BankModePreference::read(path) == mode, "remember each bank mode");
        const auto bytes = sms::io::readBytes(path, 64);
        check(std::string(bytes.begin(), bytes.end()) == expected[mode],
              "bank preference persists exact versioned symbolic mode");
        check(LayoutPreference::read(layoutPath) == 2, "bank write leaves layout preference unchanged");
    }
    check(BankModePreference::write(path, 0), "remember nonfactory bank mode");
    check(LayoutPreference::write(layoutPath, 1), "update independent layout preference");
    check(BankModePreference::read(path) == 0, "layout write leaves bank preference unchanged");
    for (const int invalid : {-1, 2, 100}) {
        check(!BankModePreference::write(path, invalid), "reject invalid bank mode index");
        check(BankModePreference::read(path) == 0, "invalid bank writes preserve preference");
    }
    const std::string malformed[] = {
        "", "selected\n", "SMS-BANK-MODE-PREFERENCE 2\nselected\n",
        "SMS-BANK-MODE-PREFERENCE 1\n0\n", "SMS-BANK-MODE-PREFERENCE 1\nSelected\n",
        "SMS-BANK-MODE-PREFERENCE 1\nselected", "SMS-BANK-MODE-PREFERENCE 1\n selected\n",
        "SMS-BANK-MODE-PREFERENCE 1\r\nselected\r\n", std::string(expected[0]) + '\0',
        std::string(expected[0]) + "extra", std::string(expected[0]) + std::string(65, 'x')};
    for (const auto& contents : malformed) {
        Fixture::file(path, contents);
        check(BankModePreference::read(path) == factory, "invalid or oversized bank preference uses factory mode");
    }
    check(!BankModePreference::write({}, 0), "empty bank destination fails");
    check(!BankModePreference::write(directory, 0), "bank directory destination fails");
    const auto blocker = fixture.root / "blocker";
    Fixture::file(blocker, "unchanged");
    check(!BankModePreference::write(blocker / "selected-bank-mode", 0), "bank non-directory parent fails");
    check(BankModePreference::read(directory) == factory, "bank directory source uses factory mode");
    const auto config = BankModePreference::configPath();
    const auto layout = LayoutPreference::configPath();
    const auto skin = sms::ui::SkinPreference::configPath();
    check(config.empty() == layout.empty() && config.empty() == skin.empty(),
          "all preferences agree on config availability");
    if (!config.empty()) {
        check(config == layout.parent_path() / "selected-bank-mode", "bank mode uses exact layout sibling path");
        check(config == skin.parent_path() / "selected-bank-mode", "bank mode uses exact skin sibling path");
    }
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
