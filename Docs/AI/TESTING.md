# Testing reference

[Build/skin tests](DEVELOPMENT.md); [packaging tests](../RELEASING.md#licenses-and-corresponding-source).

## Checks

```bash
ctest --test-dir build --output-on-failure
python3 scripts/check_docs.py
git diff --check
```

`ctest --test-dir build -R NAME` filters tests:

| CTest name | Coverage |
| --- | --- |
| `preview-mailbox` | Concurrent coherent play/stop ranges, latest-wins, no replay |
| `pad-workflows` | Reset, clipboard, collapse, split cancel/replay/conflict, revision-checked chops; exact PCM/settings/replies |
| `sampler-core` | Capture, MIDI/banks/layouts, storage/voices, region/ADSR, mixer/filter/Dirty, preview/repartition, resampling, clipboard/lifecycle, >1,024 events/block, parameter IDs |
| `buffer-timing` | Stereo playback/capture, fixed/irregular blocks, MIDI offsets/order, pre-roll wrap, zero-frame callbacks, resampling/effects |
| `adsr-envelope` | Stages, early note-off, retrigger, zero-duration/live edits, sanitization, release resume |
| `sampler-transfer` | Concurrent transfers; exact monitoring/MIDI/recording continuity; retained storage, stale commits, capacity, host snapshots, dispatcher lifecycle, callback allocation/destruction |
| `global-value-mode-*` | Modes/remapping, codec/grid/zero snap, raw preservation, private state/startup defaults, CLEAR PADS preservation, automation/mailbox |
| `defaults-file` | Bounded flat INI, quoted Unicode/whitespace, atomic merged writes, unknown keys, concurrency and failures |
| `state-codec` | Audio/editor/mixer compatibility, cut/waveform protocols, truncation/corruption, numeric rejection, unchanged destinations |
| `ui-skin` | Manifest bounds/versions, scalar/UTF-8/printf fallback, Unicode paths, confined images, startup snapshots |
| `ui-skin-catalog` | Discovery/forced precedence, missing choices, config folders, Unicode atomic preferences/concurrent writes |
| `layout-*` | [Preferences, startup/host precedence, dummy metadata](LAYOUT-PREFERENCE.md) |
| `audio-ports-*` | Stereo groups/symbols; VST3 sidechain |
| `ui-skin-theme` | Placeholder completeness, RGBA/metric bounds, envelope RGBA/hover, opacity, logo decode/glow, nested/thread scopes |
| `ui-bundled-skins` | Default/bundled choices and persistence, embedded logo, bundled/additional assets/colors |
| `ui-geometry` | Metadata/ports, identity/help URLs, pad mapping, editor snapshots, waveform/chop/viewport geometry, logo halo/pulse, hit testing, wheel editing, menu/range restore, numeric validation/Tab/keypad Enter, Space routing |
| `ui-browser-launcher` | Windows dispatch/UTF-8/errors (fake API); Linux literal URLs/missing launchers |
| `ui-message-bus` | Bounded VST3 ordering, independent view cursors, wrap gap reporting, large replies |
| `wav-codec` | WAV formats, chunk/truncation/header/nonfinite validation, file actions, offline render, real-time gate |

Long-audio: `./build/anvillesampler_transfer_tests "/path/to/long-sample.wav"`.

## LV2 bundle discovery

`lilv-utils` discovery (not audio validation):

```bash
LV2_PATH="$PWD/build/bin" \
  lv2info https://github.com/sirsipe/SMS-Plugins/SMS-AnvilleSampler
```

## LV2 host integration

```bash
LV2_URI='https://github.com/sirsipe/SMS-Plugins/SMS-AnvilleSampler'
LV2_PATH="$PWD/build/bin" pw-jack carla-single native lv2 "$LV2_URI"
```

[Carla main](https://github.com/falkTX/Carla/blob/main/source/backend/plugin/CarlaPluginLV2.cpp) supports control-input change requests for MIDI bank activation; Carla 2.5.10 lacks it.

jalv diagnostics:

```bash
LV2_PATH="$PWD/build/bin" pw-jack jalv -s "$LV2_URI"
```

Jalv/Carla 2.5.10 cannot validate DSP-requested `active_bank` updates.

- Capture (VST3/LV2): ARM; first note starts capture, later notes split visible
  pads; FINALIZE keeps the open slice. UNDO discards the open/last slice;
  confirmed CLEAR empties all banks. Compare output timing/pre-roll.
- Playback: verify notes/banks, polyphony and stereo. Hold/editor Loop repeat
  ADSR each pass; Hold note-off uses full Release across seams without retrigger,
  including after shared-bank switches. Check zero Release and One Shot.
  Toggle local/global Reverse during overlapping One Shot/Hold voices; verify
  immediate turns, mirrored cuts/cursor, preserved envelopes/filters/tails,
  arrows and restoration. Cut/split stays raw. Cycle editor audition modes and
  check replacement, Stop/Space and Exit. Bypass Global Mixer with effects
  active; verify immediate direction/cursor changes, independent local effects,
  OUT, reenable, persistence and copy/paste.
- Skins: check Default/three bundled skins in LV2/VST3: artwork, halo/pulse,
  opacity, resizing, cross-format persistence, custom logos, disabled halos,
  scrolling, deleted skins and config-write failure. Forced `skin` hides selection.
  Verify SKIN's separator/pinned `<Get More>` with long collections; it opens the
  additional-skins README without changing appearance/preferences.
- UI/editor: check drawing, selection, region/mixer/ADSR drag, wheel/reset,
  sound/playhead, hover. VST3 numeric entry: double-click knob/fader/value;
  Enter/numpad Enter commit, Escape cancels; Tab/Shift+Tab accept valid entries
  in manual order, retaining invalid entries. Middle-click resets. Check ARM/play/stop icons and
  default green PLAY/red ARMED indicators. ARM hides the mixer; both edge faders remain
  editable in every view. Verify independent monitor/playback gain, raw preview,
  and unchanged captured PCM. Check global Settings headers, mode changes keeping
  menus open/current selection closing, range validation, zero-excluding fallback,
  ±5% Zero Snap, Ctrl+wheel grids after remapping, original-bound labels,
  middle-click bounds, confirmed range RESET, private state/defaults and raw
  preservation. IN and pads remain unaffected. Click to focus: Space plays/stops
  in PLAY/Sample Editor and starts/chops in ARM. Hold/release/tap to check repeat
  suppression. Lose focus while held, release, return, and retry. Menus, numeric
  entry, and cut-point views block Space. Verify DAW transport does not respond.
- Cut Point Editor: follow its focused [validation contract](CHOP-EDITOR.md).
- Waveforms (Sample Editor, Adjust Cut Points, Split Sample): drag zoom/scroll,
  Ctrl/Shift-wheel. Check peak alignment, coarse fallback/refinement, deep zoom,
  edges, zoom-out, pad switching, cut-save/split-Apply refresh without stale peaks.
- Pad clipboard: copy edited audio; alter/clear its source, then paste to
  empty/occupied targets. Verify copy-time PCM/settings and empty-clipboard disable.
- Collapse Gap: A trailing gap, B occupied run. Earlier gap slot: disabled.
  Last slot: verify count, confirmation, moved audio/settings and vacated slots.
  Repeat Play/Sample Editor, both bank modes. Preserve hidden slots/later runs;
  D never wraps to A.
- State: save populated pads/settings, close/reload the DAW, compare playback.
- CLEAR PADS: change globals, knob settings, pad edits/colors, clipboard and
  preferences. Verify timeout/dismissal cancels confirmation. Confirm within two
  seconds: pad audio/edits/colors/clipboard clear; all globals, knob settings,
  audition mode and preferences remain. Save/reopen and compare. Check danger
  hover/confirmation colors on CLEAR PAD, CLEAR PADS and range RESET.
- Layout/bank-mode: follow the [preference acceptance checks](LAYOUT-PREFERENCE.md).

## UI tooling

[Dev Container tools](DEVELOPMENT.md#docker-development-environment): run `desktop-health`, adding `--dialogs` after portal changes.
Avoid concurrent mouse input.

`bash .devcontainer/scripts/test-plugin.sh`: build VST3/LV2, CTest/`lv2info`, Carla LV2,
ARM click, captures in ignored `build/gui-test/`. `--keep-open` enables interaction.
`screenshot-window WINDOW_ID OUTPUT.png`: resolve IDs after relaunch/resize;
inspect captures for private data.

VST3: `carla-single native vst3 "$PWD/build/bin/SMS-AnvilleSampler.vst3"`. Import WAV; check pads, waveform/settings, Split Sample or Adjust Cut Points. Save/reload after deleting the WAV; compare audio/editor values.

Report host/rate/buffer/results/skips.
