# SMS-Midichopper architecture

Audience: agents changing engine, adapter, state, or UI. This describes
implemented behavior; [VISION.md](VISION.md) describes intended scope. Paths
below are relative to `SMS-Midichopper/`.

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
- `../Common-UI` contains the shared theme, DPF/NanoVG base, controls,
  context-menu and hover primitives, pad layouts, and waveform editor pieces.
- `../third_party/DPF` is the single repository-level DPF submodule used by all
  plug-ins.

## MIDI mapping

The saved `midi_bank_mode` selects two mappings. All Banks maps exposed storage
slots from the base note as a gapless sequence, grouped into four layout-sized
pages. A valid note-on updates the active bank; note-off does not. The DSP asks
supporting hosts to save this `active_bank` change. Layout changes regroup slots
without changing their MIDI notes.
Every successful playback note-on also advances a hidden output event that
encodes the global pad index in alternating halves of its range. Both UI views
follow that event, so bank, MIDI labels, and the single last-played selection
stay current across retriggers even without host input-parameter changes.
All Banks limits the effective base note to 64 so every layout remains within
MIDI notes 0–127. UI-generated note-on and note-off events and displayed note
labels use the same mapping as the engine. Selected Bank reuses one note range,
with `active_bank` choosing its target, and retains the released fixed 16-slot
bank organization.

## Capture model

Arming chooses the first empty visible pad in the selected bank, or its first
pad when full. An idle mouse selection overrides that target. A hidden output
reports the engine's actual target to the UI. The first Sequential note-on
begins capture there. Each later note-on is a sample-accurate
boundary: the active slice is published and capture continues at the next
visible pad. Capture advances from Bank A through Bank D and stops rather than
silently wrapping or overwriting earlier material. The 12- and 8-pad layouts
skip hidden storage slots at the end of each bank in Selected Bank mode. All
Banks instead advances through contiguous layout-sized pages. Note-off does not
affect capture. Active recording ignores manual retargeting. Disarming or
Finalize publishes the last slice.

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

Four hidden outputs carry allocation-free raw-input and final-output peaks.
Meter redraws are 30-FPS capped, change only at LED boundaries, and batch
colors. Input ignores settings; output follows monitoring,
voices, envelopes, and gain. Host hard bypass that skips DSP cannot be metered.

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

The `pad_clear_request` UI state publishes only an atomic command and reads back
as neutral `0`; the audio callback clears that pad at the next block boundary.

Long recordings make DAW project files correspondingly larger. The current
limit is 30 seconds per pad.

## Build and validation

See [development](../../Docs/AI/DEVELOPMENT.md) and
[testing](../../Docs/AI/TESTING.md).
Platform-specific work is confined to DPF/DGL.
