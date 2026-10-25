# Testing reference

[Build and skin tests](DEVELOPMENT.md).

## Checks

```bash
ctest --test-dir build --output-on-failure
python3 scripts/check_docs.py
git diff --check
```

Filter: `ctest --test-dir build --output-on-failure -R NAME`:

| CTest name | Coverage |
| --- | --- |
| `preview-mailbox` | Concurrent coherent play/stop ranges, latest-wins, no replay |
| `pad-workflows` | Reset, clipboard, collapse, split cancel/replay/conflict, revision-checked chops; exact PCM/settings/replies |
| `sampler-core` | Capture, MIDI/banks/layouts, storage/voices, region/ADSR, mixer/filter/Dirty, preview/repartition, resampling, clipboard/lifecycle, >1,024 events/block, parameter IDs |
| `buffer-timing` | Stereo playback/capture, fixed/irregular blocks, MIDI offsets/order, pre-roll wrap, zero-frame callbacks, resampling/effects |
| `adsr-envelope` | Stage sequences, early note-off, retrigger, zero-duration/live edits, sanitization, release resume |
| `sampler-transfer` | Concurrent transfers; exact monitoring/MIDI/recording continuity; retained storage, stale commits, capacity, host snapshots, dispatcher lifecycle, callback allocation/destruction |
| `state-codec` | Audio/editor/mixer compatibility, cut/waveform protocols; truncation, corruption, numeric rejection, unchanged destinations |
| `ui-skin` | Bounded/versioned manifests, scalar/UTF-8/printf fallback, Unicode paths, confined images, startup snapshots |
| `ui-skin-catalog` | Discovery, forced precedence, missing choices, config folders, Unicode atomic preferences/concurrent writes |
| `layout-*` | [Preferences, startup/host precedence, dummy metadata](LAYOUT-PREFERENCE.md) |
| `ui-skin-theme` | Placeholder completeness, RGBA/metric bounds, envelope RGBA/hover, opacity, logo decode/glow, nested/thread scopes |
| `ui-bundled-skins` | Three bundled skins, Default, selectable/remembered choices, embedded logo, asset/color validity |
| `ui-geometry` | Metadata/ports, identity/help URLs, pad mapping, editor snapshots, waveform/chop/viewport geometry, logo halo/pulse, hit testing, wheel editing, Space routing |
| `ui-message-bus` | Bounded VST3 ordering, independent view cursors, wrap gap reporting, large replies |
| `wav-codec` | WAV formats, chunk/truncation/header/nonfinite validation, file actions, offline render, real-time gate |

Rebuild; run all tests. Long audio:

```bash
./build/anvillesampler_transfer_tests "/path/to/long-sample.wav"
```


## LV2 bundle discovery

`lilv-utils` discovery:

```bash
LV2_PATH="$PWD/build/bin" \
  lv2info https://github.com/sirsipe/SMS-Plugins/SMS-AnvilleSampler
```

Discovery does not validate audio.

## LV2 host integration

Carla:

```bash
LV2_URI='https://github.com/sirsipe/SMS-Plugins/SMS-AnvilleSampler'
LV2_PATH="$PWD/build/bin" pw-jack carla-single native lv2 "$LV2_URI"
```

Use [Carla main](https://github.com/falkTX/Carla/blob/main/source/backend/plugin/CarlaPluginLV2.cpp)
with LV2 control-input change-request support for MIDI bank activation;
Carla 2.5.10 lacks it.

LV2 diagnostics with jalv:

```bash
LV2_PATH="$PWD/build/bin" pw-jack jalv -s "$LV2_URI"
```

Jalv/Carla 2.5.10 cannot validate DSP-requested `active_bank` updates.

Checks:

- Capture: ARM; first note starts capture, later notes split consecutive visible
  pads; FINALIZE keeps the open slice. UNDO discards the open/last slice;
  confirmed CLEAR empties all banks. Repeat in VST3/LV2; check affected capture
  modes and compare recorded output for timing/pre-roll changes.
- Playback: verify notes/banks, voice limits and stereo. Hold and editor loops
  repeat ADSR each pass; edit all stages live. Hold note-off uses full Release
  across seams without retriggering; zero Release stops immediately. In shared
  MIDI bank mode, hold a pad, switch banks, then release: the original pad must
  stop after Release while other held notes continue. One Shot
  ends once. Toggle pad/global Reverse during overlapping voices in both modes;
  check immediate turns, preserved ADSR/filters/tails, mirrored cuts/cursor,
  pad arrows and restoration. Cut/split stays raw. Cycle editor Play on Select →
  Loop on Select → Off; check labels, live cuts, selection replacement and
  stopping on Off/Exit. Editor Play/Stop and Space follow Loop mode. Bypass
  Global Mixer with global pan/tune/Dirty/filters/Reverse active; check immediate
  bypass, orientation and active local effects/OUT. Verify reenable, independent
  pad flags, save/reload and copy/paste.
- Skins: without preferences, verify Default. Switch
  Default/Finnish Dream/Looks like Wu/White Cadillac Vinyl in LV2/VST3; check
  artwork, halo/pulse, opacity, resizing and remembered selection
  across formats. In installed copies, verify forced `skin` hides the
  selector; check steady/disabled halos and custom logos. Test scrolling, deleted
  skins, config-write failure, and resizing.
- UI/editor: check drawing, pad selection, region/mixer/ADSR drag,
  wheel/reset, sound/playhead, and single-target hover. In embedded VST3, type
  a global value: Enter commits, Escape cancels. Check ARM/play/stop icons and
  default green PLAY/red ARMED indicators. ARM hides the mixer; both edge faders remain
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
  Check DAW restoration.
- Reset: change controls, pad edits/colors, clipboard, layout/bank mode/skin. Verify
  timeout and menu dismissal cancel confirmation. Confirm within two seconds:
  audio/settings/colors/clipboard clear; controls/editor options default; layout/bank
  mode/skin remain. Save/reopen and verify.
- Layout/bank-mode: follow the [preference acceptance checks](LAYOUT-PREFERENCE.md).

## UI tooling

The [Dev Container](../../.devcontainer/devcontainer.json) supplies display `:1`,
noVNC (6080), dummy JACK, software Mesa and UI tools. Run `desktop-health`;
after image/portal changes, use `desktop-health --dialogs` to exercise Open/Save.
Avoid concurrent mouse input.

`bash .devcontainer/scripts/test-plugin.sh` builds VST3/LV2, runs CTest/`lv2info`,
launches Carla LV2, clicks ARM, and captures to ignored `build/gui-test/`.
`--keep-open` enables interaction.
`screenshot-window WINDOW_ID OUTPUT.png` captures one window; resolve IDs after relaunch/resize.

Inspect screenshots; exclude private data/unrelated windows.

VST3:
`carla-single native vst3 "$PWD/build/bin/SMS-AnvilleSampler.vst3"` on this desktop. Import a WAV; open Sample Editor and switch pads; check waveform/settings. Exercise Split Sample or Adjust Cut Points. Then save,
close, delete the source WAV, reload, and compare audio and editor values.

Report host/rate/buffer/results/skips. CI: CTest/`lv2info`.
