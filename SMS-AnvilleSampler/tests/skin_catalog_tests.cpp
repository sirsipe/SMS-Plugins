#include "SkinCatalog.hpp"
#include "SkinFileIO.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
using sms::ui::Skin;
using sms::ui::SkinCatalog;
using sms::ui::SkinPreference;
using sms::ui::skinFileIO::pathFromUtf8;
using sms::ui::skinFileIO::pathToUtf8;
void check(bool value, const char* message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(EXIT_FAILURE); }
}
struct Fixture {
    fs::path root;
    Fixture()
    {
        static std::atomic<unsigned> sequence{0};
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            root = fs::temp_directory_path() / pathFromUtf8(
                "sms-catalog-Ääni-雪-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(sequence.fetch_add(1)));
            std::error_code error;
            if (fs::create_directory(root, error)) return;
            check(!error, "create portable temporary directory");
        }
        check(false, "unique portable temporary directory");
    }
    ~Fixture() { std::error_code error; fs::remove_all(root, error); }
    static void file(const fs::path& path, const std::string& value)
    {
        fs::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream << value;
        check(static_cast<bool>(stream), "fixture file written");
    }
    void skin(std::string_view id, std::string_view name)
    {
        file(root / "skins" / pathFromUtf8(id) / "skin.ini", "[skin]\nversion=1\nname=" + std::string(name) + "\n");
    }
};
bool symlink(const fs::path& target, const fs::path& link, bool directory = false)
{
    std::error_code error;
    if (directory) fs::create_directory_symlink(target, link, error);
    else fs::create_symlink(target, link, error);
    if (!error) return true;
#ifdef _WIN32
    // Win32 ERROR_PRIVILEGE_NOT_HELD has no reliable generic-category mapping.
    constexpr int privilegeNotHeld = 1314;
    check(error == std::errc::permission_denied || error == std::errc::operation_not_permitted ||
          error == std::errc::operation_not_supported || error == std::errc::function_not_supported ||
          error.value() == privilegeNotHeld, "unexpected Windows symlink fixture failure");
    std::cout << "SKIP: symlink creation unavailable for " << pathToUtf8(link)
              << ": " << error.message() << '\n';
    return false;
#else
    check(false, "POSIX symlink fixture creation succeeds");
    return false;
#endif
}
void names()
{
    Fixture f;
    const auto path = f.root / "skin";
    Fixture::file(path / "skin.ini", "[skin]\nversion=1\nname=  Ääni ☃ \t\n");
    check(Skin::load(path).name == "Ääni ☃", "trimmed UTF8 display name");
    Fixture::file(path / "skin.ini", "[skin]\nversion=1\n");
    check(Skin::load(path).valid() && Skin::load(path).name.empty(), "legacy missing name remains valid");
    for (const std::string& name : {std::string{}, std::string(513, 'x'), std::string("\xc0\xaf"), std::string("x\x7f"), std::string("x\nname=y")}) {
        Fixture::file(path / "skin.ini", "[skin]\nversion=1\nname=" + name + "\n");
        check(!Skin::load(path).valid(), "invalid or duplicate name rejects snapshot");
    }
    Fixture::file(path / "skin.ini", "[skin]\nversion=1\nname=" + std::string(512, 'x') + "\n");
    check(Skin::load(path).valid(), "exact name byte bound accepted");
}
void catalog()
{
    Fixture f;
    auto c = SkinCatalog::discover(f.root / "skin");
    check(!c.forced && !c.selectable() && c.entries.size() == 1, "empty catalog keeps Default");
    check(c.entries[0].id.empty() && c.entries[0].name == "Default" &&
          c.entries[0].directory.empty() && !c.load(0).valid(), "Default has no external paths");
    f.skin("z", "Alpha"); f.skin("a", "Alpha"); f.skin("middle", "Zulu");
    Fixture::file(f.root / "skins" / "legacy" / "skin.ini", "[skin]\nversion=1\n");
    Fixture::file(f.root / "skins" / "future" / "skin.ini", "[skin]\nversion=2\nname=Future\n");
    Fixture::file(f.root / "skins" / "broken" / "skin.ini", "[skin]\nversion=1\nname=Broken\nbroken\n");
    fs::create_directories(f.root / "skins" / "missing");
    c = SkinCatalog::discover(f.root / "skin");
    check(c.selectable() && c.entries.size() == 4 && c.entries[1].id == "a" &&
          c.entries[2].id == "z" && c.entries[3].id == "middle", "names sorted with stable ID tie break; invalid entries skipped");
    check(c.selectedIndex("middle") == 3 && c.selectedIndex("gone") == 0 &&
          c.selectedIndex("") == 0 && c.load(2).name == "Alpha" && !c.load(100).valid(), "remembered ID resolves with Default fallback");
    Fixture::file(f.root / "skin", "regular file");
    check(SkinCatalog::discover(f.root / "skin").selectable(), "regular skin file does not force selection");
    fs::remove(f.root / "skin"); fs::create_directory(f.root / "skin");
    c = SkinCatalog::discover(f.root / "skin");
    check(c.forced && !c.selectable() && !c.load(3).valid(), "invalid forced directory suppresses catalog");
    Fixture::file(f.root / "skin" / "skin.ini", "[skin]\nversion=1\n[colors]\ncanvas=#123456\n");
    check(SkinCatalog::discover(f.root / "skin").load(0).valid(), "legacy forced manifest loads without name");
    check(SkinCatalog::collectionDirectory(f.root / "SMS-AnvilleSampler.skin") ==
          f.root / "SMS-AnvilleSampler.skins", "single-file CLAP collection path");
    fs::rename(f.root / "skins", f.root / "SMS-AnvilleSampler.skins");
    check(SkinCatalog::discover(f.root / "SMS-AnvilleSampler.skin").selectable(), "single-file collection discovered");
    check(!SkinCatalog::discover({}).selectable(), "missing plugin path cannot discover cwd skins");
}
void unicodeCatalog()
{
    Fixture f;
    constexpr std::string_view id = "雪 🔨";
    f.skin(id, "Unicode skin 雪");
    const auto c = SkinCatalog::discover(f.root / "skin");
    check(c.entries.size() == 2 && c.entries[1].id == id && c.selectedIndex(id) == 1,
          "Unicode collection folder discovered as UTF8 ID");
    check(pathToUtf8(c.entries[1].directory.filename()) == id && c.load(1).name == "Unicode skin 雪",
          "Unicode catalog directory and manifest load");
    const auto preference = f.root / pathFromUtf8("設定") / "defaults.ini";
    check(SkinPreference::write(preference, c.entries[1].id) &&
          c.selectedIndex(SkinPreference::read(preference)) == 1,
          "Unicode catalog selection survives preference roundtrip");
}
void confinementAndBounds()
{
    Fixture f;
    f.skin("inside", "Inside");
    Fixture::file(f.root / "outside" / "skin.ini", "[skin]\nversion=1\nname=Outside\n");
    symlink(f.root / "outside", f.root / "skins" / "escape", true);
    fs::create_directories(f.root / "skins" / "manifest-escape");
    symlink(f.root / "outside" / "skin.ini", f.root / "skins" / "manifest-escape" / "skin.ini");
    auto c = SkinCatalog::discover(f.root / "skin");
    check(c.entries.size() == 2, "directory and manifest symlink escapes excluded");
    fs::rename(f.root / "skins", f.root / "actual");
    if (symlink(f.root / "actual", f.root / "skins", true))
        check(SkinCatalog::discover(f.root / "skin").entries.size() == 2, "symlink collection root resolves safely");
    else fs::rename(f.root / "actual", f.root / "skins");
    for (int i = 0; i < 150; ++i) f.skin("bounded-" + std::to_string(1000 + i), "Bounded");
    c = SkinCatalog::discover(f.root / "skin");
    check(c.entries.size() == 152 && c.entries[150].id == "bounded-1149",
          "all valid entries supported beyond 128 names");
    Fixture invalid;
    for (int i = 0; i < 150; ++i)
        fs::create_directories(invalid.root / "skins" / ("invalid-" + std::to_string(i)));
    invalid.skin("z-valid", "Valid");
    c = SkinCatalog::discover(invalid.root / "skin");
    check(c.selectable() && c.entries.size() == 2 && c.entries[1].id == "z-valid",
          "invalid directories cannot hide later valid skin");
}
void preferences()
{
    Fixture f;
    const auto path = f.root / pathFromUtf8("設定") / pathFromUtf8("選択-skin");
    check(SkinPreference::read(path).empty(), "missing preference defaults");
    check(SkinPreference::write(path, "Ääni folder") && SkinPreference::read(path) == "Ääni folder", "folder ID roundtrip");
    for (const auto* id : {"../escape", "/absolute", ".", "..", "nested/name", "back\\slash", "bad\nline"}) {
        check(!SkinPreference::write(path, id) && SkinPreference::read(path) == "Ääni folder", "unsafe preference cannot replace previous file");
    }
    check(!SkinPreference::write(path, std::string(513, 'x')), "oversize ID rejected");
    check(SkinPreference::write(path, "雪 🔨") && SkinPreference::read(path) == "雪 🔨",
          "Unicode preference overwrites existing file");
    check(sms::io::DefaultsFile::get(path, "skin") == "雪 🔨", "Unicode ID stored under skin key");
    for (const auto* id : {" leading and trailing ", "a=b", "#folder", ";folder", "\"quoted\""}) {
        check(SkinCatalog::safeId(id) && SkinPreference::write(path, id) &&
              SkinPreference::read(path) == id, "INI quoting preserves every safe folder ID");
    }
#ifdef _WIN32
    check(SkinPreference::write(path, "old"), "initial held-reader preference");
    const auto previousBytes = sms::io::readBytes(path, 64 * 1024);
    const HANDLE heldReader = ::CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    check(heldReader != INVALID_HANDLE_VALUE, "open Windows preference reader with delete sharing");
    check(SkinPreference::write(path, "new") && SkinPreference::read(path) == "new",
          "Windows atomic replacement publishes new preference while reader remains open");
    char heldBytes[128]{};
    DWORD heldCount = 0;
    check(::ReadFile(heldReader, heldBytes, static_cast<DWORD>(sizeof(heldBytes)), &heldCount, nullptr) != 0 &&
          std::string(heldBytes, heldCount) == std::string(previousBytes.begin(), previousBytes.end()),
          "held Windows reader retains complete previous preference after replacement");
    check(::CloseHandle(heldReader) != 0, "close held Windows preference reader");
#endif
    check(SkinPreference::write(path, "") && SkinPreference::read(path).empty(), "Default persists empty ID");
    check(sms::io::DefaultsFile::read(path).count("skin") == 1, "Default persists explicit empty skin key");
    for (const auto* malformed : {"folder\n", "other=folder\n", "skin=../bad\n", "skin=nested/name\n", "skin=\"unterminated\n"}) {
        Fixture::file(path, malformed);
        check(SkinPreference::read(path).empty(), "malformed preference defaults");
    }
    Fixture::file(path, "skin=folder\nfuture=" + std::string(64 * 1024, 'x'));
    check(SkinPreference::read(path).empty(), "oversize preference defaults");
    Fixture::file(f.root / "blocked", "file");
    check(!SkinPreference::write(f.root / "blocked" / "choice", "skin"), "parent directory error handled");
    fs::create_directory(f.root / "target-directory");
    check(!SkinPreference::write(f.root / "target-directory", "skin") &&
          fs::is_directory(f.root / "target-directory"), "failed atomic rename preserves destination");
    check(!SkinPreference::write({}, "skin"), "empty preference path rejected");
    fs::remove(path); // An oversized store cannot be safely merged by a preference writer.
    check(SkinPreference::write(path, "a"), "initial concurrent preference");
    check(sms::io::DefaultsFile::set(path, "future_option", "keep"), "prepare unknown concurrent sibling");
    auto writer = [&](const char* id) { for (int i = 0; i < 100; ++i) check(SkinPreference::write(path, id), "concurrent write succeeds"); };
    std::thread first(writer, "a"), second(writer, "b");
    std::thread sibling([&] {
        for (int i = 0; i < 100; ++i)
            check(sms::io::DefaultsFile::set(path, "bank_mode", "selected"), "concurrent sibling write succeeds");
    });
    for (int i = 0; i < 200; ++i) {
        const auto id = SkinPreference::read(path);
        check(id == "a" || id == "b", "reader observes only complete concurrent writes");
    }
    first.join(); second.join(); sibling.join();
    check(sms::io::DefaultsFile::get(path, "bank_mode") == "selected" &&
          sms::io::DefaultsFile::get(path, "future_option") == "keep",
          "concurrent skin writes preserve sibling and unknown keys");
    check(SkinPreference::write(path, "Ääni folder"), "initial Unicode concurrent preference");
    std::thread unicodeFirst(writer, "Ääni folder"), unicodeSecond(writer, "雪 🔨");
    for (int i = 0; i < 200; ++i) {
        const auto id = SkinPreference::read(path);
        check(id == "Ääni folder" || id == "雪 🔨", "reader observes only complete Unicode concurrent writes");
    }
    unicodeFirst.join(); unicodeSecond.join();
    for (const auto& entry : fs::directory_iterator(path.parent_path()))
        check(entry.path() == path, "atomic writes leave no temporary files");
}
struct Environment {
    std::string key, old; bool existed;
    explicit Environment(const char* name): key(name), existed(std::getenv(name) != nullptr)
    { if (existed) old = std::getenv(name); }
    static void set(const char* name, const std::string& value)
    {
#ifdef _WIN32
        check(::_putenv_s(name, value.c_str()) == 0, "set Windows test environment");
#else
        check(::setenv(name, value.c_str(), 1) == 0, "set POSIX test environment");
#endif
    }
    static void unset(const char* name)
    {
#ifdef _WIN32
        check(::_putenv_s(name, "") == 0, "unset Windows test environment");
#else
        check(::unsetenv(name) == 0, "unset POSIX test environment");
#endif
    }
    ~Environment() { if (existed) set(key.c_str(), old); else unset(key.c_str()); }
};
void configuration()
{
    Fixture f;
    Environment xdg("XDG_CONFIG_HOME"), home("HOME");
    const auto xdgRoot = f.root / pathFromUtf8("設定-xdg");
    const auto homeRoot = f.root / pathFromUtf8("家-home");
#ifdef _WIN32
    const auto native = SkinPreference::configPath();
    check(native.is_absolute() && native.filename() == "defaults.ini" &&
          native.parent_path().filename() == "SMS-AnvilleSampler", "Windows native config path is absolute with plugin suffix");
#endif
    Environment::set("XDG_CONFIG_HOME", pathToUtf8(xdgRoot));
    Environment::set("HOME", pathToUtf8(homeRoot));
#if defined(_WIN32)
    check(SkinPreference::configPath() == native, "Windows native config ignores XDG and HOME overrides");
#elif defined(__APPLE__)
    check(SkinPreference::configPath() == homeRoot / "Library" / "Application Support" / "SMS-AnvilleSampler" / "defaults.ini",
          "macOS config uses Unicode HOME Application Support and ignores XDG");
#else
    check(SkinPreference::configPath() == xdgRoot / "SMS-AnvilleSampler" / "defaults.ini", "absolute Unicode XDG preferred");
#endif
    Environment::set("XDG_CONFIG_HOME", "relative");
#if defined(_WIN32)
    check(SkinPreference::configPath() == native, "relative XDG cannot alter Windows native config");
#elif defined(__APPLE__)
    check(SkinPreference::configPath() == homeRoot / "Library" / "Application Support" / "SMS-AnvilleSampler" / "defaults.ini",
          "relative XDG cannot alter macOS config");
#else
    check(SkinPreference::configPath() == homeRoot / ".config" / "SMS-AnvilleSampler" / "defaults.ini", "relative XDG falls back Unicode HOME");
#endif
    Environment::unset("XDG_CONFIG_HOME"); Environment::unset("HOME");
#ifdef _WIN32
    check(SkinPreference::configPath() == native, "missing POSIX user directories preserves Windows native persistence");
    Environment::set("HOME", "relative");
    check(SkinPreference::configPath() == native, "relative HOME cannot alter Windows native config");
#else
    check(SkinPreference::configPath().empty(), "missing user directories disables persistence");
    Environment::set("HOME", "relative");
    check(SkinPreference::configPath().empty(), "relative HOME cannot persist to cwd");
#endif
}
}
int main()
{
    names(); catalog(); unicodeCatalog(); confinementAndBounds(); preferences(); configuration();
    std::cout << "Skin catalog tests passed\n";
}
