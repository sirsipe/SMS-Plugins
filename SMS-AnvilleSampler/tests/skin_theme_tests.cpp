#include "DPF/SkinTheme.hpp"
#include "DPF/LogoGlow.hpp"
#include "DPF/WaveformRenderer.hpp"
#include "../../third_party/DPF/dgl/src/nanovg/nanovg.h"

#include <atomic>
#include <barrier>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#ifndef ANVILLESAMPLER_SKIN_TEMPLATE_DIR
#error "Define ANVILLESAMPLER_SKIN_TEMPLATE_DIR to the placeholder skin directory"
#endif

namespace {
using namespace sms::ui::dpf;

void check(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool sameColor(const DGL_NAMESPACE::Color& first, const DGL_NAMESPACE::Color& second)
{
    for (unsigned channel = 0; channel < 4; ++channel)
        if (std::abs(first.rgba[channel] - second.rgba[channel]) > 0.000001f) return false;
    return true;
}

std::string testColor(unsigned index)
{
    char result[10]{};
    std::snprintf(result, sizeof(result), "#%02X%02X%02X%02X",
                  index + 1, index + 31, index + 61, index + 91);
    return result;
}

DGL_NAMESPACE::Color expectedColor(unsigned index)
{
    return DGL_NAMESPACE::Color(static_cast<int>(index + 1), static_cast<int>(index + 31),
                               static_cast<int>(index + 61), static_cast<float>(index + 91) / 255.0f);
}

void envelopeHandleColors()
{
    struct Fill {
        std::array<float, 4> bounds;
        DGL_NAMESPACE::Color color;
    };
    std::vector<Fill> fills;
    // Record real NanoVG draw submissions without requiring OpenGL or a display.
    NVGparams backend{};
    backend.userPtr = &fills;
    backend.renderCreate = [](void*, void*) { return 1; };
    backend.renderCreateTexture = [](void*, int, int, int, int, const unsigned char*) { return 1; };
    backend.renderDeleteTexture = [](void*, int) { return 1; };
    backend.renderViewport = [](void*, float, float, float) {};
    backend.renderFlush = [](void*) {};
    backend.renderFill = [](void* data, NVGpaint* paint, NVGcompositeOperationState,
                            NVGscissor*, float, const float* bounds, const NVGpath*, int) {
        static_cast<std::vector<Fill>*>(data)->push_back({
            {bounds[0], bounds[1], bounds[2], bounds[3]},
            {paint->innerColor.r, paint->innerColor.g, paint->innerColor.b, paint->innerColor.a}});
    };
    backend.renderStroke = [](void*, NVGpaint*, NVGcompositeOperationState,
                              NVGscissor*, float, float, const NVGpath*, int) {};
    auto* context = nvgCreateInternal(&backend, nullptr);
    check(context != nullptr, "recording renderer creates a NanoVG context");
    {
        DGL_NAMESPACE::NanoVG canvas(context);
        const sms::ui::Rect bounds{0.0f, 0.0f, 370.0f, 104.0f};
        sms::audio::WaveformSummary summary;
        summary.frames = 48000;
        sms::dsp::SamplePlaybackSettings settings;
        settings.attackSeconds = 0.2f;
        settings.decaySeconds = 0.2f;
        settings.sustainLevel = 0.4f;
        settings.releaseSeconds = 0.2f;
        const auto graph = sms::ui::waveform::envelopeGeometry(bounds, summary, settings);
        const std::array<sms::ui::Point, 3> handles{{
            {graph.attackHandleX, graph.top}, {graph.decayHandleX, graph.sustainY},
            {graph.releaseHandleX, graph.sustainY}}};
        using sms::ui::waveform::EditTarget;
        for (const char* playback : {"#24A8D380", "#73CE41FF"}) {
            sms::ui::Skin skin;
            skin.colors["activityPlayback"] = playback;
            skin.colors["controlAccent"] = "#FF8800";
            const auto colors = skinTheme(skin);
            ThemeScope scope(colors, skin);
            for (const auto hovered : {EditTarget::none, EditTarget::attackNode,
                                       EditTarget::decayNode, EditTarget::releaseNode}) {
                fills.clear();
                canvas.beginFrame(400, 120);
                drawEnvelopeGraph(canvas, bounds, summary, settings, hovered);
                canvas.endFrame();
                for (const auto handle : handles) {
                    unsigned matches = 0;
                    for (const auto& fill : fills) {
                        if (std::abs((fill.bounds[0] + fill.bounds[2]) * 0.5f - handle.x) > 0.01f ||
                            std::abs((fill.bounds[1] + fill.bounds[3]) * 0.5f - handle.y) > 0.01f)
                            continue;
                        ++matches;
                        check(sameColor(fill.color, colors.activityPlayback),
                              "every envelope handle uses skin playback RGBA, including on hover");
                    }
                    check(matches == 1, "each envelope handle submits one identifiable fill");
                }
            }
        }
    }
    nvgDeleteInternal(context);
}

void colorsAndPadIndices()
{
    sms::ui::Skin skin;
    unsigned index = 0;
    for (const auto& field : themeColorFields)
        skin.colors[std::string(field.name)] = testColor(index++);
    for (std::size_t pad = 0; pad < Theme::padColorCount; ++pad)
        skin.colors["pad" + std::to_string(pad + 1)] = testColor(index++);
    skin.colors["unknown"] = "#FFFFFFFF";
    const auto mapped = skinTheme(skin);
    index = 0;
    for (const auto& field : themeColorFields)
        check(sameColor(mapped.*field.member, expectedColor(index++)), "every semantic color maps RGBA");
    const Theme defaults;
    for (std::size_t pad = 0; pad < Theme::padColorCount; ++pad) {
        check(sameColor(mapped.padColors[pad].tint, expectedColor(index++)),
              "saved one-based palette index maps corresponding RGBA");
        check(std::string(mapped.padColors[pad].name) == defaults.padColors[pad].name,
              "skin preserves saved palette identities");
    }
    for (const auto& field : themeColorFields)
        skin.colors[std::string(field.name)] = "#wrong";
    for (std::size_t pad = 0; pad < Theme::padColorCount; ++pad)
        skin.colors["pad" + std::to_string(pad + 1)] = "#wrong";
    const auto fallback = skinTheme(skin);
    for (const auto& field : themeColorFields)
        check(sameColor(fallback.*field.member, defaults.*field.member), "invalid semantic colors fall back");
    for (std::size_t pad = 0; pad < Theme::padColorCount; ++pad)
        check(sameColor(fallback.padColors[pad].tint, defaults.padColors[pad].tint),
              "invalid pad colors fall back");
}

void safeMetrics()
{
    const Theme defaults;
    for (const auto& field : themeMetricFields) {
        sms::ui::Skin skin;
        for (const auto value : {field.minimum, (field.minimum + field.maximum) / 2.0f, field.maximum}) {
            skin.metrics[std::string(field.name)] = std::to_string(value);
            check(skinTheme(skin).*field.member == value, "metric endpoints and interior accepted");
        }
        for (const auto& value : {std::to_string(field.minimum - 1.0f),
                                 std::to_string(field.maximum + 1.0f), std::string("nan"),
                                 std::string("inf"), std::string("1px")}) {
            skin.metrics[std::string(field.name)] = value;
            check(skinTheme(skin).*field.member == defaults.*field.member,
                  "out-of-range or invalid metrics retain built-in value");
        }
    }
}

void independentOpacityControls()
{
    sms::ui::Skin skin;
    const auto original = skinTheme(skin);
    check(original.backgroundAlpha == 1.0f && original.padAlpha == 1.0f &&
          original.padPanelAlpha == 1.0f, "older skins retain full opacity");
    skin.metrics["backgroundAlpha"] = "0.75";
    skin.metrics["padAlpha"] = "0";
    skin.metrics["padPanelAlpha"] = "0.35";
    const auto transparent = skinTheme(skin);
    check(transparent.backgroundAlpha == 0.75f && transparent.padAlpha == 0.0f &&
          transparent.padPanelAlpha == 0.35f,
          "background, pad surfaces and backing panel opacity are independent");
    check(transparent.padTintAlpha == original.padTintAlpha &&
          sameColor(transparent.selection, original.selection) &&
          sameColor(transparent.contentPrimary, original.contentPrimary) &&
          sameColor(transparent.controlAccent, original.controlAccent),
          "surface transparency preserves saved tints, labels, selection and knob colors");
}

void logoGlowLoading()
{
    const Theme defaults;
    check(!logoGlowEnabled(defaults), "legacy themes leave glow and animation disabled");
    auto enabled = defaults;
    enabled.logoGlowAlpha = 0.7f;
    check(logoGlowEnabled(enabled), "nonzero alpha enables glow");
    enabled.logoGlowRadius = 0.0f;
    check(!logoGlowEnabled(enabled), "zero radius disables glow");
    enabled.logoGlowRadius = 10.0f;
    enabled.logoGlow.alpha = 0.0f;
    check(!logoGlowEnabled(enabled), "transparent glow color disables glow");
    const auto path = (std::filesystem::path(ANVILLESAMPLER_SKIN_TEMPLATE_DIR) /
                       "../../res/anville.png").lexically_normal().string();
    std::ifstream input(path, std::ios::binary);
    check(static_cast<bool>(input), "original logo is readable for glow test");
    const std::vector<std::uint8_t> png{std::istreambuf_iterator<char>(input),
                                      std::istreambuf_iterator<char>()};
    const auto embedded = loadLogoGlow(nullptr, png, 10.0f);
    const auto custom = loadLogoGlow(path.c_str(), {}, 10.0f);
    check(!embedded.empty() && embedded == custom,
          "embedded and custom logos use identical alpha decoding");
    bool visible = false, transparent = false;
    for (std::size_t index = 3; index < embedded.size(); index += 4) {
        visible |= embedded[index] != 0;
        transparent |= embedded[index] == 0;
    }
    check(visible && transparent, "real original logo creates a confined visible silhouette halo");
    const std::array<std::uint8_t, 4> corrupt{0, 1, 2, 3};
    check(loadLogoGlow(nullptr, corrupt, 10.0f).empty(), "unreadable image fails without a glow");
}

void scopeRestorationAndTranslations()
{
    const auto* defaultTheme = &theme();
    const char* literal = "PLAY";
    check(themeText(literal) == literal, "outside scope returns original literal");
    Theme first, second;
    sms::ui::Skin firstSkin, secondSkin;
    firstSkin.strings["PLAY"] = "SOITA";
    secondSkin.strings["PLAY"] = "JOUER";
    {
        ThemeScope outer(first, firstSkin);
        check(&theme() == &first && std::string(themeText("PLAY")) == "SOITA", "outer theme and translation selected");
        const char* outerText = themeText("PLAY");
        {
            ThemeScope inner(second, secondSkin);
            check(&theme() == &second && std::string(themeText("PLAY")) == "JOUER",
                  "nested scope selects its own theme and translation");
            check(themeText("unknown") == secondSkin.text("unknown"), "untranslated text retains literal");
        }
        check(&theme() == &first && themeText("PLAY") == outerText, "nested scope restores outer snapshot");
        try {
            ThemeScope exceptional(second, secondSkin);
            throw 1;
        } catch (int) {}
        check(&theme() == &first && themeText("PLAY") == outerText, "exception unwind restores scope");
    }
    check(&theme() == defaultTheme && themeText(literal) == literal && activeSkin == nullptr,
          "final scope restores built-ins and literal text");
}

void concurrentScopeIsolation()
{
    std::barrier rendezvous(3);
    std::atomic<bool> correct{true};
    const auto* defaultTheme = &theme();
    auto worker = [&](const char* replacement) {
        Theme ownTheme;
        sms::ui::Skin ownSkin;
        ownSkin.strings["PLAY"] = replacement;
        {
            ThemeScope scope(ownTheme, ownSkin);
            rendezvous.arrive_and_wait();
            for (unsigned repeat = 0; repeat < 5000; ++repeat)
                if (&theme() != &ownTheme || std::string(themeText("PLAY")) != replacement)
                    correct = false;
            rendezvous.arrive_and_wait();
        }
        if (activeTheme != nullptr || activeSkin != nullptr) correct = false;
    };
    std::thread first(worker, "FIRST");
    std::thread second(worker, "SECOND");
    rendezvous.arrive_and_wait();
    check(&theme() == defaultTheme && std::string(themeText("PLAY")) == "PLAY",
          "concurrent editor scopes do not affect main thread");
    rendezvous.arrive_and_wait();
    first.join();
    second.join();
    check(correct, "concurrent editor themes and translations stay independent and restore");
}

void placeholderCatalog()
{
    const auto skin = sms::ui::Skin::load(ANVILLESAMPLER_SKIN_TEMPLATE_DIR);
    check(skin.valid(), "placeholder manifest loads");
    const auto mapped = skinTheme(skin);
    const Theme defaults;
    for (const auto& field : themeColorFields) {
        check(skin.colors.contains(field.name), "placeholder catalogs every semantic color");
        check(sameColor(mapped.*field.member, defaults.*field.member), "placeholder preserves built-in color");
    }
    for (const auto& field : themeMetricFields) {
        check(skin.metrics.contains(field.name), "placeholder catalogs every metric");
        float manifestValue = 0.0f;
        check(sms::ui::Skin::parseMetric(skin.metrics.find(field.name)->second, manifestValue) &&
              manifestValue == defaults.*field.member, "placeholder metric literal equals built-in value");
        check(mapped.*field.member == defaults.*field.member, "placeholder preserves built-in metric");
    }
    for (std::size_t pad = 0; pad < Theme::padColorCount; ++pad) {
        check(skin.colors.contains("pad" + std::to_string(pad + 1)), "placeholder catalogs every palette index");
        check(sameColor(mapped.padColors[pad].tint, defaults.padColors[pad].tint),
              "placeholder preserves built-in pad palette");
    }
    check(!skin.imagePath("background").empty() && !skin.imagePath("logo").empty(),
          "placeholder background and logo resolve to existing confined files");
}
} // namespace

int main()
{
    envelopeHandleColors();
    colorsAndPadIndices();
    safeMetrics();
    independentOpacityControls();
    logoGlowLoading();
    scopeRestorationAndTranslations();
    concurrentScopeIsolation();
    placeholderCatalog();
    std::cout << "Skin theme tests passed\n";
}
