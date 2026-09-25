# SMS-Midichopper architecture

Audience: developers.

SMS-Midichopper separates these layers:

- `src/core` is the framework-free C++20 capture/playback engine.
- `src/plugin` adapts DPF parameters, audio, MIDI, and state; start with
  `Parameters.hpp`, `MidichopperPlugin.cpp`, and `StateCodec.*`.
- `src/ui/MidichopperUI.cpp` owns host communication;
  `MidichopperInteraction.hpp` resolves input and `MidichopperView.cpp` draws.
- `tests` covers engine behavior, state, WAV handling, and host-free UI geometry.
- `../Common-Src` contains reusable sample, DSP, state, and UI geometry code.
- `../Common-UI` contains the shared theme, DPF/NanoVG base, controls,
  context-menu and hover primitives, pad layouts, and waveform editor pieces.

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

The Cut Point Editor uses exclusive raw audition. Its three notes select proposed
slices; Split Sample maps two notes to virtual halves. Other notes stay silent.

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
ADSR, and End atomics per block; Start remains fixed until retrigger. Output
updates seed a UI-clocked playhead that briefly holds its final position.

See [Cut Point Editor](../../Docs/AI/CHOP-EDITOR.md) for boundary editing.

Pad storage mutations are control-thread work. A shared gate makes `run()`
output silence while control code copies or replaces storage at a block
boundary; its audio side only checks lock-free atomics. File access, codecs, and
offline rendering never run in the callback. See `SamplerEngine.hpp`.
Export and same-rate import use block copies, allocating before
gating. Large transfers can silence the callback; avoiding that needs another
handoff.

Collapse Gap moves shared-pool block mappings and complete pad settings without
copying PCM. Split Sample stages only the selected PCM, snapshots generations
for the affected suffix, and atomically shifts whole pads before writing the two
halves. Pad generations cover audio and settings changes so a pending split is
rejected if its plan becomes stale. Both operations are confined to the active
visible bank/page.

Four hidden outputs carry input and output peaks. Meter redraws are 30-FPS
capped and change only at LED boundaries. Host hard bypass cannot be metered.

## Project state

Each pad stores versioned, CRC-checked interleaved PCM16 audio, Base64 encoded
for DPF state. Decoding checks size and structure. Empty pads use empty values.

Cut/ADSR and mixer values use versioned per-pad states. Import resets both;
rechopping preserves mixer settings on non-empty results and clears settings
when a pad becomes empty. The UI retains its snapshot until matching waveform
and control replies arrive, ignoring stale replies and retrying failures. DSP
returns a 128-bin min/max summary through DPF state in LV2 or the bounded
direct-access bus in VST3. Ctrl-wheel zoom requests a new 128-bin summary of
the visible frame range, across up to three adjacent pads; Shift-wheel pans.
Request sequence and range reject stale detail replies. The control thread reads
sample blocks behind the real-time access gate without copying whole pads.
Zoom is UI-local and resets on pad or editor change. Waveform work stays outside
the audio callback.

`pad_clear_request` publishes an atomic command consumed at the next block.
`pad_file_request` carries an action, pad, and UTF-8 path; LV2 handles it on its
required worker. Busy/status states contain small messages, while a hidden
output signals completion where wrapper state callbacks cannot return status
to the UI. The VST3 bus carries no PCM. DPF's VST3 initial state transfer does
not filter DSP-only state keys and may send Base64 pad PCM to a newly opened UI.

## Build and validation

See [development](../../Docs/AI/DEVELOPMENT.md) and
[testing](../../Docs/AI/TESTING.md).
