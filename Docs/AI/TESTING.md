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

## jalv integration

[jalv](https://drobilla.net/software/jalv.html) runs LV2 plugins as JACK
applications. On Ubuntu/Debian the package is `jalv`. Use an existing JACK server
or PipeWire JACK compatibility setup, and a display for custom UI checks.
Inspect `jalv --help` locally; UI frontends/options depend on the installed build.

```bash
LV2_PATH="$PWD/build/bin" \
  jalv -s https://github.com/sirsipe/SMS-Plugins/SMS-Midichopper
```

`-s` requests the custom UI when supported. With PipeWire's JACK wrapper, use
`pw-jack jalv -s` in the same command if your setup requires it. Without a
display, omit `-s` for audio/MIDI checks. Use `-c symbol=value` for initial
controls; get symbols from `lv2info` or `MidichopperPlugin.cpp`, not UI labels.
If the custom UI fails, check available jalv GUI frontends and installed UI
support; a generic control window does not validate the custom UI.

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
  installed frontend's help for saving; jalv can load a saved state path in place
  of the plugin URI. Also check DAW project restoration when the change concerns
  DAW integration.

Report host/frontend, sample rate/buffer size for audio tests, steps, observed
results, and failures. If JACK, a display, or routing is unavailable, say which
integration checks remain untested. CI currently runs CTest and `lv2info`, not
jalv audio/UI tests. Do not silently substitute one for the other.
