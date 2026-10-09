# Testing reference

Run from the repository root after the [build](DEVELOPMENT.md); report checks.

## Automated checks

```bash
ctest --test-dir build --output-on-failure
python3 scripts/check_docs.py
git diff --check
```

Filter with `ctest --test-dir build --output-on-failure -R NAME`:

| CTest name | Coverage |
| --- | --- |
| `sampler-core` | Capture, banks/layouts, storage, voices, region/ADSR, pad/global mixer sums, filter response and dirty conversion, raw chop preview/repartition, resampling, clipboard, lifecycle, and more than 1,024 MIDI events per block |
| `sampler-transfer` | Concurrent transfers, exact monitoring/MIDI/recording continuity, retained storage, stale commits, capacity, published host snapshots, dispatcher lifecycle, and audio-thread allocation/destruction |
| `state-codec` | Audio, editor-state, MX1/MX2 mixer compatibility, cut commands, and visible waveform request/reply round trips; malformed/corrupt state |
| `ui-geometry` | Pad mapping, editor snapshots, waveform/chop geometry, viewport mapping, hit testing, wheel editing, Space press/repeat routing |
| `ui-message-bus` | Bounded VST3 message ordering, independent view cursors, explicit gap reporting on wrap, and large replies |
| `wav-codec` | WAV formats, validation, file actions, offline render, real-time access gate |

Rebuild affected targets before testing. Run all six before handing off code
changes. For docs-only changes, run the doc checks and verify
changed commands against CMake/tool help; no audio rebuild is required.
Test long audio with an optional WAV argument; keep supplied audio out of Git:

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

The URI comes from `src/plugin/DistrhoPluginInfo.h` under `SMS-AnvilSampler`.
Discovery/metadata does not validate audio.

## LV2 host integration

Use Carla as the primary manual host because it exercises a more DAW-like UI,
state, and parameter transport. Test the built bundle rather than an older
installed copy:

```bash
LV2_URI='https://github.com/sirsipe/SMS-Plugins/SMS-AnvilSampler'
LV2_PATH="$PWD/build/bin" pw-jack carla-single native lv2 "$LV2_URI"
```

Omit `pw-jack` when Carla already uses the desired JACK server. Current
[Carla main](https://github.com/falkTX/Carla/blob/main/source/backend/plugin/CarlaPluginLV2.cpp)
includes the LV2 control-input change-request feature that SMS-Anvil Sampler uses
when MIDI activates a bank, but Carla 2.5.10 does not expose it. A future
reproducible environment should pin a Carla revision or release containing that
feature and verify it rather than assuming all Carla versions support it.

Keep jalv as a secondary, minimal LV2 audio/MIDI and diagnostics host:

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
  pads; FINALIZE keeps the last slice. Check bank transitions and fixed mode
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

The [Dev Container](../../.devcontainer/devcontainer.json) provides X11 display
`:1`, TigerVNC, Openbox, noVNC, dummy JACK, software Mesa, `xdotool`, `wmctrl`,
`xwininfo`, and ImageMagick. Run `desktop-health` to check desktop services,
JACK, OpenGL, D-Bus, and portal FileChooser. Run `desktop-health --dialogs` after
an image rebuild or portal change to exercise Open and Save dialogs. Watch
automation through noVNC port 6080; avoid VNC mouse input during a sequence.

`bash .devcontainer/scripts/test-plugin.sh` builds VST3/LV2, runs CTest and
`lv2info`, launches LV2 in Carla, clicks ARM, and captures the plug-in window in
ignored `build/gui-test/`. It sets VNC to 1920x1080. Use `--keep-open` for further
interaction. `screenshot-window WINDOW_ID OUTPUT.png` refuses desktop capture;
resolve the window after relaunch/resize.

Capture only the plug-in window, never the full desktop. Inspect every artifact
before sharing or committing it, and exclude usernames, home paths, machine
names, unrelated applications, notifications, accounts, and other private data.
Repository screenshots should contain only intentional product UI.

For the VST3 UI, launch
`carla-single native vst3 "$PWD/build/bin/SMS-AnvilSampler.vst3"` on the same
desktop. Import a WAV, open Sample Editor, switch pads and back, and check the
waveform and settings. Exercise Split Sample or Adjust Cut Points. Then save,
close, delete the source WAV, reload, and compare audio and editor values.

Report host/frontend, sample rate/buffer size for audio tests, steps, observed
results, and failures. If JACK, a display, or routing is unavailable, say which
integration checks remain untested. CI currently runs CTest and `lv2info`, not
interactive host/UI tests. Do not silently substitute one layer for another.
