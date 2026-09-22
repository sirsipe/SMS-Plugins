# Testing reference

Audience: AI agents. Run commands from the repository root after the
[build](DEVELOPMENT.md). Use the relevant layers below; record what actually ran.

## Automated checks

```bash
ctest --test-dir build --output-on-failure
python3 scripts/check_docs.py
git diff --check
```

For focused iteration, use `ctest --test-dir build --output-on-failure -R NAME`:

| CTest name | Coverage |
| --- | --- |
| `sampler-core` | Capture, banks/layouts, storage, voices, region/ADSR, pad/global mixer sums, raw chop preview/repartition, resampling, clipboard, lifecycle |
| `state-codec` | Audio, editor-state, and Cut Point Editor command round trips; malformed/corrupt state |
| `ui-geometry` | Pad mapping, editor snapshot assembly, waveform/chop geometry, hit testing, wheel editing |
| `wav-codec` | WAV formats, validation, file actions, offline render, real-time access gate |

Rebuild affected targets before testing. Run all four before handing off code
changes; they are small. For docs-only changes, run the doc checks and verify
changed commands against CMake/tool help; no audio rebuild is required.
## LV2 bundle discovery

Install `lilv-utils` if needed. Test the built bundle without copying it into
the user's installed plugins:

```bash
LV2_PATH="$PWD/build/bin" \
  lv2info https://github.com/sirsipe/SMS-Plugins/SMS-Midichopper
```

The URI comes from `src/plugin/DistrhoPluginInfo.h` under `SMS-Midichopper`.
This checks discovery/metadata; it does not prove audio processing works.

## LV2 host integration

Use Carla as the primary manual host because it exercises a more DAW-like UI,
state, and parameter transport. Test the built bundle rather than an older
installed copy:

```bash
LV2_URI='https://github.com/sirsipe/SMS-Plugins/SMS-Midichopper'
LV2_PATH="$PWD/build/bin" pw-jack carla-single native lv2 "$LV2_URI"
```

Omit `pw-jack` when Carla already uses the desired JACK server. Current
[Carla main](https://github.com/falkTX/Carla/blob/main/source/backend/plugin/CarlaPluginLV2.cpp)
includes the LV2 control-input change-request feature that SMS-Midichopper uses
when MIDI activates a bank, but Carla 2.5.10 does not expose it. A future
reproducible environment should pin a Carla revision or release containing that
feature and verify it rather than assuming all Carla versions support it.

Keep jalv as a secondary, minimal LV2 audio/MIDI and diagnostics host:

```bash
LV2_PATH="$PWD/build/bin" pw-jack jalv -s "$LV2_URI"
```

The currently tested jalv and Carla 2.5.10 hosts both load the custom UI and can
route MIDI, but neither provides LV2 control-input change requests. They can
confirm that MIDI changes engine behavior, but cannot validate that a
DSP-requested `active_bank` change reaches the custom UI. Do not report that
behavior as broken based on either host version alone. Inspect local host help;
frontends, features, and options vary by installed version.

Connect stereo source → plugin inputs, MIDI source → plugin event input, and
plugin outputs → a recorder or monitoring destination using your JACK graph
tool. Confirm the graph actually carries audio and MIDI before judging results.

Choose checks matching the change:

- Capture: ARM; first note starts capture, later notes split consecutive visible
  pads; FINALIZE keeps the last slice. Check bank transitions and fixed mode
  when affected, and compare recorded output for timing/pre-roll changes.
- Playback: return to PLAY; verify expected notes/banks, one-shot/gated behavior,
  voice limits, and stereo output.
- UI/editor: verify drawing/resizing and input mapping; select pads, adjust
  region/mixer/ADSR by dragging and wheel during playback, reset controls, and
  check sound/playhead. Sweep the pointer and confirm only the
  enabled target under it receives hover emphasis.
- Cut Point Editor: follow its focused [validation contract](CHOP-EDITOR.md).
- Pad clipboard: Copy an edited occupied pad, then alter or clear its source and
  Paste to empty and occupied targets. Confirm stereo audio and all editor settings
  match the copy-time snapshot, and that an empty clipboard disables Paste.
- State: save populated pads and editor settings using the host's state-saving
  facility, close, reload that state, and compare playback/settings. Inspect the
  installed host's help for saving. Also check DAW project restoration when the
  change concerns DAW integration.

## UI interaction tooling

The [Dev Container](../../.devcontainer/devcontainer.json) provides fixed X11
display `:1` through TigerVNC, Openbox, noVNC, dummy JACK, software Mesa,
`xdotool`, `wmctrl`, `xwininfo`, and ImageMagick. Desktop services start with the
container. Run `desktop-health` to check tini supervision, zombies, X11 pointer
movement, native VNC, noVNC, JACK, software OpenGL, the isolated session bus,
and the portal FileChooser interface. Run `desktop-health --dialogs` after an
image rebuild or portal change; it opens and dismisses real Open and Save dialogs
on `:1`. View the same pointer that automation controls through forwarded noVNC
port 6080; do not use the VNC mouse during an automated sequence.

From `/workspaces/SMS-Plugins`, `test-plugin` runs the release VST3/LV2 build,
CTest, `lv2info`, launches the built LV2 directly in Carla, resolves its visible
window, clicks ARM at its layout coordinate, and saves an exact-window PNG under
the ignored `build/gui-test/` directory. Use `test-plugin --keep-open` for more
interactions. `screenshot-window WINDOW_ID OUTPUT.png` refuses desktop-wide
capture. Resolve and activate the window again after every relaunch or resize.

Capture only the plug-in window, never the full desktop. Inspect every artifact
before sharing or committing it, and exclude usernames, home paths, machine
names, unrelated applications, notifications, accounts, and other private data.
Repository screenshots should contain only intentional product UI.

The image uses Ubuntu's distribution Carla rather than pinning a post-2.5.10
revision. Therefore the MIDI-triggered `active_bank` UI synchronization path is
still not validated by this harness; Carla versions through 2.5.10 lack the
needed LV2 control-input change-request feature. Report the installed Carla
version with results and do not treat that limitation as a plug-in failure.

Report host/frontend, sample rate/buffer size for audio tests, steps, observed
results, and failures. If JACK, a display, or routing is unavailable, say which
integration checks remain untested. CI currently runs CTest and `lv2info`, not
interactive host/UI tests. Do not silently substitute one layer for another.
