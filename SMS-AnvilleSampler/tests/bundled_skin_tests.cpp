#include "SkinCatalog.hpp"

#include <cstdlib>
#include <iostream>
#include <iterator>

namespace {
namespace fs = std::filesystem;

void check(bool value, const char* message)
{
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::string contents(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    check(static_cast<bool>(input), "packaged asset is readable");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void packagedSkin(const fs::path& source, const fs::path& packaged, const sms::ui::Skin& skin)
{
    check(skin.valid(), "packaged manifest loads");
    check(!skin.imagePath("background").empty() && skin.imagePath("logo").empty(),
          "bundled background loads while original embedded logo is retained");
    const auto backgroundName = fs::path(skin.imagePath("background")).filename();
    check(fs::equivalent(skin.imagePath("background"), packaged / backgroundName),
          "background resolves inside its packaged skin");
    check(contents(source / "skin.ini") == contents(packaged / "skin.ini"),
          "packaged manifest matches maintained source");
    const auto background = contents(packaged / backgroundName);
    check(background == contents(source / backgroundName),
          "packaged artwork matches maintained source");
    const bool png = background.size() >= 24 &&
                     background.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0;
    const bool jpeg = background.size() >= 3 &&
                      background.compare(0, 3, "\xff\xd8\xff", 3) == 0;
    check(png || jpeg, "background is a supported PNG or JPEG asset");
    for (const auto& [key, value] : skin.colors) {
        std::array<std::uint8_t, 4> rgba{};
        check(sms::ui::Skin::parseColor(value, rgba), "authored skin colors are valid RGBA");
    }
    for (const auto key : {"backgroundAlpha", "padAlpha", "padPanelAlpha", "logoGlowAlpha"}) {
        float alpha = 0.0f;
        check(skin.metrics.contains(key) &&
              sms::ui::Skin::parseMetric(skin.metrics.at(key), alpha) &&
              alpha >= 0.0f && alpha <= 1.0f, "authored alpha settings are within bounds");
    }
    for (const auto& [key, maximum] : {std::pair{"logoGlowRadius", 24.0f},
                                     std::pair{"logoGlowPulseSeconds", 60.0f}}) {
        float value = 0.0f;
        check(skin.metrics.contains(key) &&
              sms::ui::Skin::parseMetric(skin.metrics.at(key), value) &&
              value >= 0.0f && value <= maximum, "authored logo metrics are within bounds");
    }
}

void packagedCollection(const fs::path& source, const fs::path& forced)
{
    check(!fs::exists(forced), "build output contains no forced skin");
    const auto catalog = sms::ui::SkinCatalog::discover(forced);
    check(!catalog.forced && catalog.selectable(), "bundled collection exposes the skin selector");
    check(catalog.entries.size() == 3, "Default and both bundled skins are available");
    check(catalog.entries[0].id.empty() && catalog.entries[0].name == "Default",
          "built-in appearance is the first choice");
    check(catalog.selectedIndex("") == 0 && catalog.selectedIndex("missing-skin") == 0 &&
          !catalog.load(0).valid(), "no preference or unavailable skin uses the built-in appearance");
    const std::array<std::pair<const char*, const char*>, 2> expected{{
        {"AnvilleGraffiti", "Anville Graffiti"}, {"FinnishDream", "Finnish Dream"}}};
    for (std::size_t i = 0; i < expected.size(); ++i) {
        const auto& [id, name] = expected[i];
        const auto index = i + 1;
        check(catalog.entries[index].id == id && catalog.entries[index].name == name,
              "bundled choices have stable IDs and alphabetical display names");
        check(catalog.selectedIndex(id) == index, "remembered bundled choices resolve");
        packagedSkin(source / id, sms::ui::SkinCatalog::collectionDirectory(forced) / id,
                     catalog.load(index));
    }
}
} // namespace

int main(int argc, char** argv)
{
    check(argc >= 3, "source skins and at least one packaged collection supplied");
    for (int i = 2; i < argc; ++i) packagedCollection(argv[1], argv[i]);
    std::cout << "Packaged skin collection checks passed\n";
}
