#pragma once

#include "ChopEditorController.hpp"
#include "ContextMenu.hpp"
#include "DSP/SampleMixerSettings.hpp"
#include "DSP/SamplePlaybackSettings.hpp"
#include "../plugin/PadClipboardProtocol.hpp"
#include "../plugin/PadFileActionProtocol.hpp"

#include <array>
#include <chrono>
#include <span>
#include <string>
#include <string_view>

namespace midichopper::ui {

enum class PadMenuAction : int {
    editSample, color, adjustCutPoints, splitSample, collapseGap,
    exportRaw, exportProcessed, import, copy, paste, clear, count,
};

struct PadMenuEntry {
    PadMenuAction action = PadMenuAction::count;
    sms::ui::ContextMenuItemKind kind = sms::ui::ContextMenuItemKind::action;
};

inline constexpr std::array kPadMenuEntries{
    PadMenuEntry{PadMenuAction::editSample},
    PadMenuEntry{PadMenuAction::color},
    PadMenuEntry{PadMenuAction::count, sms::ui::ContextMenuItemKind::separator},
    PadMenuEntry{PadMenuAction::adjustCutPoints},
    PadMenuEntry{PadMenuAction::splitSample},
    PadMenuEntry{PadMenuAction::collapseGap},
    PadMenuEntry{PadMenuAction::count, sms::ui::ContextMenuItemKind::separator},
    PadMenuEntry{PadMenuAction::exportRaw},
    PadMenuEntry{PadMenuAction::exportProcessed},
    PadMenuEntry{PadMenuAction::import},
    PadMenuEntry{PadMenuAction::count, sms::ui::ContextMenuItemKind::separator},
    PadMenuEntry{PadMenuAction::copy},
    PadMenuEntry{PadMenuAction::paste},
    PadMenuEntry{PadMenuAction::count, sms::ui::ContextMenuItemKind::separator},
    PadMenuEntry{PadMenuAction::clear},
};
inline constexpr int kPadColorMenuRow = 1;

enum class PadFileDialog : std::uint8_t { none, exportRaw, exportProcessed, import };

/** Current adapter inputs. The controller owns workflow state, not this projection. */
struct PadActionContext {
    int firstPad = 0;
    int visibleCount = kPadsPerBank;
    MidiBankMode bankMode = kDefaultMidiBankMode;
    bool armed = false;
    bool editorMode = false;
    int selectedPad = -1;
    std::span<const char> padState;
    int editorSettingsPad = -1;
    sms::dsp::SamplePlaybackSettings editorSettings{};
    int mixerSettingsPad = -1;
    sms::dsp::SampleMixerSettings mixerSettings{};

    [[nodiscard]] int localPad(int pad) const noexcept;
};

/** Adapter effects; file paths remain transient transport inputs. */
class PadActionHost {
public:
    virtual ~PadActionHost() = default;
    virtual void sendPadActionState(const char* key, const std::string& value) = 0;
    virtual bool showPadFileBrowser(bool saving, const char* defaultName) = 0;
    virtual void setPadActionStatus(const char* message) = 0;
    virtual void repaintPadActions() = 0;
    virtual void clearPadActionHover() noexcept = 0;
    virtual void beginPadSampleEditor(int pad) = 0;
    virtual void refreshPadWaveform() = 0;
    virtual void setPadColor(int pad, int color) = 0;
};

/** Owns pad menu decisions and the asynchronous file/clipboard/structure lifecycle.
 * Event precedence and pointer capture belong to SamplerInputController.
 */
class PadActionController {
public:
    using Clock = std::chrono::steady_clock;
    PadActionController(PadActionHost& host, ChopEditorController& chopEditor,
                        int colorItemCount, bool stateTransport = true,
                        bool fileBrowser = true) noexcept;

    [[nodiscard]] bool menuOpen() const noexcept { return fMenuOpen; }
    [[nodiscard]] bool colorMenuOpen() const noexcept { return fColorMenuOpen; }
    [[nodiscard]] int target() const noexcept { return fTarget; }
    [[nodiscard]] const auto& menu() const noexcept { return fMenu; }
    [[nodiscard]] const auto& colorMenu() const noexcept { return fColorMenu; }
    [[nodiscard]] bool dialogPending() const noexcept { return fPendingDialog != PadFileDialog::none; }
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool actionEnabled(const PadActionContext&, PadMenuAction) const noexcept;
    [[nodiscard]] int collapseShiftCount(const PadActionContext&) const noexcept;
    [[nodiscard]] sms::ui::ContextMenuItemView actionView(
        const PadActionContext&, PadMenuEntry, const char* collapseLabel) const noexcept;

    void openMenu(const PadActionContext&, int pad, sms::ui::Point anchor);
    void closeMenu() noexcept;
    void openColorMenu(const PadActionContext&);
    void hideColorMenu() noexcept { fColorMenuOpen = false; }
    void selectColor(const PadActionContext&, int item);
    void invoke(const PadActionContext&, PadMenuEntry, Clock::time_point now = Clock::now());
    bool acceptParameter(const PadActionContext&, std::uint32_t index, float value);
    bool acceptState(const PadActionContext&, std::string_view key, const char* value);
    void fileSelected(const char* filename);
    void idle(Clock::time_point now);
    void invalidatePendingStructure() noexcept;
    void setStructureBusy(bool busy) noexcept { fStructureBusy = busy; }
    void clearClipboard() noexcept { fClipboardAvailable = false; }

private:
    [[nodiscard]] bool targetOccupied(const PadActionContext&) const noexcept;
    [[nodiscard]] bool localPadOccupied(const PadActionContext&, int localPad) const noexcept;
    [[nodiscard]] bool exportEnabled(const PadActionContext&) const noexcept;
    [[nodiscard]] bool processedExportEnabled(const PadActionContext&) const noexcept;
    [[nodiscard]] bool cutPointsEnabled(const PadActionContext&) const noexcept;
    [[nodiscard]] bool splitEnabled(const PadActionContext&) const noexcept;
    void requestSplitPlan(const PadActionContext&, int pad);
    void requestCollapseGap(const PadActionContext&, int pad);
    void openFileDialog(const PadActionContext&, PadFileDialog action);
    void requestClipboard(plugin::PadClipboardAction action);
    void requestClear();
    void clearPendingSplit() noexcept;
    void setLocalStatus(const char* message);
    [[nodiscard]] static bool hasWavFileExtension(std::string_view path) noexcept;
    [[nodiscard]] static bool hasAnyFileExtension(std::string_view path) noexcept;

    PadActionHost& fHost;
    ChopEditorController& fChopEditor;
    const int fColorItemCount;
    const bool fStateTransport;
    const bool fFileBrowser;
    bool fMenuOpen = false;
    bool fColorMenuOpen = false;
    int fTarget = -1;
    sms::ui::ContextMenuGeometry fMenu;
    sms::ui::ContextMenuGeometry fColorMenu;
    bool fClearArmed = false;
    Clock::time_point fClearDeadline{};
    bool fCollapseArmed = false;
    Clock::time_point fCollapseDeadline{};
    bool fFileBusy = false;
    bool fClipboardAvailable = false;
    bool fClipboardBusy = false;
    bool fStructureBusy = false;
    int fCollapseAvailablePad = -1;
    int fCollapseAvailableCount = 0;
    int fSplitAvailablePad = -1;
    bool fSplitAvailable = false;
    int fPendingSplitTarget = -1;
    int fPendingSplitFirst = -1;
    int fPendingSplitCount = 0;
    plugin::PadClipboardAction fActiveClipboardAction = plugin::PadClipboardAction::copy;
    int fActiveClipboardPad = -1;
    bool fSaveDialogAvailable = true;
    PadFileDialog fPendingDialog = PadFileDialog::none;
    int fPendingFilePad = -1;
    PadFileDialog fActiveFileAction = PadFileDialog::none;
    int fActiveFilePad = -1;
    plugin::PadFileResultEventTracker fFileResultEvents;
    plugin::PadClipboardResultEventTracker fClipboardResultEvents;
};

} // namespace midichopper::ui
