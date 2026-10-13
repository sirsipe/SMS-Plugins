# Testing reference

See [build](DEVELOPMENT.md).

## Automated checks

```bash
ctest --test-dir build --output-on-failure
python3 scripts/check_docs.py
git diff --check
```

Filter with `ctest --test-dir build --output-on-failure -R NAME`:

| CTest name | Coverage |
| --- | --- |
| `preview-mailbox` | Coherent play/stop ranges under concurrent writers, latest-wins, no replay |
| `pad-workflows` | Clipboard, collapse, split cancellation/replay/conflict, revision-checked chops; exact PCM/settings and replies |
| `sampler-core` | Capture, MIDI/banks/layouts, storage/voices, region/ADSR, mixers/filter/Dirty, raw preview/repartition, resampling, clipboard/lifecycle, >1,024 events/block, literal parameter IDs |
| `buffer-timing` | Stereo playback/capture across fixed/irregular blocks, MIDI offsets/order, pre-roll ring wrap, zero-frame callbacks, resampling/effects |
| `adsr-envelope` | Exact stage sequences, early note-off, retrigger, zero-duration/live edits, sanitization, automatic-release resume |
| `sampler-transfer` | Concurrent transfers; exact monitoring/MIDI/recording continuity; retained storage, stale commits, capacity, host snapshots, dispatcher lifecycle, callback allocation/destruction |
| `state-codec` | Audio/editor/mixer compatibility, cut/waveform protocols; truncation, corrupt headers/payloads, numeric rejection, unchanged destinations |
| `ui-skin` | Bounded/versioned manifests, scalar/UTF-8/printf fallback, confined images, startup snapshots |
| `ui-skin-catalog` | Discovery, forced precedence, missing choices, atomic preferences and concurrent writes |
| `ui-skin-theme` | Placeholder completeness, RGBA/metric bounds, surface opacities, logo decoding/glow activation, nested/thread scopes |
| `ui-default-skin` | Packaged default assets in enabled formats, forced/hidden selector, embedded logo, color/metric validity |
| `ui-geometry` | Plugin identity/help URLs, pad mapping, editor snapshots, waveform/chop geometry, viewport mapping, logo halo/pulse geometry, hit testing, wheel editing, Space press/repeat routing |
| `ui-message-bus` | Bounded VST3 message ordering, independent view cursors, explicit gap reporting on wrap, and large replies |
| `wav-codec` | WAV formats, chunk/truncation/header/nonfinite validation, file actions, offline render, real-time access gate |

Rebuild affected targets and run all tests after code changes. Docs-only changes
need command/source checks. Optional long-audio test (keep WAVs out of Git):

```bash
./build/anvillesampler_transfer_tests "/path/to/long-sample.wav"
```

## LV2 bundle discovery

Inspect with `lilv-utils`:

```bash
LV2_PATH="$PWD/build/bin" \
  lv2info https://github.com/sirsipe/SMS-Plugins/SMS-AnvilleSampler
```

`SMS-AnvilleSampler/src/plugin/DistrhoPluginInfo.h` defines the URI. Discovery does not validate audio.

## LV2 host integration

Carla:

```bash
LV2_URI='https://github.com/sirsipe/SMS-Plugins/SMS-AnvilleSampler'
LV2_PATH="$PWD/build/bin" pw-jack carla-single native lv2 "$LV2_URI"
```

Omit `pw-jack` if unnecessary. Pin
[Carla main](https://github.com/falkTX/Carla/blob/main/source/backend/plugin/CarlaPluginLV2.cpp)
with LV2 control-input change-request support for MIDI bank activation;
Carla 2.5.10 lacks it.

LV2 diagnostics with jalv:

```bash
LV2_PATH="$PWD/build/bin" pw-jack jalv -s "$LV2_URI"
```

Jalv and Carla 2.5.10 load the UI and route MIDI, but cannot validate
DSP-requested `active_bank` UI updates.

Connect JACK stereo/MIDI inputs and outputs; verify audio/MIDI flow.

Applicable checks:

- Capture: ARM; first note starts capture, later notes split consecutive visible
  pads; FINALIZE keeps the open slice. UNDO discards the open/last slice;
  confirmed CLEAR empties all banks. Repeat in VST3/LV2. Check transitions/fixed mode
  when affected, and compare recorded output for timing/pre-roll changes.
- Playback: return to PLAY; verify expected notes/banks, voice limits, and stereo
  output. Hold a Gate pad beyond several Start/End turns; release and check its
  ADSR fade (zero Release stops immediately). One Shot still ends once. In Sample
  Editor cycle Play on Select → Loop on Select → Off; check pressed labels,
  sustained loops after mouse release, replacement on selection, and stopping
  on Off/Exit. Play/Stop and Space loop only in editor Loop mode.
- Skins: verify forced `skin` hides the selector; check halo/pulse,
  steady/disabled settings, logos, background/pad/panel opacity and resizing. Remove the default
  before testing collections. With named `skins` entries,
  switch Default/custom in LV2/VST3; check images/text/colors and reopening
  across formats. Test scrolling, deleted skins, config-write failure, and resizing.
- UI/editor: check drawing/resizing, pad selection, region/mixer/ADSR drag,
  wheel/reset, sound/playhead, and single-target hover. In embedded VST3, type
  a global value: Enter commits, Escape cancels. Check ARM/play/stop icons and
  bundled blue PLAY/red ARMED indicators. ARM hides the mixer; both edge faders remain
  editable in every view. Verify independent monitor/playback gain, raw preview,
  and unchanged captured PCM. Click to focus: Space plays/stops
  in PLAY/Sample Editor and starts/chops in ARM. Hold/release/tap to check repeat
  suppression. Lose focus while held, release, return, and retry. Menus, numeric
  entry, and cut-point views block Space. Verify DAW transport does not respond.
- Cut Point Editor: follow its focused [validation contract](CHOP-EDITOR.md).
- Waveforms: in Sample Editor, Adjust Cut Points, and Split Sample, continuously
  drag zoom/scroll and Ctrl/Shift-wheel. Check visible, correctly positioned peaks,
  coarse fallback/refinement, deep zoom, edges, zoom-out, pad switching, and
  Apply refresh without stale peaks.
- Pad clipboard: Copy an edited occupied pad, then alter or clear its source and
  Paste to empty and occupied targets. Confirm stereo audio and all editor settings
  match the copy-time snapshot, and that an empty clipboard disables Paste.
- State: save populated pads/settings, close, reload, and compare playback.
  Check DAW restoration for integration changes.

## UI interaction tooling

The [Dev Container](../../.devcontainer/devcontainer.json) supplies display `:1`,
noVNC (port 6080), dummy JACK, software Mesa and UI tools. `desktop-health` checks
services, JACK, OpenGL, D-Bus and FileChooser. After image/portal changes, run
`desktop-health --dialogs` to exercise Open/Save. Avoid mouse input during automation.

`bash .devcontainer/scripts/test-plugin.sh` builds VST3/LV2, runs CTest and
`lv2info`, launches LV2 in Carla, clicks ARM, and captures the plug-in window in
ignored `build/gui-test/`. It sets VNC to 1920x1080. Use `--keep-open` for further
interaction. `screenshot-window WINDOW_ID OUTPUT.png` captures one window; resolve it again
after relaunch/resize.

Inspect plug-in-only screenshots before sharing; exclude private data,
notifications, and unrelated applications.

For VST3, launch
`carla-single native vst3 "$PWD/build/bin/SMS-AnvilleSampler.vst3"` on this desktop. Import a WAV, open Sample Editor, switch pads and back, and check the
waveform and settings. Exercise Split Sample or Adjust Cut Points. Then save,
close, delete the source WAV, reload, and compare audio and editor values.

Report host, rate/buffer, results and skipped checks. CI covers CTest/`lv2info`,
not host audio/UI behavior.
