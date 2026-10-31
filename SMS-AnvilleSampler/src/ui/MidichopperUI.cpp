/*
 * SMS-Anville Sampler DPF UI adapter.
 * SamplerUiSession owns sampler/editor synchronization; input, mixer and
 * application-menu controllers own interaction. GL resources stay here.
 */
#ifndef DISTRHO_UI_USE_NANOVG
# define DISTRHO_UI_USE_NANOVG 1
#endif
#include "DistrhoUI.hpp"
#include "DistrhoPluginUtils.hpp"
#include "DPF/NanoUI.hpp"
#include "DPF/SkinTheme.hpp"
#include "DPF/LogoGlow.hpp"
#include "SkinCatalog.hpp"
#include "SamplerInputController.hpp"
#include "MidichopperView.hpp"
#include "BrowserLauncher.hpp"
#include "KnobPreference.hpp"
#include "PluginUiBridge.hpp"
#include "StateKeys.hpp"
#include "anvillesampler_artwork.hpp"

#include <algorithm>
#include <chrono>
#include <optional>

START_NAMESPACE_DISTRHO
namespace {
using namespace midichopper::plugin;
using namespace midichopper::ui;
namespace uiLayout = midichopper::ui::layout;
constexpr auto kMeterFrameInterval = std::chrono::milliseconds(33);
constexpr std::size_t kPadColorMenuItemCount = sms::ui::dpf::Theme::padColorCount + 1U;
KnobAdjustment knobAdjustment(const uint modifiers) noexcept
{
    if ((modifiers & DGL_NAMESPACE::kModifierControl) != 0U) return KnobAdjustment::stepped;
    if ((modifiers & DGL_NAMESPACE::kModifierShift) != 0U) return KnobAdjustment::fine;
    return KnobAdjustment::normal;
}
} // namespace

class MidichopperUI final : public sms::ui::dpf::NanoUI,
    private SamplerUiHost, private MixerInteractionHost,
    private ApplicationMenuHost, private SamplerInputHost {
public:
    MidichopperUI()
        : NanoUI(uiLayout::canvasWidth, uiLayout::canvasHeight,
                 uiLayout::minimumWidth, uiLayout::minimumHeight),
          fSkinCatalog(sms::ui::SkinCatalog::discover(skinDirectory())),
          fSkinPreferencePath(sms::ui::SkinPreference::configPath()),
          fSelectedSkin(fSkinCatalog.selectedIndex(sms::ui::SkinPreference::read(fSkinPreferencePath))),
          fSkin(fSkinCatalog.load(fSelectedSkin)), fTheme(sms::ui::dpf::skinTheme(fSkin)),
          fSession(*this, {DISTRHO_PLUGIN_WANT_STATE != 0, DISTRHO_PLUGIN_WANT_MIDI_INPUT != 0,
              DISTRHO_UI_FILE_BROWSER != 0, static_cast<int>(sms::ui::dpf::Theme::padColorCount)}),
          fMixer(*this, DISTRHO_PLUGIN_WANT_STATE != 0), fMenu(*this, ANVILLESAMPLER_HELP_REF),
          fInput(*this, fSession, fMixer, fMenu, static_cast<int>(kPadColorMenuItemCount))
    {
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (void* const instance = getPluginInstancePointer()) {
            fUiBridge = static_cast<PluginUiBridge*>(static_cast<Plugin*>(instance));
            fUiMessageCursor = fUiBridge->uiMessageCursor();
        }
#endif
#ifndef DGL_NO_SHARED_RESOURCES
        loadSharedResources();
#endif
    }

    ~MidichopperUI() override
    {
        fSession.stopEditorAudition();
        if (!fSession.chopEditor().active()) return;
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (fUiBridge != nullptr) { fUiBridge->stopUiPreview(); return; }
#endif
        fSession.chopEditor().disableMidi();
    }

protected:
    void parameterChanged(const uint32_t index, const float value) override
    { fSession.acceptParameter(index, value); }
#if DISTRHO_PLUGIN_WANT_STATE
    void stateChanged(const char* key, const char* value) override
    {
        if (key == nullptr || value == nullptr) return;
        if (!fMixer.acceptState(key, value)) fSession.acceptState(key, value);
    }
#endif
    void onUiIdle() override
    {
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
        if (fUiBridge != nullptr) {
            UiMessageBus::Message message;
            bool skipped = false;
            while (fUiBridge->readUiMessage(fUiMessageCursor, message, skipped)) {
                if (skipped) fSession.missedReplies();
                stateChanged(message.key.data(), message.value.data());
            }
        }
#endif
        const auto now = std::chrono::steady_clock::now();
        if (getWindow().isVisible() && fLogoGlow && fLogoGlow->isValid() &&
            fTheme.logoGlowPulseSeconds > 0.0f && now >= fNextLogoGlowRepaint) {
            fNextLogoGlowRepaint = now + kMeterFrameInterval;
            requestRepaint();
        }
        fInput.idle(now);
        fSession.idle(now, fMixer.effective(kParameterGlobalTuneSemitones, fSession.state().globalTune));
    }

    void onNanoDisplay() override
    {
        // Texture deletion/creation needs this editor's current GL context.
        if (fPendingSkin) {
            fLogo.reset();
            fLogoGlow.reset();
            fBackground.reset();
            fSkin = std::move(*fPendingSkin);
            fPendingSkin.reset();
            fTheme = sms::ui::dpf::skinTheme(fSkin);
            fSkinImagesLoaded = false;
        }
        const sms::ui::dpf::ThemeScope themeScope(fTheme, fSkin);
        // Any full redraw presents the latest levels, even when another control
        // caused it, so do not schedule a redundant meter-only frame.
        const auto now = std::chrono::steady_clock::now();
        fSession.rendered(now);
        fNextLogoGlowRepaint = now + kMeterFrameInterval;

        const float w = static_cast<float>(getWidth());
        const float h = static_cast<float>(getHeight());

        beginPath();
        rect(0.0f, 0.0f, w, h);
        fillColor(sms::ui::dpf::theme().canvas);
        fill();

        beginLogicalDisplay();
        // One attempt per editor. Missing/invalid assets retain built-in art.
        // NanoVG images require the current GL context, so defer to first draw.
        if (!fSkinImagesLoaded) {
            fSkinImagesLoaded = true;
            const auto flags = DGL_NAMESPACE::NanoVG::IMAGE_GENERATE_MIPMAPS;
            const auto& logoPath = fSkin.imagePath("logo");
            const auto logoBytes = sms::ui::skinFileIO::readImage(
                sms::ui::skinFileIO::pathFromUtf8(logoPath));
            if (!logoBytes.empty())
                fLogo.emplace(createImageFromMemory(logoBytes.data(),
                    static_cast<uint>(logoBytes.size()), flags));
            const bool customLogoLoaded = fLogo && fLogo->isValid();
            if (!customLogoLoaded)
                fLogo.emplace(createImageFromMemory(anvillesampler_artwork::anvilleData,
                                              anvillesampler_artwork::anvilleDataSize, flags));
            if (sms::ui::dpf::logoGlowEnabled(fTheme)) {
                const auto pixels = sms::ui::dpf::loadLogoGlow(
                    customLogoLoaded ? std::span<const std::uint8_t>(logoBytes)
                        : std::span<const std::uint8_t>(anvillesampler_artwork::anvilleData,
                                                       anvillesampler_artwork::anvilleDataSize),
                    fTheme.logoGlowRadius);
                if (!pixels.empty())
                    fLogoGlow.emplace(createImageFromRGBA(sms::ui::logoGlowWidth,
                        sms::ui::logoGlowHeight, pixels.data(), flags));
            }
            fLogoGlowEpoch = std::chrono::steady_clock::now();
            const auto& backgroundPath = fSkin.imagePath("background");
            const auto backgroundBytes = sms::ui::skinFileIO::readImage(
                sms::ui::skinFileIO::pathFromUtf8(backgroundPath));
            if (!backgroundBytes.empty())
                fBackground.emplace(createImageFromMemory(backgroundBytes.data(),
                    static_cast<uint>(backgroundBytes.size()), flags));
        }
        char collapseLabel[48];
        const int collapsingPads = fSession.padActions().collapseShiftCount(fSession.padActionContext());
        if (collapsingPads == 0) {
            std::snprintf(collapseLabel, sizeof(collapseLabel), "%s", fSkin.text("COLLAPSE GAP"));
        } else {
            std::snprintf(collapseLabel, sizeof(collapseLabel), "%s (%d %s)", fSkin.text("COLLAPSE GAP"),
                          collapsingPads, fSkin.text(collapsingPads == 1 ? "PAD" : "PADS"));
        }
        std::array<sms::ui::ContextMenuItemView, kPadMenuEntries.size()>
            contextMenuItems{};
        for (std::size_t index = 0; index < contextMenuItems.size(); ++index) {
            const PadMenuEntry entry = kPadMenuEntries[index];
            contextMenuItems[index] = entry.kind == sms::ui::ContextMenuItemKind::separator
                ? sms::ui::ContextMenuItemView{
                    "", false, false, sms::ui::ContextMenuItemKind::separator}
                : fSession.padActions().actionView(fSession.padActionContext(), entry, collapseLabel);
        }
        std::array<sms::ui::ContextMenuItemView, kPadColorMenuItemCount>
            colorMenuItems{};
        const int selectedColor = fSession.padActions().target() >= 0
            ? fSession.state().padColors[static_cast<std::size_t>(fSession.padActions().target())] : 0;
        colorMenuItems[0] = {"NONE", true, false,
                             sms::ui::ContextMenuItemKind::action,
                             selectedColor == 0};
        const auto& palette = sms::ui::dpf::theme().padColors;
        for (std::size_t index = 0; index < palette.size(); ++index)
            colorMenuItems[index + 1U] = {
                palette[index].name, true, false,
                sms::ui::ContextMenuItemKind::action,
                selectedColor == static_cast<int>(index + 1U),
                static_cast<int>(index)};
        std::array<sms::ui::ContextMenuItemView, uiLayout::skinMenuMaximumChoices + 2> skinMenuItems{};
        const int skinRows = uiLayout::skinMenuVisibleCount(static_cast<int>(fSkinCatalog.entries.size()));
        for (int row = 0; row < skinRows; ++row) {
            const auto index = static_cast<std::size_t>(uiLayout::skinMenuEntry(row, fMenu.skinOffset()));
            skinMenuItems[static_cast<std::size_t>(row)] = {
                fSkinCatalog.entries[index].name.c_str(), true, false,
                sms::ui::ContextMenuItemKind::action, index == fSelectedSkin,
                -1, false, index == 0U};
        }
        skinMenuItems[static_cast<std::size_t>(skinRows)] = {
            "", false, false, sms::ui::ContextMenuItemKind::separator};
        skinMenuItems[static_cast<std::size_t>(skinRows + 1)] = {"<Get More>"};
        const auto editorSettings = fSession.displayEditorSettings();
        const midichopper::ui::ViewState view{
            fSession.state().armed, fSession.state().captureMode, fSession.state().fixedLength, fSession.state().playbackMode, fSession.state().monitor,
            fSession.state().startPad, fSession.state().preRoll, fSession.state().baseNote, fSession.state().midiBankMode,
            fMixer.effective(kParameterOutputGainDb, fSession.state().gain), fSession.state().monitorGain,
            fMixer.effective(kParameterGlobalPan, fSession.state().globalPan),
            fMixer.effective(kParameterGlobalTuneSemitones, fSession.state().globalTune),
            fMixer.effective(kParameterGlobalLowpass, fSession.state().globalLowpass),
            fMixer.effective(kParameterGlobalHighpass, fSession.state().globalHighpass), fSession.state().globalFilterSlope, fSession.state().globalDirty, fSession.state().globalReverse,
            fSession.state().maxVoices, fSession.state().bank, fSession.state().layout,
            fSession.state().selectedPad, fSession.state().currentPad, fSession.padPress().pad(), fInput.clearArmed(), fMenu.open(),
            fMenu.resetArmed(), fSession.resetEnabled(),
            fSession.padActions().menuOpen(), fSession.padActions().menu(),
            std::span<const sms::ui::ContextMenuItemView>{contextMenuItems},
            fSession.padActions().colorMenuOpen(), fSession.padActions().colorMenu(),
            std::span<const sms::ui::ContextMenuItemView>{colorMenuItems},
            fInput.hovered(),
            fMixer.entry().target(), fMixer.entry().text(),
            fSession.state().editorMode, fSession.chopEditor().active(), fSession.chopEditor().split(), fSession.state().editorAuditionMode,
            fSession.state().anyPlaybackActive, fSession.state().hasWaveform,
            fSession.state().inputLevels, fSession.state().outputLevels, fSession.state().padState, fSession.state().padStatus, fSession.state().padColors,
            editorSettings, fSession.state().mixerSettings, fSession.state().waveform, fSession.playbackIndicator().position(),
            std::span<const sms::audio::WaveformSummary>{
                fSession.chopEditor().waveforms().data(), fSession.chopEditor().waveforms().size()},
            std::span<const std::int64_t>{
                fSession.chopEditor().offsets().data(), fSession.chopEditor().offsets().size()},
            fSession.chopEditor().firstPad(), fSession.state().selectedPad, fSession.chopEditor().previewPosition(), fSession.chopEditor().previewPad(),
            fSession.chopEditor().activeBoundary(), fSession.chopEditor().ready(),
            fSession.chopEditor().dirty(), fSession.chopEditor().applying(),
            fSession.chopEditor().canNavigate(-1, fSession.globalPad(0), fSession.visiblePadCount(), fSession.midiBankMode()),
            fSession.chopEditor().canNavigate(1, fSession.globalPad(0), fSession.visiblePadCount(), fSession.midiBankMode()), fSession.state().status,
            fSession.detail().viewport(), fSession.detail().detail(),
            fLogo ? &*fLogo : nullptr, fBackground ? &*fBackground : nullptr,
            fLogoGlow ? &*fLogoGlow : nullptr,
            sms::ui::logoGlowOpacity(fTheme.logoGlowAlpha, fTheme.logoGlowPulseSeconds,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - fLogoGlowEpoch).count()),
            fSkinCatalog.selectable(), fMenu.open() ? fMenu.submenu() : uiLayout::MenuSubmenu::none, fMenu.skinMenu(),
            std::span<const sms::ui::ContextMenuItemView>{skinMenuItems.data(),
                static_cast<std::size_t>(skinRows + 2)}, fMenu.skinOffset(),
            static_cast<int>(fSkinCatalog.entries.size()),
            fMixer.menu(), fMixer.settings(),
        };
        midichopper::ui::draw(*this, view);
        endLogicalDisplay();

    }

    bool onMouse(const MouseEvent& ev) override
    {
        const auto point = logicalPoint(ev.pos);
        const auto button = ev.button == DGL_NAMESPACE::kMouseButtonLeft ? InputButton::left
            : ev.button == DGL_NAMESPACE::kMouseButtonMiddle ? InputButton::middle
            : ev.button == DGL_NAMESPACE::kMouseButtonRight ? InputButton::right : InputButton::other;
        return fInput.mouse(point, button, ev.press, knobAdjustment(ev.mod));
    }
    bool onMotion(const MotionEvent& ev) override { return fInput.motion(logicalPoint(ev.pos)); }
    bool onScroll(const ScrollEvent& ev) override
    {
        float delta = static_cast<float>(ev.delta.getY());
        const bool shift = (ev.mod & DGL_NAMESPACE::kModifierShift) != 0U;
        if (delta == 0.0f) {
            if (ev.direction == DGL_NAMESPACE::kScrollUp) delta = 1.0f;
            else if (ev.direction == DGL_NAMESPACE::kScrollDown) delta = -1.0f;
            else if (shift) {
                delta = -static_cast<float>(ev.delta.getX());
                if (delta == 0.0f && ev.direction == DGL_NAMESPACE::kScrollLeft) delta = 1.0f;
                else if (delta == 0.0f && ev.direction == DGL_NAMESPACE::kScrollRight) delta = -1.0f;
            }
        }
        return fInput.scroll(logicalPoint(ev.pos), delta,
            (ev.mod & DGL_NAMESPACE::kModifierControl) != 0U, shift, knobAdjustment(ev.mod));
    }
    bool onKeyboard(const KeyboardEvent& ev) override
    {
        const auto key = ev.key == ' ' ? InputKey::space
            : ev.key == DGL_NAMESPACE::kKeyEscape ? InputKey::escape
            : isMixerValueCommitKey(ev.key) ? InputKey::commit
            : ev.key == DGL_NAMESPACE::kKeyTab ? InputKey::tab
            : ev.key == DGL_NAMESPACE::kKeyBackspace ? InputKey::backspace
            : ev.key == DGL_NAMESPACE::kKeyDelete ? InputKey::deleteText : InputKey::other;
        return fInput.keyboard(key, ev.press, (ev.mod & DGL_NAMESPACE::kModifierShift) != 0U);
    }
    bool onCharacterInput(const CharacterInputEvent& ev) override { return fInput.character(ev.character); }
    void uiFocus(const bool focus, DGL_NAMESPACE::CrossingMode) override { fInput.focus(focus); }
#if DISTRHO_UI_FILE_BROWSER
    void uiFileBrowserSelected(const char* filename) override { fSession.padActions().fileSelected(filename); }
#endif

private:
    sms::ui::Point logicalPoint(const DGL_NAMESPACE::Point<double>& point) const
    {
        const auto logical = toLogicalPosition(point);
        return {logical.getX() - uiLayout::contentOffsetX, logical.getY()};
    }
    std::filesystem::path skinDirectory() const
    {
#if defined(_WIN32)
        return sms::ui::Skin::directoryForNativeBinary(sms::ui::skinFileIO::moduleBinaryPath(),
                                                       "SMS-AnvilleSampler.skin");
#else
        return sms::ui::Skin::directoryForPlugin(getBundlePath(), getBinaryFilename(), "SMS-AnvilleSampler.skin");
#endif
    }
    void sendUiParameter(const std::uint32_t parameter, const float value) override
    { setParameterValue(parameter, value); }
    void sendUiState(const char* key, const std::string& value) override
    {
#if DISTRHO_PLUGIN_WANT_STATE
        setState(key, value.c_str());
#else
        static_cast<void>(key); static_cast<void>(value);
#endif
    }
    void sendUiNote(const std::uint8_t note, const std::uint8_t velocity) override
    {
#if DISTRHO_PLUGIN_WANT_MIDI_INPUT
        sendNote(0, note, velocity);
#else
        static_cast<void>(note); static_cast<void>(velocity);
#endif
    }
    void repaintUi() override { requestRepaint(); }
    bool showUiPadFileBrowser(const bool saving, const char* defaultName) override
    {
#if DISTRHO_UI_FILE_BROWSER
        FileBrowserOptions options;
        options.saving = saving; options.defaultName = defaultName;
        options.title = fSkin.text(saving ? "Export pad as WAV" : "Import WAV into pad");
        return openFileBrowser(options);
#else
        static_cast<void>(saving); static_cast<void>(defaultName); return false;
#endif
    }
    void invalidateUiInteraction(const UiInteractionInvalidation reason) override { fInput.invalidate(reason); }
    void editMixerParameter(const std::uint32_t parameter, const bool begin) override { editParameter(parameter, begin); }
    void setMixerParameter(const std::uint32_t parameter, const float value) override { fSession.setControlValue(parameter, value); }
    void commitPadMixer(const int pad, const sms::dsp::SampleMixerSettings& settings) override
    { if (pad == fSession.state().selectedPad) fSession.commitMixerSettings(settings); }
    void saveGlobalMixerSettings(const std::uint32_t parameter, const GlobalValueSettings& settings) override
    {
        fSession.setLocalStatus("");
        sendUiState(kGlobalControlStateKeys[globalControlIndex(parameter)], encodeGlobalValueSettings(settings));
        if (!KnobPreference::write(KnobPreference::configPath(), parameter, settings))
            fSession.setLocalStatus("Knob settings changed, but defaults could not be saved");
    }
    void setMixerStatus(const char* message) override { fSession.setLocalStatus(message); }
    void repaintMixer() override { requestRepaint(); }
    void focusMixer() override { getWindow().focus(); }
    void clearMixerHover() noexcept override { fInput.clearHover(); }
    void chooseLayout(const int layout) override { fSession.selectLayout(layout); }
    void chooseMidiBankMode(const int mode) override { fSession.selectMidiBankMode(mode); }
    bool chooseSkin(const std::size_t index) override
    {
        if (!fSkinCatalog.selectable() || index >= fSkinCatalog.entries.size()) return false;
        auto skin = fSkinCatalog.load(index);
        if (index != 0U && !skin.valid()) {
            fSession.setLocalStatus("Could not load skin; reopen the editor to refresh the list");
            return false;
        }
        fSelectedSkin = index; fPendingSkin = std::move(skin);
        if (!sms::ui::SkinPreference::write(fSkinPreferencePath, fSkinCatalog.entries[index].id))
            fSession.setLocalStatus("Skin changed, but the preference could not be saved");
        return true;
    }
    void clearAllPads() override { fSession.clearAllPadContents(); }
    void setStatus(const char* message) override { fSession.setLocalStatus(message); }
    void repaintApplicationMenu() override { requestRepaint(); }
    void clearHover() noexcept override { fInput.clearHover(); }
    bool openLink(const char* url) override { return sms::ui::openBrowser(url); }
    void focusSamplerInput() override { getWindow().focus(); }
    void ignoreSamplerKeyRepeat(const bool ignore) override { getWindow().setIgnoringKeyRepeat(ignore); }
    void editSamplerParameter(const std::uint32_t parameter, const bool begin) override { editParameter(parameter, begin); }
    void repaintSamplerInput() override { requestRepaint(); }
    ApplicationMenuContext applicationMenuContext() const override
    { return {fSkinCatalog.selectable(), static_cast<int>(fSkinCatalog.entries.size()), fSelectedSkin}; }

    const sms::ui::SkinCatalog fSkinCatalog;
    const std::filesystem::path fSkinPreferencePath;
    std::size_t fSelectedSkin = 0;
    sms::ui::Skin fSkin;
    sms::ui::dpf::Theme fTheme;
    std::optional<sms::ui::Skin> fPendingSkin;
    std::optional<DGL_NAMESPACE::NanoImage> fLogo, fLogoGlow, fBackground;
    std::chrono::steady_clock::time_point fLogoGlowEpoch{}, fNextLogoGlowRepaint{};
    bool fSkinImagesLoaded = false;
    SamplerUiSession fSession;
    MixerInteractionController fMixer;
    ApplicationMenuController fMenu;
    SamplerInputController fInput;
#if DISTRHO_PLUGIN_WANT_DIRECT_ACCESS
    PluginUiBridge* fUiBridge = nullptr;
    std::uint64_t fUiMessageCursor = 0;
#endif
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidichopperUI)
};

UI* createUI() { return new MidichopperUI(); }
END_NAMESPACE_DISTRHO
