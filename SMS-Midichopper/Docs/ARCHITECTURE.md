# SMS-Midichopper architecture

SMS-Midichopper separates the sampler from the plug-in format and UI:

- `src/core` is a standard C++20 stereo capture/playback engine with no DPF,
  LV2, window-system, or DAW dependencies.
- `src/plugin` adapts DPF parameters, audio, MIDI, and project state to the
  engine.
- `src/ui` is the resizable DGL/NanoVG interface.
- `tests` exercises slice timing, capture lifecycle, playback resampling, and
  state corruption handling without loading a plug-in host.

## Capture model

While armed in Sequential mode, the first note-on begins capture at the chosen
start pad. Each later note-on is a sample-accurate boundary: the active slice is
published and capture continues at the next pad. Note-off does not affect
capture. Disarming or Finalize publishes the last slice. The bank stops after
pad 16 rather than silently wrapping or overwriting pad 1.

The pre-roll ring holds up to 100 ms. At a boundary, that history becomes the
start of the new slice and is trimmed from the previous slice, avoiding a gap
or duplicated audio. Fixed mode instead waits for a note-on, records the chosen
duration, and waits for the next note before advancing again.

## Real-time behavior

Audio processing uses preallocated pad and pre-roll storage. `process()` takes
sample-offset MIDI events and performs no allocation, locking, file access, or
exception handling. Each pad is one voice; all 16 may play simultaneously.
Playback uses linear interpolation when a restored sample's source rate differs
from the current host rate.

Pad import/export and sample-rate reconfiguration are control-thread work.
DPF's state worker keeps large project-state operations away from the audio
callback.

## Project state

Each pad is stored independently with a magic value, version, channel count,
source sample rate, frame count, payload length, and CRC-32. Audio is
interleaved signed PCM16 and Base64 encoded for portable DPF state. Decoding has
strict size and structural checks. Empty pads use empty state values.

Long recordings make DAW project files correspondingly larger. The current
limit is 30 seconds per pad.

## Other formats

DPF is pinned as a submodule. LV2 is built by default; the same adapter and UI
can also be compiled as VST3 or CLAP:

```bash
cmake -S . -B build -G Ninja -DMIDICHOPPER_BUILD_VST3=ON -DMIDICHOPPER_BUILD_CLAP=ON
cmake --build build
```

The VST3 build is exercised during development, while Linux LV2 remains the
primary target. Platform-specific work is confined to DPF/DGL.
