#pragma once

#include "SamplerUiState.hpp"
#include "ChopEditorController.hpp"
#include "EditorSnapshotSession.hpp"
#include "MidichopperInteraction.hpp"
#include "PadActionController.hpp"
#include "PlaybackIndicator.hpp"
#include "WaveformDetailSession.hpp"
#include "PadLayout.hpp"
#include "../plugin/PlaybackRequest.hpp"

#include <chrono>
#include <optional>
#include <string>

namespace midichopper::ui
{

enum class UiInteractionInvalidation {
    armChanged,
    armEntered,
    globalDirectionChanged,
    clearPads,
    clearPadsInputs,
    sampleEditorClosed,
    sampleGlobalMixerChanged,
    padActionHover,
};

/** Effects supplied by the format/UI adapter, with no framework-owned objects. */
class SamplerUiHost
{
  public:
    virtual ~SamplerUiHost() = default;
    virtual void sendUiParameter(std::uint32_t parameter, float value) = 0;
    virtual void sendUiState(const char *key, const std::string &value) = 0;
    virtual void sendUiNote(std::uint8_t note, std::uint8_t velocity) = 0;
    virtual void repaintUi() = 0;
    virtual bool showUiPadFileBrowser(bool saving, const char *defaultName) = 0;
    virtual void invalidateUiInteraction(UiInteractionInvalidation reason) = 0;
};

struct SamplerUiCapabilities {
    bool stateTransport = true;
    bool midiInput = true;
    bool fileBrowser = true;
    int colorCount = 8;
};

/** Sampler selection, audition and editor synchronization, independent of DPF. */
class SamplerUiSession final : private ChopEditorHost, private PadActionHost
{
  public:
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;

    explicit SamplerUiSession(SamplerUiHost &host,
                              SamplerUiCapabilities capabilities = {}) noexcept;

    [[nodiscard]] const SamplerUiState &state() const noexcept { return fState; }
    [[nodiscard]] const PadPressTracker &padPress() const noexcept { return fPadPress; }
    [[nodiscard]] WaveformDetailSession &detail() noexcept { return fWaveformDetail; }
    [[nodiscard]] const WaveformDetailSession &detail() const noexcept { return fWaveformDetail; }
    [[nodiscard]] const PlaybackIndicator &playbackIndicator() const noexcept
    {
        return fPlaybackIndicator;
    }
    [[nodiscard]] PadActionController &padActions() noexcept { return fPadActions; }
    [[nodiscard]] const PadActionController &padActions() const noexcept { return fPadActions; }
    [[nodiscard]] ChopEditorController &chopEditor() noexcept { return fChopEditor; }
    [[nodiscard]] const ChopEditorController &chopEditor() const noexcept { return fChopEditor; }
    [[nodiscard]] PadActionContext padActionContext() const noexcept;

    void acceptParameter(std::uint32_t index, float value, Time now = Clock::now());
    bool acceptState(const char *key, const char *value);
    void idle(Time now, float effectiveGlobalTune);
    void rendered(Time now) noexcept;
    void missedReplies();
    void setControlValue(std::uint32_t parameter, float value);
    void setLocalStatus(const char *status);

    [[nodiscard]] int visiblePadCount() const noexcept;
    [[nodiscard]] int globalPad(int localPad) const noexcept;
    [[nodiscard]] MidiBankMode midiBankMode() const noexcept;
    [[nodiscard]] int bankForGlobalPad(int pad) const noexcept;
    [[nodiscard]] int localPadForGlobalPad(int pad) const noexcept;
    [[nodiscard]] int mappedMidiNote(int pad) const noexcept;
    [[nodiscard]] int localPadFromVisualIndex(int index) const noexcept;
    [[nodiscard]] sms::ui::BankedPadLayout padLayout() const noexcept;
    [[nodiscard]] bool hasSelectedPad() const noexcept;
    [[nodiscard]] bool selectedPadOccupied() const noexcept;
    [[nodiscard]] bool resetEnabled() const noexcept;
    [[nodiscard]] bool effectiveGlobalReverse() const noexcept;
    [[nodiscard]] bool effectiveSampleReverse() const noexcept;
    [[nodiscard]] sms::dsp::SamplePlaybackSettings displayEditorSettings() const noexcept;

    void selectBank(int bank);
    void selectLayout(int layout);
    void selectMidiBankMode(int mode);
    void selectMainPadForContextMenu(int pad);
    void selectCaptureTarget(int localPad);
    void selectEditorPad(int pad, bool stopAudition = true);
    void openSampleEditor(int pad);
    void closeSampleEditor();
    void pressPlaybackPad(int localPad);
    void releasePlaybackPad();
    void captureSpace();
    void captureAction(const char *action);
    void stopEditorAudition();
    void auditionSelectedEditorPad();
    void cycleEditorAuditionMode();
    void togglePlayStop();
    void clearAllPadContents();

    void requestWaveform();
    void requestWaveformDetail();
    void commitPlaybackSettings(const sms::dsp::SamplePlaybackSettings &settings);
    void commitMixerSettings(const sms::dsp::SampleMixerSettings &settings);
    void flipSampleDirection();
    void toggleSampleGlobalMixer();

  private:
    [[nodiscard]] static int clampPad(float value);
    [[nodiscard]] int clampLocalPad(float value) const;
    [[nodiscard]] static int clampBaseNote(float value);
    [[nodiscard]] int firstEmptyLocalPad() const noexcept;
    void selectAutomaticArmTarget();
    void restoreLastPlayedSelection();
    void normalizeSelectionForContext();
    void sendPlaybackRequest(plugin::PlaybackAction action, int pad = -1);
    void startLocalPlayhead(int pad, std::optional<bool> loop = std::nullopt);
    void stopLocalPlayhead(bool holdFinal);
    void updateLocalPlayhead(Time now, float effectiveGlobalTune);
    void sendEditorSnapshotRequest(Time now);
    void commitEditorSnapshotIfReady();
    void refreshPendingEditorSnapshot() noexcept;
    bool parseEditorState(const char *key, const char *value);
    bool parseMixerState(const char *key, const char *value);
    bool parsePadColorState(const char *key, const char *value);

    void sendChopState(const char *key, const std::string &value) override;
    void selectChopPad(int pad) override;
    void leaveChopEditor() override;
    void resetChopViewport(std::uint64_t frames) override;
    void setChopStatus(const char *message) override;
    void setChopStructureBusy(bool busy) override;
    void repaintChopEditor() override;
    void sendPadActionState(const char *key, const std::string &value) override;
    bool showPadFileBrowser(bool saving, const char *defaultName) override;
    void setPadActionStatus(const char *message) override;
    void repaintPadActions() override;
    void clearPadActionHover() noexcept override;
    void beginPadSampleEditor(int pad) override;
    void refreshPadWaveform() override;
    void setPadColor(int pad, int color) override;

    SamplerUiHost &fHost;
    const SamplerUiCapabilities fCapabilities;
    SamplerUiState fState;
    PadPressTracker fPadPress;
    int fEditorAuditionPad = -1;
    bool fEditorAuditionLoop = false;
    bool fCaptureTargetRequestAlternateHalf = false;
    plugin::PlaybackPadEventTracker fPlaybackPadEvents;
    EditorSnapshotSession fEditorSnapshot;
    Time fEditorSnapshotRetryDeadline{};
    WaveformDetailSession fWaveformDetail;
    PlaybackIndicator fPlaybackIndicator;
    ChopEditorController fChopEditor;
    PadActionController fPadActions;
    bool fMeterRepaintPending = false;
    bool fCaptureStorageExhausted = false;
    Time fNextMeterRepaint{};
};

} // namespace midichopper::ui
