# SMS-Anville Sampler architecture

- `src/core`: `SamplerEngine` manages capture, voices, pad publication, and capacity. `SampleStoragePool` owns PCM, capture reserve, block
  maps, immutable imports, resampling, and bounded reclamation.
- `src/plugin`: `MidichopperPlugin` adapts DPF parameters, audio, MIDI, state,
  and lifecycle. `PadWorkflows` owns serialized file/clipboard/cut/split actions;
  `PadWorkflowObserver` returns transport messages and output notifications.
- `src/ui`: `MidichopperUI` coordinates host communication and input;
  `MidichopperInteraction` resolves gestures and `MidichopperView` draws.
  `ChopEditorController` owns cut/split sessions; `EditorSnapshotSession` matches
  waveform/settings replies. `WaveformDetailSession` owns zoom/detail requests,
  `PlaybackIndicator` displays host source positions; `MixerValueEntry` edits text.
- `Common-Src` provides framework-neutral DSP, codecs, audio handoff, geometry.
  `Common-UI` provides shared controls, pads, waveform editing and DPF rendering.

Skins are per-instance, outside DSP/project state. [Guide](SKINS.md).

## MIDI mapping

`midi_bank_mode` selects mappings. All Banks maps four gapless pages from the base note. Note-on updates the active bank; note-off does not. Layouts regroup slots without changing notes.
Playback note-on reports the global pad index through an alternating-range hidden
output. Views follow bank, notes, and selection.
A hidden output reports pad/preview activity.
`play_stop_request` toggles audition/Stop All at block boundaries.
`PlaybackRequest.hpp` preserves legacy integers and adds Play, Loop, looping
toggle, and Stop Audition through one atomic command. UI-local Play/Loop/Off
replaces previous auditions within the voice limit. MIDI retrigger takes ownership;
Stop Audition cannot cut its replacement. Stop All also supports host control.
All Banks limits the effective base note to 64 so every layout remains within
MIDI notes 0–127. UI notes and labels use the engine mapping. Selected Bank reuses one range,
with `active_bank` choosing its target, and retains fixed 16-slot banks.
Hold voices retain their triggering MIDI note; note-off releases matching voices
across bank, layout, base-note, and mapping changes. Retriggering replaces that ownership.

## Capture model

Arm targets the first empty visible pad (first pad when full); idle selection
overrides it. A hidden output reports the target. The first
Sequential note-on begins capture; later note-ons publish sample-accurate
boundaries and advance through Bank D. Selected Bank skips hidden slots;
All Banks uses contiguous pages. Note-off does not affect capture. Disarming or
Finalize publishes the last slice.

Pre-roll ring holds up to 100 ms. At a boundary, history starts the new
slice and is trimmed from the previous one. Fixed mode records the chosen
duration after each note-on.

## Real-time behavior

Preallocated stereo pool: 64 pads, about eight minutes. Pads share free blocks; clearing returns them to every bank.
`process()` pulls
sample-offset MIDI without a fixed event cap or allocation, locking, I/O, or
exceptions. Each pad is one voice, with a global limit of
1–16 simultaneous voices and deterministic oldest-voice stealing.
`input_monitor` values are Off=0, On=1, Auto=2. Auto follows Arm each block.
Playback linearly interpolates source-rate conversion and semitone
varispeed. Per-pad region/ADSR and mixers are non-destructive; global
volume/pan/tune combine during playback. Active voices refresh mixer,
ADSR, and cuts per block; one-shot Start stays fixed until retrigger.
Triggers latch pad direction/mode; Global Reverse turns enabled pads. Editor requests atomically mirror
Start/End, active cursor and orientation without restarting ADSR/filters. Reverse
maps playback i to raw N−1−i before effects, preserving PCM; cut/split stays raw. Hold loops Start/End through note-off and ADSR
release. Arm cuts Hold voices. Editor Loop repeats until stopped; Play auditions once.
Loops preserve fractional overshoot, seam interpolation and filters, retriggering
ADSR each pass. Pass fades cap Release to half their duration; Hold note-off
restores full Release without retriggering. Loop cut edits keep an
in-range cursor; otherwise they wrap its fractional position into the new range.
Filter and Dirty follow [Mixing](../../Docs/AI/MIXING.md).

Pad transfers commit retained immutable audio at block boundaries; unrelated
audio continues and host reads never wait. [Storage handoff](../../Docs/AI/PAD-STORAGE.md).

Collapse Gap moves descriptors/settings without copying PCM. Split and cut edits
stage affected PCM and validate generations before atomic commit. Recording,
resets, audio, and settings changes invalidate stale plans. Structure actions
stay within the active visible bank/page.

Four peak outputs; LEDs redraw at ≤30 FPS. Hard bypass cannot be metered.

## Project state

`pad_layout` and `midi_bank_mode` use remembered startup defaults; host values win.
[Initialization/format limits](../../Docs/AI/LAYOUT-PREFERENCE.md).

Pad state stores versioned, CRC-checked PCM16 as Base64; decoding validates it.
Durable restoration stages per-pad desired updates without waiting for
processing; published reads include them, and the next block commits them.

Cut/ADSR and mixer states are versioned. Import resets both; rechopping preserves
mixers on non-empty results and clears empty-pad settings. The UI retains waveform/settings snapshots until matching sequenced replies,
rejecting stale replies and retrying failures. DSP sends 128-bin min/max summaries
through LV2 DPF state or the bounded VST3 direct-access bus. Zoom/pan requests
range/sequence-checked detail from retained storage on the control worker.
Both editors reproject range-tagged cached extrema through
`waveform::visibleSummary`, filling uncovered bins from the whole-source overview
until matching detail arrives. Pad/editor changes reset zoom and cache. [Editor detail](../../Docs/AI/CHOP-EDITOR.md).

`pad_color_01..64` saves slot palette indices. Zero/unavailable indices mean no
tint; colors stay with slots through sample edits and moves.

`pad_clear_request` publishes an atomic command consumed at the next block.
RESET restores host controls, preserving layout/bank mode/skin. The control worker
resets pads at block boundaries and clears clipboard, split plans and colors.
`pad_file_request` carries an action, pad, and UTF-8 path. VST3 queues transient transfers on an instance-owned worker; LV2 executes
them through its required host worker. Busy/status states contain small messages, while a hidden
output signals completion where wrapper state callbacks cannot return status
to the UI. VST3 bus excludes PCM. DPF's VST3 initial state transfer does
not filter DSP-only state keys and may send Base64 pad PCM to a newly opened UI.

## Validation

[Build](../../Docs/AI/DEVELOPMENT.md); [test](../../Docs/AI/TESTING.md).
