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

void packagedSkin(const fs::path& source, const fs::path& packaged)
{
    const auto catalog = sms::ui::SkinCatalog::discover(packaged);
    check(catalog.forced && !catalog.selectable(), "bundled default hides the skin selector");
    check(catalog.entries.size() == 1 && catalog.selectedIndex("old-custom-skin") == 0,
          "forced default suppresses collection and remembered choices");
    const auto skin = catalog.load(0);
    check(skin.valid() && skin.name == "Anville Graffiti", "packaged manifest loads");
    check(catalog.load(999).name == skin.name, "forced manifest ignores selection index");
    check(!skin.imagePath("background").empty() && skin.imagePath("logo").empty(),
          "graffiti background loads while original embedded logo is retained");
    check(fs::equivalent(skin.imagePath("background"), packaged / "background.png"),
          "background resolves inside its packaged skin");
    check(contents(source / "skin.ini") == contents(packaged / "skin.ini"),
          "packaged manifest matches maintained source");
    const auto background = contents(packaged / "background.png");
    check(background == contents(source / "background.png"),
          "packaged artwork matches maintained source");
    check(background.size() >= 24 && background.compare(0, 8, "\x89PNG\r\n\x1a\n", 8) == 0,
          "background is a PNG asset");
    for (const auto& [key, value] : skin.colors) {
        std::array<std::uint8_t, 4> rgba{};
        check(sms::ui::Skin::parseColor(value, rgba), "authored default colors are valid RGBA");
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
} // namespace

int main(int argc, char** argv)
{
    check(argc >= 3, "source skin and at least one packaged skin supplied");
    for (int i = 2; i < argc; ++i) packagedSkin(argv[1], argv[i]);
    std::cout << "Packaged default skin checks passed\n";
}
