# Testing reference

Run from the repository root after [building](DEVELOPMENT.md).

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
| `ui-geometry` | Pad mapping, editor snapshots, waveform/chop geometry, viewport mapping, hit testing, wheel editing, Space press/repeat routing |
| `ui-message-bus` | Bounded VST3 message ordering, independent view cursors, explicit gap reporting on wrap, and large replies |
| `wav-codec` | WAV formats, chunk/truncation/header/nonfinite validation, file actions, offline render, real-time access gate |

Rebuild affected targets; run the full suite for code changes. For docs-only
changes, check docs and commands against CMake/tool help; no rebuild is required.
Test long audio with an optional WAV; keep audio out of Git:

```bash
./build/anvilsampler_transfer_tests "/path/to/long-sample.wav"
```

## LV2 bundle discovery

Install `lilv-utils` if needed. Test the built bundle without copying it into
the user's installed plugins:

```bash
LV2_PATH="$PWD/build/bin" \
  lv2info https://github.com/sirsipe/SMS-Plugins/SMS-AnvilSampler
```

`SMS-AnvilSampler/src/plugin/DistrhoPluginInfo.h` defines the URI. Discovery
does not validate audio.

## LV2 host integration

Use Carla as the primary manual LV2 host for UI, state, and parameter checks.
Test the built bundle:

```bash
LV2_URI='https://github.com/sirsipe/SMS-Plugins/SMS-AnvilSampler'
LV2_PATH="$PWD/build/bin" pw-jack carla-single native lv2 "$LV2_URI"
```

Omit `pw-jack` when Carla already uses the desired JACK server. Current
[Carla main](https://github.com/falkTX/Carla/blob/main/source/backend/plugin/CarlaPluginLV2.cpp)
includes the LV2 control-input change-request feature that SMS-Anvil Sampler uses
when MIDI activates a bank, but Carla 2.5.10 does not expose it. A future
reproducible environment should pin a Carla revision or release containing that
feature and verify support.

Use jalv for minimal LV2 audio/MIDI and diagnostics:

```bash
LV2_PATH="$PWD/build/bin" pw-jack jalv -s "$LV2_URI"
```

Jalv and Carla 2.5.10 load the UI and route MIDI, but cannot validate
DSP-requested `active_bank` UI updates. Inspect host help.

Connect stereo source → plugin inputs, MIDI source → plugin event input, and
plugin outputs → a recorder or monitoring destination using your JACK graph
tool. Confirm the graph actually carries audio and MIDI before judging results.

Applicable checks:

- Capture: ARM; first note starts capture, later notes split consecutive visible
  pads; FINALIZE keeps the open slice. UNDO discards the open/last slice;
  confirmed CLEAR empties all banks. Repeat in VST3/LV2. Check transitions/fixed mode
  when affected, and compare recorded output for timing/pre-roll changes.
- Playback: return to PLAY; verify expected notes/banks, one-shot/gated behavior,
  voice limits, and stereo output.
- UI/editor: check drawing/resizing, pad selection, region/mixer/ADSR drag,
  wheel/reset, sound/playhead, and single-target hover. In embedded VST3, type
  a global value: Enter commits, Escape cancels. Check ARM/play/stop icons and
  green PLAY/red ARMED indicators. ARM hides the mixer; both edge faders remain
  editable in every view. Verify independent monitor/playback gain, raw preview,
  and unchanged captured PCM. Click to focus: Space plays/stops
  in PLAY/Sample Editor and starts/chops in ARM. Hold/release/tap to check repeat
  suppression. Lose focus while held, release, return, and retry. Menus, numeric
  entry, and cut-point views block Space. Verify DAW transport does not respond.
- Cut Point Editor: follow its focused [validation contract](CHOP-EDITOR.md).
- Pad clipboard: Copy an edited occupied pad, then alter or clear its source and
  Paste to empty and occupied targets. Confirm stereo audio and all editor settings
  match the copy-time snapshot, and that an empty clipboard disables Paste.
- State: save populated pads/settings, close, reload, and compare playback.
  Inspect host help for saving; check DAW restoration for integration changes.

## UI interaction tooling

The [Dev Container](../../.devcontainer/devcontainer.json) provides display `:1`,
noVNC, dummy JACK, software Mesa and UI inspection tools. Run `desktop-health` to check desktop services,
JACK, OpenGL, D-Bus, and portal FileChooser. Run `desktop-health --dialogs` after
an image rebuild or portal change to exercise Open and Save dialogs. Watch
automation through noVNC port 6080; avoid VNC mouse input during a sequence.

`bash .devcontainer/scripts/test-plugin.sh` builds VST3/LV2, runs CTest and
`lv2info`, launches LV2 in Carla, clicks ARM, and captures the plug-in window in
ignored `build/gui-test/`. It sets VNC to 1920x1080. Use `--keep-open` for further
interaction. `screenshot-window WINDOW_ID OUTPUT.png` captures one window; resolve it again
after relaunch/resize.

Capture only the plug-in window, never the full desktop. Inspect every artifact
before sharing or committing it, and exclude usernames, home paths, machine
names, unrelated applications, notifications, accounts, and other private data.
Repository screenshots should contain only intentional product UI.

For the VST3 UI, launch
`carla-single native vst3 "$PWD/build/bin/SMS-AnvilSampler.vst3"` on the same
desktop. Import a WAV, open Sample Editor, switch pads and back, and check the
waveform and settings. Exercise Split Sample or Adjust Cut Points. Then save,
close, delete the source WAV, reload, and compare audio and editor values.

Report host/frontend, audio rate/buffer, steps, results, failures and unavailable
integration checks. CI runs CTest and `lv2info`; manual host/UI evidence remains
necessary. A launch or metadata check alone does not validate audio/UI behavior.
