# SMS-Anvil Sampler architecture

Layers:

- `src/core` is the C++20 capture/playback engine.
- `src/plugin` adapts DPF parameters, audio, MIDI, and state.
- `src/ui/MidichopperUI.cpp` owns host communication;
  `MidichopperInteraction.hpp` resolves input, `ChopEditorSession.hpp` collects
  coherent cut baselines, `EditorSnapshotSession.hpp` matches sample snapshots,
  and `MidichopperView.cpp` draws.
- `tests` covers engine, state, WAV, and UI.
- `../Common-Src` contains reusable DSP, state, and UI geometry.
- `../Common-UI` contains shared theme, controls, pads, and waveform editing.

## MIDI mapping

Saved `midi_bank_mode` selects two mappings. All Banks maps four gapless pages
from the base note. Note-on updates the active bank; note-off does not. Layout
changes regroup slots without changing notes.
Every successful playback note-on also advances a hidden output event that
encodes the global pad index in alternating halves of its range. Both UI views
follow it to keep bank, MIDI labels, and last-played selection current.
An appended hidden output reports whether any pad or raw preview is active.
The UI sends `play_stop_request` through state transport; the DSP decides from
its voice state whether to audition the selected pad or hard-cut every voice at
the next audio block. An appended Stop All trigger also supports host control.
All Banks limits the effective base note to 64 so every layout remains within
MIDI notes 0–127. UI notes and labels use the engine mapping. Selected Bank reuses one range,
with `active_bank` choosing its target, and retains the released fixed 16-slot
bank organization.

## Capture model

Arming chooses the first empty visible pad, or the first when full; idle
selection overrides it. A hidden output reports the target. The first
Sequential note-on begins capture; later note-ons publish sample-accurate
boundaries and advance through Bank D. Selected Bank skips hidden slots;
All Banks uses contiguous pages. Note-off does not affect capture. Disarming or
Finalize publishes the last slice.

The pre-roll ring holds up to 100 ms. At a boundary, history starts the new
slice and is trimmed from the previous one. Fixed mode records the chosen
duration after each note-on.

## Real-time behavior

Audio processing uses preallocated sample blocks plus pre-roll storage. The pool
supports 64 pad slots; cleared blocks can be reused by any bank. A pad can use
the entire free pool, so a long song can be split across pads. The pool holds
about eight minutes of stereo audio at the host rate. `process()` pulls
sample-offset MIDI without a fixed event cap or allocation, locking, I/O, or
exceptions. Each pad is one voice, with a global limit of
1–16 simultaneous voices and deterministic oldest-voice stealing.
Saved `input_monitor` values are Off=0, On=1, Auto=2. The adapter resolves Auto
against Arm before passing the engine's boolean monitor gate each block.
Playback uses linear interpolation for source-rate conversion and semitone
varispeed. Each pad has non-destructive region/ADSR and mixer settings; global
volume, pan, and tune combine during playback. Active voices refresh mixer,
ADSR, and End atomics per block; Start remains fixed until retrigger.
The filter and Dirty processing contract is in [Mixing](../../Docs/AI/MIXING.md).

Pad transfers retain immutable completed audio, prepare replacements off-thread,
and commit bounded descriptor changes at audio block boundaries. Monitoring and
unrelated voices continue during transfers. Host state reads use coherent
published revisions without waiting for processing. See the
[storage handoff contract](../../Docs/AI/PAD-STORAGE.md) for ownership, reserve
memory, conflicts, lifecycle, and regression coverage.

Collapse Gap moves descriptors and complete pad settings without copying PCM.
Split and ordinary cut edits stage only affected PCM, then validate generations
before an atomic transaction. Recording start, completion, resets, audio, and
settings changes invalidate stale plans. Both structure actions stay within the
active visible bank/page.

Four outputs report input/output peaks. Redraws cap at 30 FPS and follow LED
boundaries. Host hard bypass cannot be metered.

## Project state

Each pad stores versioned, CRC-checked interleaved PCM16 audio, Base64 encoded
for DPF state. Decoding checks size and structure. Empty pads use empty values.
Durable restoration stages per-pad desired updates without waiting for
processing; published reads include them, and the next block commits them.

Cut/ADSR and mixer values use versioned per-pad states. Import resets both;
rechopping preserves mixer settings on non-empty results and clears settings
when a pad becomes empty. The UI retains its snapshot until a matching sequenced reply carries waveform
and settings together, ignoring stale same-pad replies and retrying failures. DSP
returns a 128-bin min/max summary through DPF state in LV2 or the bounded
direct-access bus in VST3. Ctrl-wheel zoom requests a new 128-bin summary of
the visible frame range, across up to three adjacent pads; Shift-wheel pans.
Request sequence and range reject stale detail replies. The control worker scans retained immutable sample storage outside the callback.
Zoom is UI-local and resets on pad or editor change. Waveform work stays outside
the audio callback.

Each pad slot also saves a `pad_color_01..64` theme palette index. Zero removes
the tint. The UI treats an unavailable index as no color; colors stay with pad
slots through sample edits and moves.

`pad_clear_request` publishes an atomic command consumed at the next block.
`pad_file_request` carries an action, pad, and UTF-8 path. VST3 queues transient transfers on an instance-owned worker; LV2 executes
them through its required host worker. Busy/status states contain small messages, while a hidden
output signals completion where wrapper state callbacks cannot return status
to the UI. The VST3 bus carries no PCM. DPF's VST3 initial state transfer does
not filter DSP-only state keys and may send Base64 pad PCM to a newly opened UI.

## Build and validation

See [development](../../Docs/AI/DEVELOPMENT.md) and
[testing](../../Docs/AI/TESTING.md).
