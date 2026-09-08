# SMS-Midichopper architecture

Audience: AI agents. Read only for engine, adapter, state, or UI changes.
This describes implemented behavior; [VISION.md](VISION.md) describes intended
scope. File paths below are relative to `SMS-Midichopper/`.

SMS-Midichopper separates the sampler from the plug-in format and UI:

- `src/core` is a standard C++20 stereo capture/playback engine with no DPF,
  LV2, window-system, or DAW dependencies.
- `src/plugin` adapts DPF parameters, audio, MIDI, and project state to the
  engine. Start with `Parameters.hpp` for parameter indices/ranges,
  `MidichopperPlugin.cpp` for host callbacks and symbols, `StateCodec.*` for
  sample serialization, and `DistrhoPluginInfo.h` for plugin identity/ports.
- `src/ui/MidichopperUI.cpp` owns host communication and interaction state,
  while `MidichopperView.cpp` composes the product-specific drawing.
- `tests` exercises slice timing, capture lifecycle, playback resampling, and
  state corruption handling without loading a plug-in host.
- `../Common-Src` contains plug-in-independent sample-region, ADSR, waveform
  summary, state-codec, and UI geometry components intended for reuse by future
  SMS plug-ins.
- `../Common-UI` contains the shared semantic theme, host-safe DPF/NanoVG base,
  control primitives, banked-pad layouts, and waveform/envelope editor pieces.
- `../third_party/DPF` is the single repository-level DPF submodule used by all
  plug-ins.

## Capture model

While armed in Sequential mode, the first note-on begins capture at the chosen
start pad in the selected bank. Each later note-on is a sample-accurate
boundary: the active slice is published and capture continues at the next
visible pad. Capture advances from Bank A through Bank D and stops rather than
silently wrapping or overwriting earlier material. The 12- and 8-pad layouts
skip hidden storage slots at the end of each bank. Note-off does not affect
capture. Disarming or Finalize publishes the last slice.

The pre-roll ring holds up to 100 ms. At a boundary, that history becomes the
start of the new slice and is trimmed from the previous slice, avoiding a gap
or duplicated audio. Fixed mode instead waits for a note-on, records the chosen
duration, and waits for the next note before advancing again.

## Real-time behavior

Audio processing uses a shared pool of preallocated sample blocks plus pre-roll
storage. The pool supports 64 logical pad slots while keeping its nominal audio
allocation close to the earlier 16-pad design; cleared blocks can be reused by
any bank. An individual pad remains limited to 30 seconds, and the pool can hold
approximately eight minutes of 48 kHz stereo audio in total. `process()` takes
sample-offset MIDI events and performs no allocation, locking, file access, or
exception handling. Each pad is one voice, with a configurable global limit of
1–16 simultaneous voices and deterministic oldest-voice stealing.
Playback uses linear interpolation when a restored sample's source rate differs
from the current host rate. Each pad has normalized, non-destructive start/end
points and an allocation-free ADSR voice envelope. Editor values use atomics;
playback snapshots them on the next note trigger. Preserve that boundary when
changing live-edit behavior.

Pad import/export and sample-rate reconfiguration are control-thread work.
Configuration and pad import must not run concurrently with `process()`; see
`SamplerEngine.hpp`. Keep large state operations outside the audio callback and
verify the wrapper's scheduling when changing state transport.

## Project state

Each of the 64 pad slots is stored independently with a magic value, version, channel count,
source sample rate, frame count, payload length, and CRC-32. Audio is
interleaved signed PCM16 and Base64 encoded for portable DPF state. Decoding has
strict size and structural checks. Empty pads use empty state values.

Cut points and ADSR values are stored as compact versioned state per pad. The UI
never receives the full PCM state: it requests the selected pad and the DSP-side
worker returns a fixed 128-bin min/max waveform summary. This keeps waveform
drawing and editor interaction away from the audio callback and avoids sending
large sample blobs through the UI channel.

Long recordings make DAW project files correspondingly larger. The current
limit is 30 seconds per pad.

## Build and validation

See [development](../../Docs/AI/DEVELOPMENT.md) for formats and build commands,
and [testing](../../Docs/AI/TESTING.md) for CTest coverage and jalv checks.
Platform-specific work is confined to DPF/DGL.
