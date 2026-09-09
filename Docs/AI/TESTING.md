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
| `sampler-core` | Capture boundaries/pre-roll, banks/layouts, storage, voices, regions/ADSR, resampling, lifecycle |
| `state-codec` | Audio and editor-state round trips, malformed/corrupt state |
| `ui-geometry` | Pad mapping, waveform geometry, hit testing and editing |

Rebuild affected targets before testing. Run all three before handing off code
changes; they are small. For docs-only changes, run the doc checks and verify
changed commands against CMake/tool help; no audio rebuild is required.
These tests do not load a real host or render the plugin UI.

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
  region/ADSR, retrigger, and check that both waveform and sound match.
- State: save populated pads and editor settings using the host's state-saving
  facility, close, reload that state, and compare playback/settings. Inspect the
  installed host's help for saving. Also check DAW project restoration when the
  change concerns DAW integration.

## UI interaction tooling

The intended development environment includes maintained command-line tools for
mouse movement, clicks, window lookup, and window-only screenshots. The planned
X11 baseline is a pinned Carla with the required LV2 features, `xdotool`,
`wmctrl`, `xwininfo` (usually `x11-utils`), and ImageMagick `import`; headless
containers will also need Xvfb and a small window manager. Keep jalv and JACK
inspection/MIDI tools available for lower-level diagnostics. Package names,
versions, and the final container setup are provisional.

At present the repository has no development-container definition and does not
install or wrap this toolchain. UI automation therefore works only when the
tools and an X11/XWayland display are already available. Until a maintained
harness exists, use installed tools directly; do not recreate missing utilities
with one-off foreign-function bindings. Resolve the exact window ID after launch,
activate it before each interaction, and use window-relative logical coordinates
from `MidichopperLayout.hpp`. Re-resolve after relaunch or resize rather than
assuming a desktop position. Native Wayland automation is not yet specified.

Capture only the plug-in window, never the full desktop. Inspect every artifact
before sharing or committing it, and exclude usernames, home paths, machine
names, unrelated applications, notifications, accounts, and other private data.
Repository screenshots should contain only intentional product UI. This workflow
is subject to change when a reproducible container and UI harness are added.

Report host/frontend, sample rate/buffer size for audio tests, steps, observed
results, and failures. If JACK, a display, or routing is unavailable, say which
integration checks remain untested. CI currently runs CTest and `lv2info`, not
interactive host/UI tests. Do not silently substitute one layer for another.
