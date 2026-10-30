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
  `PlaybackIndicator` displays host source positions; `MixerValueEntry` edits text,
  `MixerValueNavigation` traverses entries, and `GlobalControlMenu` manages modes/ranges.
- `Common-Src` provides framework-neutral DSP, codecs, audio handoff, geometry,
  and bounded atomic-merge defaults through `IO/DefaultsFile.*`.
  `Common-UI` provides shared controls, pads, waveform editing and DPF rendering.

Skins are per-instance, outside DSP/project state. [Guide](SKINS.md).

## MIDI mapping

`midi_bank_mode` selects mappings. All Banks maps four gapless pages from the
base note; note-on updates the active bank. Layouts regroup slots without changing
notes. Alternating-range hidden output reports playback pad indices; another
reports pad/preview activity. Views follow bank, notes and selection.
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

Preallocated stereo storage spans 64 pads/about eight minutes; clearing returns
shared blocks. `process()` consumes sample-offset MIDI without event caps,
allocation, locks, I/O or exceptions. Each pad is one voice; a 1–16 global limit
steals oldest voices.
`input_monitor` values are Off=0, On=1, Auto=2. Auto follows Arm each block.
Playback linearly interpolates source-rate conversion and semitone
varispeed. Per-pad region/ADSR and mixers are non-destructive; global
volume/pan/tune combine during playback. Active voices refresh mixer,
ADSR, and cuts per block; one-shot Start stays fixed until retrigger.
Triggers latch pad direction/mode; Global Reverse turns enabled pads. Editor requests atomically mirror
Start/End, active cursor and orientation without restarting ADSR/filters. Reverse
maps playback i to raw N−1−i before effects, preserving PCM; cut/split stays raw. Hold loops through note-off/release; Arm cuts Hold voices. Editor Loop repeats;
Play auditions once. Loops preserve fractional overshoot, seam interpolation and
filters, retriggering ADSR. Pass fades cap Release to half the pass; Hold note-off
restores full Release. Cut edits wrap out-of-range cursors fractionally.
Filter and Dirty follow [Mixing](../../Docs/AI/MIXING.md).

Pad transfers commit retained immutable audio at block boundaries; unrelated
audio continues and host reads never wait. [Storage handoff](../../Docs/AI/PAD-STORAGE.md).

Collapse Gap moves descriptors/settings within the active bank/page without
copying PCM. Split and cut edits stage PCM and validate generations before atomic
cross-bank commit, skipping hidden slots. Recording, resets, audio,
and settings changes invalidate stale plans.

Four peak outputs; LEDs redraw at ≤30 FPS. Hard bypass cannot be metered.

## Project state

`pad_layout` and `midi_bank_mode` use remembered startup defaults; host values win.
Layout, bank mode, skin and global knob settings share one `defaults.ini`; only
explicit UI choices update it.
[Schema, initialization and format limits](../../Docs/AI/LAYOUT-PREFERENCE.md).

`GlobalValueMode` maps raw parameters through Normal/Quantized/Zero Snap and
optional ranges, preserving host parameter values/identities. Five durable
`global_*_settings` states use `KC1` codecs. `KnobPreference` seeds instance
settings/metadata; host state wins. Control-side mutex serialization and
`RealtimeLatestMailbox` deliver settings to audio without locks/allocation.
[Mapping](../../Docs/AI/MIXING.md); [restoration](../../Docs/AI/LAYOUT-PREFERENCE.md).

Pad state stores versioned, CRC-checked PCM16 as Base64; decoding validates it.
Durable restoration stages per-pad desired updates without waiting for
processing; published reads include them, and the next block commits them.

Cut/ADSR and mixer states are versioned. Import resets both; rechopping preserves
mixers and rebases markers on non-empty results, resets changed-pad ADSR,
and clears empty-pad settings. The UI matches waveform/settings replies, rejecting stale replies
and retrying failures. LV2 state/VST3 direct access carries 128-bin summaries.
Control-worker zoom detail is range/sequence checked; editors reproject cached
extrema with whole-source fallback. Pad/editor changes reset caches.
[Editor detail](../../Docs/AI/CHOP-EDITOR.md).

`pad_color_01..64` saves slot palette indices. Zero/unavailable indices mean no
tint; colors stay with slots through sample edits and moves.

`pad_clear_request` publishes an atomic command consumed at the next block.
**CLEAR PADS** preserves every global parameter, knob mode/range, layout, bank
mode, skin and startup default. Its released private `reset` request is retained.
The control worker clears pads at block boundaries, clipboard, split plans and
colors; the UI invalidates pad/editor snapshots and returns to the main view.
`pad_file_request` carries action/pad/UTF-8 path. VST3 uses an instance worker;
LV2 uses its required host worker. Small busy/status states and a hidden completion
output return results. VST3 bus excludes PCM, but DPF's initial state transfer
may send DSP-only Base64 pad PCM to a newly opened UI.

## Validation

[Build](../../Docs/AI/DEVELOPMENT.md); [test](../../Docs/AI/TESTING.md).
