#include "Skin.hpp"
#include "SkinFileIO.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
namespace fs = std::filesystem;
using sms::ui::skinFileIO::pathFromUtf8;
using sms::ui::skinFileIO::pathToUtf8;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

struct Fixture {
    fs::path root;
    Fixture()
    {
        static std::atomic<unsigned> sequence{0};
        for (unsigned attempt = 0; attempt < 100; ++attempt) {
            root = fs::temp_directory_path() / pathFromUtf8(
                "sms-skin-tests-雪-🔨-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                "-" + std::to_string(sequence.fetch_add(1)));
            std::error_code error;
            if (fs::create_directory(root, error)) {
                fs::create_directories(directory() / "images");
                return;
            }
            check(!error, "create portable temporary directory");
        }
        check(false, "unique portable temporary directory");
    }
    ~Fixture() { std::error_code error; fs::remove_all(root, error); }
    fs::path directory() const { return root / "skin"; }
    void manifest(const std::string& contents) const
    {
        std::ofstream output(directory() / "skin.ini", std::ios::binary);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        check(static_cast<bool>(output), "write test manifest");
    }
};

bool symlink(const fs::path& target, const fs::path& link)
{
    std::error_code error;
    fs::create_symlink(target, link, error);
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

void fullFallback(const Fixture& fixture, const std::string& manifest)
{
    fixture.manifest(manifest);
    const auto skin = sms::ui::Skin::load(fixture.directory());
    check(!skin.valid() && skin.colors.empty() && skin.metrics.empty() && skin.strings.empty() &&
          skin.images.empty(), "invalid manifest completely discards prior entries");
}

void scalarHelpers()
{
    std::array<std::uint8_t, 4> rgba{1, 2, 3, 4};
    check(sms::ui::Skin::parseColor("#01aBff", rgba) && rgba ==
          std::array<std::uint8_t, 4>{1, 171, 255, 255}, "six digit color gets opaque alpha");
    check(sms::ui::Skin::parseColor("#AABBCC00", rgba) && rgba ==
          std::array<std::uint8_t, 4>{170, 187, 204, 0}, "eight digit color keeps alpha");
    for (const auto* invalid : {"", "#abc", "#aabbccd", "aabbcc", "#GG0011", "#123456  "}) {
        const auto before = rgba;
        check(!sms::ui::Skin::parseColor(invalid, rgba) && before == rgba,
              "invalid color leaves destination unchanged");
    }
    float metric = 1;
    check(sms::ui::Skin::parseMetric("+1.25e2", metric) && metric == 125,
          "metric accepts decimal exponent");
    check(sms::ui::Skin::parseMetric("-0.5", metric) && metric == -0.5f,
          "metric accepts negative decimal");
    for (const auto* invalid : {"", "nan", "inf", "-infinity", "1e99", "1px", "0x10", " 1", "+-1"}) {
        const auto before = metric;
        check(!sms::ui::Skin::parseMetric(invalid, metric) && metric == before,
              "invalid metric leaves destination unchanged");
    }
}

void fallbackAndBounds(const Fixture& fixture)
{
    check(!sms::ui::Skin::load(fixture.directory()).valid(), "missing manifest uses defaults");
    fixture.manifest("[skin]\nversion=1\n[colors]\ncanvas=#123456\n");
    const auto previousDirectory = fs::current_path();
    fs::current_path(fixture.directory());
    const auto noDirectory = sms::ui::Skin::load({});
    fs::current_path(previousDirectory);
    check(!noDirectory.valid() && noDirectory.colors.empty(),
          "empty skin directory uses defaults even when current directory contains a manifest");
    for (const auto* invalid : {
        "[colors]\na=#123456\n", "[colors]\na=#123456\n[skin]\nversion=2\n",
        "[skin]\nversion=1\n[colors]\na=#123456\nbroken\n",
        "[skin]\nversion=1\n[colors\na=#123456\n",
        "[skin]\nversion=1\nversion=1\n", "outside=section\n[skin]\nversion=1\n",
        "[skin]\nversion=1\n[colors]\n=#123456\n"})
        fullFallback(fixture, invalid);
    std::string bounded = "[skin]\nversion=1\n#";
    bounded.resize(sms::ui::Skin::maxManifestBytes, 'x');
    fixture.manifest(bounded);
    check(sms::ui::Skin::load(fixture.directory()).valid(), "exact 64 KiB manifest accepted");
    bounded += 'x';
    fullFallback(fixture, bounded);
}

void partialEntriesAndSnapshot(const Fixture& fixture)
{
    const std::string longText(513, 'x');
    fixture.manifest(" ; comment\r\n[skin]\r\n version = 1 \r\nignored=yes\n"
                     "[future]\nfree form ignored\nsetting=whatever\n"
                     "[colors]\n background = #12345678 \ninvalid=#xyzxyz\n"
                     "[metrics]\n spacing = 2.5 \ninvalid=NaN\n"
                     "[strings]\n PLAY = SOITA \nCaption=Value=with=equals\n"
                     "Unicode=Hyv\xc3\xa4 \xf0\x9f\x94\xa8\nEmpty=\nTooLong=" + longText +
                     "\nInvalidUtf8=\xc0\xaf\nControl=x\x01y\nC1=\xc2\x85\n");
    const auto skin = sms::ui::Skin::load(fixture.directory());
    check(skin.valid() && skin.colors.size() == 1 && skin.metrics.size() == 1 &&
          skin.strings.size() == 4, "bad entries ignored independently and unknown sections tolerated");
    check(skin.colors.at("background") == "#12345678" && skin.metrics.at("spacing") == "2.5",
          "key and value whitespace trimmed");
    const char* replacement = skin.text("PLAY");
    check(std::string(replacement) == "SOITA" && skin.text("PLAY") == replacement,
          "replacement pointer stable");
    const char* literal = "missing";
    check(skin.text(literal) == literal && std::string(skin.text("Empty")).empty(),
          "unknown literal preserves original pointer and empty replacement works");
    check(std::string(skin.text("Caption")) == "Value=with=equals", "value preserves equals signs");
    fixture.manifest("[skin]\nversion=1\n[strings]\nPLAY=CHANGED\n");
    check(std::string(skin.text("PLAY")) == "SOITA", "startup snapshot does not reload files");
    check(std::string(sms::ui::Skin::load(fixture.directory()).text("PLAY")) == "CHANGED",
          "next startup sees modified file");
}

void imageConfinement(const Fixture& fixture)
{
    const auto image = fixture.directory() / "images" / "background.png";
    { std::ofstream output(image); output << "fixture"; }
    const auto external = fixture.root / "external.png";
    { std::ofstream output(external); output << "fixture"; }
    const bool escapeLink = symlink(external, fixture.directory() / "escape.png");
    const bool insideLink = symlink(image, fixture.directory() / "inside.png");
    fixture.manifest("[skin]\nversion=1\n[images]\nbackground=images/background.png\n"
                     "logo=inside.png\nunknown=images/background.png\n");
    const auto skin = sms::ui::Skin::load(fixture.directory());
    check(skin.valid() && skin.images.size() == (insideLink ? 2u : 1u) && skin.imagePath("background") ==
          pathToUtf8(fs::canonical(image)), "relative images resolve at startup");
    if (insideLink)
        check(skin.imagePath("logo") == pathToUtf8(fs::canonical(image)), "confined image symlink resolves at startup");
    check(skin.imagePath("missing").empty(), "missing image falls back");
    for (const auto& invalid : {std::string("../external.png"), pathToUtf8(external),
                              std::string("images/../background.png"),
                              std::string("missing.png"), std::string("images"), std::string()}) {
        fixture.manifest("[skin]\nversion=1\n[images]\nbackground=" + invalid +
                         "\nlogo=images/background.png\n");
        const auto partial = sms::ui::Skin::load(fixture.directory());
        check(partial.valid() && partial.imagePath("background").empty() &&
              !partial.imagePath("logo").empty(), "unsafe or missing image ignored independently");
    }
    if (escapeLink) {
        fixture.manifest("[skin]\nversion=1\n[images]\nbackground=escape.png\n");
        check(sms::ui::Skin::load(fixture.directory()).imagePath("background").empty(),
              "escaping image symlink rejected");
    }
}

void unicodeImages(const Fixture& fixture)
{
    const std::string relative = "画像/背景-雪-🔨.png";
    const auto image = fixture.directory() / pathFromUtf8(relative);
    fs::create_directories(image.parent_path());
    const std::vector<std::uint8_t> contents{0, 1, 127, 128, 255};
    {
        std::ofstream output(image, std::ios::binary);
        output.write(reinterpret_cast<const char*>(contents.data()),
                     static_cast<std::streamsize>(contents.size()));
        check(static_cast<bool>(output), "write Unicode image fixture");
    }
    fixture.manifest("[skin]\nversion=1\n[images]\nbackground=" + relative + "\n");
    const auto skin = sms::ui::Skin::load(fixture.directory());
    const auto canonical = fs::canonical(image);
    check(skin.valid() && skin.imagePath("background") == pathToUtf8(canonical),
          "Unicode image filename resolves to UTF8 path");
    const auto roundtrip = pathFromUtf8(skin.imagePath("background"));
    check(roundtrip == canonical && pathToUtf8(roundtrip) == skin.imagePath("background"),
          "Unicode native path and UTF8 image path roundtrip");
    check(sms::ui::skinFileIO::readImage(roundtrip) == contents,
          "Unicode image path reads exact binary bytes");
    check(sms::ui::skinFileIO::readBytes(roundtrip, contents.size()) == contents,
          "bounded Unicode byte read accepts exact file size");
    check(sms::ui::skinFileIO::readBytes(roundtrip, contents.size() - 1).empty(),
          "bounded Unicode byte read rejects file beyond limit");
    check(sms::ui::skinFileIO::readImage(image.parent_path() / pathFromUtf8("不明.png")).empty(),
          "missing Unicode image read falls back");
    const auto empty = image.parent_path() / pathFromUtf8("空.png");
    {
        std::ofstream output(empty, std::ios::binary);
        check(static_cast<bool>(output), "write empty Unicode image fixture");
    }
    check(sms::ui::skinFileIO::readImage(empty).empty(), "empty Unicode image read falls back");
    check(sms::ui::skinFileIO::readImage(image.parent_path()).empty(), "directory image read falls back");
    fs::resize_file(empty, sms::ui::skinFileIO::maxImageBytes + 1);
    check(sms::ui::skinFileIO::readImage(empty).empty(), "image exceeding 64 MiB limit falls back");
}

void translatedFormats(const Fixture& fixture)
{
    fixture.manifest("[skin]\nversion=1\n[strings]\n"
                     "Bank %c | Pad %u | MIDI %02d=Bank %c | Feld %u | MIDI %02d\n"
                     "Slices: %d | Duration: %.1f s=Schnitte: %d | Dauer: %.1f s\n"
                     "Gain %+.1f dB | Pan %+.2f | Tune %+.0f=Pegel %+.1f dB | Panorama %+.2f | Stimmung %+.0f\n"
                     "File: %s | Length: %.2f s=Datei: %s | Länge: %.2f s\n"
                     "Progress %u%%=Fortschritt %u%%\nPlain=Einfach\n");
    const auto skin = sms::ui::Skin::load(fixture.directory());
    check(skin.valid(), "formatted German manifest loads");
    char buffer[256]{};
    std::snprintf(buffer, sizeof(buffer), skin.format("Bank %c | Pad %u | MIDI %02d"), 'B', 3u, 7);
    check(std::string(buffer) == "Bank B | Feld 3 | MIDI 07", "translated heading preserves types and width");
    std::snprintf(buffer, sizeof(buffer), skin.format("Slices: %d | Duration: %.1f s"), 6, 2.25);
    check(std::string(buffer) == "Schnitte: 6 | Dauer: 2.2 s", "translated heading preserves decimal precision");
    std::snprintf(buffer, sizeof(buffer),
                  skin.format("Gain %+.1f dB | Pan %+.2f | Tune %+.0f"), 1.5, -0.25, 2.0);
    check(std::string(buffer) == "Pegel +1.5 dB | Panorama -0.25 | Stimmung +2",
          "translated readouts preserve sign and precision");
    std::snprintf(buffer, sizeof(buffer), skin.format("File: %s | Length: %.2f s"), "Grüße.wav", 1.25);
    check(std::string(buffer) == "Datei: Grüße.wav | Länge: 1.25 s", "UTF-8 literals and string arguments preserved");
    std::snprintf(buffer, sizeof(buffer), skin.format("Progress %u%%"), 75u);
    check(std::string(buffer) == "Fortschritt 75%", "literal percent remains escaped");
    check(std::string(skin.format("Plain")) == "Einfach" && std::string(skin.format(nullptr)).empty(),
          "plain patterns and null input supported");
    const char* missing = "Missing %d";
    check(skin.format(missing) == missing, "missing format preserves original pointer");
    check(skin.format("Progress %u%%") == skin.format("Progress %u%%"), "format replacement pointer stable");

    sms::ui::Skin unsafe;
    const char* pattern = "Pad %u | Time %.2f | Name %s | MIDI %02d %%";
    for (const auto* replacement : {
        "Feld %s | Zeit %.2f | Name %s | MIDI %02d %%", // changed argument type
        "Feld %u | Zeit %.2f | Name %s | MIDI %02d %% %d", // extra argument
        "Feld %u | Zeit %.2f | Name %s %%", // removed argument
        "Feld %.2f | Zeit %u | Name %s | MIDI %02d %%", // reordered types
        "Feld %u | Zeit %.1f | Name %s | MIDI %02d %%", // precision
        "Feld %u | Zeit %.2f | Name %s | MIDI %d %%", // width
        "Feld %lu | Zeit %.2f | Name %s | MIDI %02d %%", // length
        "Feld %1$u | Zeit %.2f | Name %s | MIDI %02d %%", // positional
        "Feld %*u | Zeit %.2f | Name %s | MIDI %02d %%", // dynamic width
        "Feld %u | Zeit %.*f | Name %s | MIDI %02d %%", // dynamic precision
        "Feld %u | Zeit %.2f | Name %s | MIDI %02d %% %n", // writes memory
        "Feld %u | Zeit %.2f | Name %s | MIDI %02d", // removed literal percent
        "Feld %u | Zeit %.2f | Name %s | MIDI %02d %% %%", // extra literal percent
        "Feld %u | Zeit %.2f | Name %s | MIDI %02d %% %", // trailing percent
        "Feld %u | Zeit %.2f | Name %s | MIDI %02d %% %q", // unknown token
        "Feld %u | Zeit %.2f | Name %s | MIDI %02d %", // malformed literal percent
        "%% Feld %u | Zeit %.2f | Name %s | MIDI %02d"}) { // reordered literal percent
        unsafe.strings[pattern] = replacement;
        check(unsafe.format(pattern) == pattern, "unsafe translated pattern falls back to original pointer");
    }
    for (const auto* invalid : {"%n", "%*d", "%.*f", "%1$d", "%", "%q", "%Ls", "%hf", "%llp"}) {
        unsafe.strings[invalid] = invalid;
        check(unsafe.format(invalid) == invalid, "unsupported original pattern never translated");
    }
    for (const auto* valid : {"%hhd", "%hd", "%ld", "%lld", "%jd", "%zu", "%td",
                             "%lf", "%Lf", "%lc", "%ls", "%p", "%#08x", "%-.3s", "%%"}) {
        unsafe.strings[valid] = std::string("Wert ") + valid;
        check(std::string(unsafe.format(valid)) == std::string("Wert ") + valid,
              "fixed standard conversion syntax preserved exactly");
    }
    unsafe.strings["%+.1f"] = "Pegel %.1f";
    check(std::string(unsafe.format("%+.1f")) == "%+.1f", "sign flag cannot be removed");
    unsafe.strings["Plain"] = "Einfach %d";
    check(std::string(unsafe.format("Plain")) == "Plain", "plain pattern cannot gain a conversion");
}

void pluginDirectories(const Fixture& fixture)
{
    const auto binary = fixture.root / "Renamed-plugin.clap";
    { std::ofstream output(binary); output << "fixture"; }
    const auto singleFileSkin = fixture.root / "SMS-AnvilleSampler.skin";
    const auto binaryName = pathToUtf8(binary);
    for (const auto* name : {"SMS-AnvilleSampler.lv2", "SMS-AnvilleSampler.vst3"}) {
        const auto bundle = fixture.root / name;
        fs::create_directory(bundle);
        const auto bundleName = pathToUtf8(bundle);
        check(sms::ui::Skin::directoryForPlugin(bundleName.c_str(), binaryName.c_str(),
                                               "SMS-AnvilleSampler.skin") == bundle / "skin",
              "LV2 and VST3 directories use bundle root skin");
    }
    check(sms::ui::Skin::directoryForPlugin(binaryName.c_str(), nullptr,
                                           "SMS-AnvilleSampler.skin") == singleFileSkin,
          "renamed single-file CLAP bundle uses fixed sibling skin directory");
    const auto unicodeBundle = fixture.root / pathFromUtf8("楽器-🔨.vst3");
    fs::create_directory(unicodeBundle);
    const auto unicodeBundleName = pathToUtf8(unicodeBundle);
    check(sms::ui::Skin::directoryForPlugin(unicodeBundleName.c_str(), binaryName.c_str(),
                                           "SMS-AnvilleSampler.skin") == unicodeBundle / "skin",
          "Unicode bundle filename uses UTF8 metadata");
    const char* unicodeSkinName = "外観-雪.skin";
    check(sms::ui::Skin::directoryForPlugin(binaryName.c_str(), nullptr, unicodeSkinName) ==
          fixture.root / pathFromUtf8(unicodeSkinName), "Unicode single-file skin name uses UTF8 metadata");
    for (const auto* absentBundle : {static_cast<const char*>(nullptr), ""})
        check(sms::ui::Skin::directoryForPlugin(absentBundle, binaryName.c_str(),
                                               "SMS-AnvilleSampler.skin") == singleFileSkin,
              "missing bundle metadata uses binary sibling skin directory");
    const auto missing = pathToUtf8(fixture.root / "missing.vst3");
    check(sms::ui::Skin::directoryForPlugin(missing.c_str(), binaryName.c_str(),
                                           "SMS-AnvilleSampler.skin").empty(),
          "nonexistent supplied bundle does not fall back to unrelated binary path");
    const auto dangling = fixture.root / "dangling.vst3";
    if (symlink(fixture.root / "missing-target", dangling)) {
        const auto danglingName = pathToUtf8(dangling);
        check(sms::ui::Skin::directoryForPlugin(danglingName.c_str(), binaryName.c_str(),
                                               "SMS-AnvilleSampler.skin").empty(),
              "failed bundle status does not fall back to unrelated binary path");
    }
    for (const auto* absentBinary : {static_cast<const char*>(nullptr), ""})
        check(sms::ui::Skin::directoryForPlugin(nullptr, absentBinary,
                                               "SMS-AnvilleSampler.skin").empty(),
              "missing bundle and binary metadata yield defaults");
    for (const auto* absentSkinName : {static_cast<const char*>(nullptr), ""})
        check(sms::ui::Skin::directoryForPlugin(binaryName.c_str(), nullptr, absentSkinName).empty(),
              "missing single-file skin name yields defaults");
}

void nativePluginDirectories(const Fixture& fixture)
{
    const auto writeBinary = [](const fs::path& binary) {
        fs::create_directories(binary.parent_path());
        std::ofstream output(binary, std::ios::binary);
        output << "fixture";
        check(static_cast<bool>(output), "write native plugin binary fixture");
    };
    const auto skinName = pathFromUtf8("外観-雪.skin");
    const auto lv2 = fixture.root / pathFromUtf8("楽器-雪.lv2") / pathFromUtf8("音源.so");
    writeBinary(lv2);
    check(sms::ui::Skin::directoryForNativeBinary(lv2, skinName) == lv2.parent_path() / "skin",
          "native Unicode LV2 binary resolves bundle skin");
    const auto vst3Bundle = fixture.root / pathFromUtf8("楽器-🔨.vst3");
    const auto vst3 = vst3Bundle / "Contents" / "x86_64-win" / pathFromUtf8("音源.vst3");
    writeBinary(vst3);
    check(sms::ui::Skin::directoryForNativeBinary(vst3, skinName) == vst3Bundle / "skin",
          "native Unicode VST3 binary resolves outer bundle skin");
    const auto upperBundle = fixture.root / "Instrument.VST3";
    const auto upperBinary = upperBundle / "CONTENTS" / "x86_64-win" / "Instrument.vst3";
    writeBinary(upperBinary);
    check(sms::ui::Skin::directoryForNativeBinary(upperBinary, skinName) == upperBundle / "skin",
          "native bundle layout accepts Windows case variations");
    const auto upperLv2 = fixture.root / "Instrument.LV2" / "Instrument.dll";
    writeBinary(upperLv2);
    check(sms::ui::Skin::directoryForNativeBinary(upperLv2, skinName) == upperLv2.parent_path() / "skin",
          "native LV2 extension accepts Windows case variations");
    const auto clap = fixture.root / pathFromUtf8("楽器-雪.clap");
    writeBinary(clap);
    check(sms::ui::Skin::directoryForNativeBinary(clap, skinName) == fixture.root / skinName,
          "native Unicode single-file CLAP resolves sibling skin");
    check(sms::ui::Skin::directoryForNativeBinary(fixture.root / "missing.clap", skinName).empty(),
          "missing native plugin binary yields defaults");
#ifdef _WIN32
    const auto module = sms::ui::skinFileIO::moduleBinaryPath();
    check(module.is_absolute() && fs::is_regular_file(module),
          "Windows wide module path is absolute and names existing binary");
#endif
}
} // namespace

int main()
{
    scalarHelpers();
    Fixture fixture;
    fallbackAndBounds(fixture);
    partialEntriesAndSnapshot(fixture);
    translatedFormats(fixture);
    imageConfinement(fixture);
    unicodeImages(fixture);
    pluginDirectories(fixture);
    nativePluginDirectories(fixture);
    std::cout << "Skin tests passed\n";
}
