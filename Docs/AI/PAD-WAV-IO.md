# Pad WAV import and export

Implemented contract for pad file operations.
Actions use the [pad context menu](PAD-CONTEXT-MENU.md) without settling
[issue #10](https://github.com/sirsipe/SMS-Plugins/issues/10).

## Action contract

- **Export WAV...** writes the complete stored sample. Disable it for an empty
  pad or when no Save dialog is available.
- **Export Processed WAV...** writes the selected region with per-pad Gain, Pan,
  Tune and ADSR applied.
  Disable it when Export is disabled or sanitized region, ADSR and Gain/Pan/Tune settings
  are all defaults.
- **Import WAV...** replaces the clicked pad only after a successful decode and
  resets playback and mixer settings to defaults. Failure or cancellation changes nothing.

Raw Export excludes cut points, ADSR, velocity, global gain, monitoring,
playback mode, and voice management. Processed Export uses the existing
one-shot region and automatic-release semantics at full velocity and unity
global gain; it includes per-pad Gain/Pan/Tune, region, and ADSR. Render at the stored source rate and
reuse shared DSP behavior so offline output cannot drift from playback.

Imported audio continues through existing embedded per-pad project state. The
source path is an action input, not durable state: projects must restore after
the original file is moved or deleted.

## Dependency-free WAV contract

Use the bounded RIFF/WAVE codec in `Common-Src/Audio`; avoid external libraries. Import little-endian mono or stereo in these common DAW encodings:

- integer PCM at 16, 24, or 32 bits;
- IEEE 32-bit floating point;
- classic format tags and `WAVE_FORMAT_EXTENSIBLE` PCM/float subtypes.

Duplicate mono into stereo. Parse chunks instead of assuming a 44-byte header;
accept unknown metadata chunks and RIFF padding. Validate format consistency,
finite float samples, arithmetic overflow, declared versus available data,
sample rate, duration, and storage before allocation or replacement. Reject
empty, malformed, compressed, float64, multichannel, RF64/WAVE64, unsupported,
over-duration, and over-capacity files with useful UI status. The import limit
is eight minutes from source frames and sample rate; the shared sampler pool
may fill sooner when other pads contain audio. A 2 GiB file-read bound covers
the supported eight-minute stereo float32 WAV at 384 kHz.

Both exports remain little-endian stereo PCM16 RIFF/WAVE at the stored source
rate, matching project-state precision and minimizing writer complexity. Append
`.wav` when the chosen name lacks an extension; reject a conflicting extension
rather than creating mislabeled data.

## Responsibilities and real-time safety

`Common-Src` owns the byte-level codec, offline renderer, and reusable command
dispatcher, without DPF or dialog policy. `Common-UI` owns menu geometry and
drawing. Anvil Sampler UI code adapts DPF dialogs; its plug-in adapter owns action
transport, filesystem policy, status delivery, and safe engine access. VST3 queues transient work on an instance worker; LV2 uses its required host
worker. Both use the same engine snapshot/commit contract. Keep these
responsibilities narrow rather than creating a file-manager class.

Never read, write, decode, encode, allocate, lock, or render whole samples in
the audio callback. Decode off-thread and validate completely before publishing
a replacement without racing `process()`. Export a retained immutable snapshot and prepare replacement PCM before a
bounded commit; see [Storage handoff](PAD-STORAGE.md).
Destruction must join or cancel workers outside the callback. Serialize
conflicting operations per pad and expose a non-blocking busy state. Do not send
PCM through UI state; LV2 UIs may run separately.

## File dialogs and Linux support

Enable DPF's asynchronous `openFileBrowser()` API. DPF has no extension
filters, so validate after selection. Its Linux X11 fallback cannot save.

### Dev Container prerequisite

The Dev Container exercises Linux portal dialogs. It installs `libdbus-1-dev`, `xdg-desktop-portal`, and the GTK backend,
runs the desktop inside its own `dbus-run-session`, and explicitly selects GTK
for Openbox. VS Code shells share its container-local bus; no host D-Bus socket is mounted.

`desktop-health` proves that `pkg-config` finds `dbus-1`, the session bus works,
and `org.freedesktop.portal.Desktop` activates with FileChooser.
`desktop-health --dialogs` opens and dismisses real Open and Save dialogs on VNC;
`make dev-ready` runs this stronger check. Launch a host with
`DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/sms-no-session-bus` to test the
portal-absent fallback without disrupting the shared bus. Simply unsetting the
variable may rediscover the session through X11. Complete these checks before
changing DPF file browsing.

Linux release builds must deliberately enable D-Bus and fail configuration if
development files are absent. `libdbus-1` is a runtime requirement. Export
requires a working `xdg-desktop-portal` FileChooser and a desktop backend such
as GNOME, GTK, or KDE. Import and non-file features remain usable without the
portal.

DPF has no non-opening Save-capability query. Prefer adding that query in
DPF/DGL so export actions disable immediately; opening can still fail after a
successful probe and must report an error. Without the query, explain the first
failed Save attempt and disable export for that UI session. Verify or fix pinned
Windows Save flags and validate `NSSavePanel` on macOS. Do not invoke `zenity`,
`kdialog`, or similar external fallbacks.

VST3 reports file completion through a hidden output.
Anvil Sampler's [VST3 UI message bus](DPF-VST3-CONSTRAINTS.md) returns waveform
and editor state after import and pad reselection. LV2 uses DPF's callback.
Test long files and DAW restoration without source files.

## Validation and deferred public documentation

Unit-test every accepted encoding and extensible subtype, round trips, mono
conversion, chunk order/padding, malformed sizes, non-finite floats, and
duration/capacity limits. Compare processed output with playback semantics.
Exercise cancellation, failures, overwrites, Unicode paths, active audio,
project restoration without the source file, and portal-present/absent Linux
sessions in LV2 and VST3 hosts. Windows/macOS claims require
[native platform checks](PLATFORM-BUILDS.md).

[WAV guide](../../SMS-AnvilSampler/Docs/WAV-FILES.md) covers the
right-click workflow, encodings, conversion/export semantics, reset behavior,
duration and project storage, plus Linux requirements and troubleshooting.
Keep it aligned with this contract and verified behavior.
