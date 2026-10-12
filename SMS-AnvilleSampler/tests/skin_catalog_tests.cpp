#include "SkinCatalog.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
namespace fs = std::filesystem;
using sms::ui::Skin;
using sms::ui::SkinCatalog;
using sms::ui::SkinPreference;
void check(bool value, const char* message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; std::exit(EXIT_FAILURE); }
}
struct Fixture {
    fs::path root = fs::temp_directory_path() /
        ("sms-catalog-" + std::to_string(::getpid()) + "-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() { fs::create_directories(root); }
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
        file(root / "skins" / id / "skin.ini", "[skin]\nversion=1\nname=" + std::string(name) + "\n");
    }
};
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
void confinementAndBounds()
{
    Fixture f;
    f.skin("inside", "Inside");
    Fixture::file(f.root / "outside" / "skin.ini", "[skin]\nversion=1\nname=Outside\n");
    fs::create_directory_symlink(f.root / "outside", f.root / "skins" / "escape");
    fs::create_directories(f.root / "skins" / "manifest-escape");
    fs::create_symlink(f.root / "outside" / "skin.ini", f.root / "skins" / "manifest-escape" / "skin.ini");
    auto c = SkinCatalog::discover(f.root / "skin");
    check(c.entries.size() == 2, "directory and manifest symlink escapes excluded");
    fs::rename(f.root / "skins", f.root / "actual");
    fs::create_directory_symlink(f.root / "actual", f.root / "skins");
    check(SkinCatalog::discover(f.root / "skin").entries.size() == 2, "symlink collection root resolves safely");
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
    const auto path = f.root / "config" / "selected-skin";
    check(SkinPreference::read(path).empty(), "missing preference defaults");
    check(SkinPreference::write(path, "Ääni folder") && SkinPreference::read(path) == "Ääni folder", "folder ID roundtrip");
    for (const auto* id : {"../escape", "/absolute", ".", "..", "nested/name", "back\\slash", "bad\nline"}) {
        check(!SkinPreference::write(path, id) && SkinPreference::read(path) == "Ääni folder", "unsafe preference cannot replace previous file");
    }
    check(!SkinPreference::write(path, std::string(513, 'x')), "oversize ID rejected");
    check(SkinPreference::write(path, "") && SkinPreference::read(path).empty(), "Default persists empty ID");
    for (const auto* malformed : {"folder\n", "SMS-SKIN-PREFERENCE 2\nfolder\n", "SMS-SKIN-PREFERENCE 1\n../bad\n", "SMS-SKIN-PREFERENCE 1\nfolder", "SMS-SKIN-PREFERENCE 1\na\nb\n"}) {
        Fixture::file(path, malformed);
        check(SkinPreference::read(path).empty(), "malformed preference defaults");
    }
    Fixture::file(path, std::string(1025, 'x'));
    check(SkinPreference::read(path).empty(), "oversize preference defaults");
    Fixture::file(f.root / "blocked", "file");
    check(!SkinPreference::write(f.root / "blocked" / "choice", "skin"), "parent directory error handled");
    fs::create_directory(f.root / "target-directory");
    check(!SkinPreference::write(f.root / "target-directory", "skin") &&
          fs::is_directory(f.root / "target-directory"), "failed atomic rename preserves destination");
    check(!SkinPreference::write({}, "skin"), "empty preference path rejected");
    check(SkinPreference::write(path, "a"), "initial concurrent preference");
    auto writer = [&](const char* id) { for (int i = 0; i < 100; ++i) check(SkinPreference::write(path, id), "concurrent write succeeds"); };
    std::thread first(writer, "a"), second(writer, "b");
    for (int i = 0; i < 200; ++i) {
        const auto id = SkinPreference::read(path);
        check(id == "a" || id == "b", "reader observes only complete concurrent writes");
    }
    first.join(); second.join();
    for (const auto& entry : fs::directory_iterator(path.parent_path()))
        check(entry.path() == path, "atomic writes leave no temporary files");
}
struct Environment {
    std::string key, old; bool existed;
    explicit Environment(const char* name): key(name), existed(std::getenv(name) != nullptr)
    { if (existed) old = std::getenv(name); }
    ~Environment() { if (existed) ::setenv(key.c_str(), old.c_str(), 1); else ::unsetenv(key.c_str()); }
};
void configuration()
{
    Environment xdg("XDG_CONFIG_HOME"), home("HOME");
    ::setenv("XDG_CONFIG_HOME", "/tmp/sms-xdg", 1); ::setenv("HOME", "/tmp/sms-home", 1);
    check(SkinPreference::configPath() == "/tmp/sms-xdg/SMS-AnvilleSampler/selected-skin", "absolute XDG preferred");
    ::setenv("XDG_CONFIG_HOME", "relative", 1);
    check(SkinPreference::configPath() == "/tmp/sms-home/.config/SMS-AnvilleSampler/selected-skin", "relative XDG falls back HOME");
    ::unsetenv("XDG_CONFIG_HOME"); ::unsetenv("HOME");
    check(SkinPreference::configPath().empty(), "missing user directories disables persistence");
    ::setenv("HOME", "relative", 1);
    check(SkinPreference::configPath().empty(), "relative HOME cannot persist to cwd");
}
}
int main()
{
    names(); catalog(); confinementAndBounds(); preferences(); configuration();
    std::cout << "Skin catalog tests passed\n";
}
