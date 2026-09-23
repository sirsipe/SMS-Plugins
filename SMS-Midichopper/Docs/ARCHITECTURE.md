# SMS-Midichopper architecture

Audience: agents changing engine, adapter, state, or UI. This covers implemented
behavior; [VISION.md](VISION.md) covers intent.

SMS-Midichopper separates sampler, plug-in format, and UI:

- `src/core` is the framework-free C++20 capture/playback engine.
- `src/plugin` adapts DPF parameters, audio, MIDI, and state. Start with
  `Parameters.hpp`, `MidichopperPlugin.cpp`, `StateCodec.*`, and
  `DistrhoPluginInfo.h` respectively.
- `src/ui/MidichopperUI.cpp` owns host communication;
  `MidichopperInteraction.hpp` resolves input and `MidichopperView.cpp` draws.
- `tests` covers engine behavior, state, WAV handling, and host-free UI geometry.
- `../Common-Src` contains plug-in-independent sample-region, ADSR, waveform
  summary, state-codec, and UI geometry components intended for reuse by future
  SMS plug-ins.
- `../Common-UI` contains the shared theme, DPF/NanoVG base, controls,
  context-menu and hover primitives, pad layouts, and waveform editor pieces.
- `../third_party/DPF` is the shared framework submodule.

## MIDI mapping

The saved `midi_bank_mode` selects two mappings. All Banks maps storage from the
base note as four gapless layout-sized pages. Note-on updates the active bank;
note-off does not. Supporting hosts save this change. Layout changes regroup
slots without changing notes.
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

Arming chooses the first empty visible pad, or the first when full; idle mouse
selection overrides it. A hidden output reports the target. The first
Sequential note-on begins capture; later note-ons publish sample-accurate
boundaries and advance. Capture runs Bank A through D and stops without
wrapping. The 12- and 8-pad layouts
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
Playback uses linear interpolation for source-rate conversion and semitone
varispeed. Each pad has non-destructive region/ADSR and mixer settings; global
volume, pan, and tune combine during playback. Active voices refresh mixer,
ADSR, and End atomics per block; Start remains fixed until retrigger. Output
updates seed a UI-clocked playhead that briefly holds its final position.

The three-pad raw boundary workflow is documented in
[Cut Point Editor](../../Docs/AI/CHOP-EDITOR.md).

Pad storage mutations are control-thread work. A shared gate makes `run()`
output silence while control code copies or replaces storage at a block
boundary; its audio side only checks lock-free atomics. File access, codecs, and
offline rendering never run in the callback. See `SamplerEngine.hpp`.

Collapse Gap moves shared-pool block mappings and complete pad settings without
copying PCM. Split Sample stages only the selected PCM, snapshots generations
for the affected suffix, and atomically shifts whole pads before writing the two
halves. Pad generations cover audio and settings changes so a pending split is
rejected if its plan becomes stale. Both operations are confined to the active
visible bank/page.

Four hidden outputs carry raw-input and final-output peaks.
Meter redraws are 30-FPS capped, change only at LED boundaries, and batch
colors. Input ignores settings; output follows monitoring,
voices, envelopes, and gain. Host hard bypass that skips DSP cannot be metered.

## Project state

Each pad slot stores a versioned, CRC-checked header and interleaved signed
PCM16 audio, Base64 encoded for portable DPF state. Decoding has strict size
and structural checks. Empty pads use empty state values.

Cut/ADSR and mixer values use compact versioned per-pad states. Import resets
both; rechopping preserves mixer state on non-empty results and clears all
settings when a pad becomes zero length. The UI retains its previous snapshot
until matching waveform and control replies arrive, ignoring stale replies and
retrying transient failures. DSP returns a fixed 128-bin min/max summary,
keeping PCM blobs and waveform work outside the UI channel and audio callback.

`pad_clear_request` publishes an atomic command consumed at the next block.
`pad_file_request` carries an action, pad, and UTF-8 path; LV2 handles it on its
required worker. Busy/status states contain small messages, while a hidden
output signals completion where wrapper state callbacks cannot return status
to the UI. PCM never crosses the UI state channel.

## Build and validation

See [development](../../Docs/AI/DEVELOPMENT.md) and
[testing](../../Docs/AI/TESTING.md).
Platform-specific work is confined to DPF/DGL.
