# Cut Point Editor contract

`RealtimeLatestMailbox` publishes complete raw-preview commands/ranges. Control
writers serialize; audio consumes the latest request without locks, allocation,
or retry. Unread requests are superseded as a whole.

## Model

The action needs a pad and both immediate neighbors; bank edge pads cannot
open it. Three raw 128-bin summaries form one waveform. Two signed frame offsets
stay UI-local until Exit/navigation saves them; they are neither host parameters
nor saved state.

The initial center is occupied; navigation and neighbors may be empty. One
`chop_snapshot_request/data` reply carries all three waveforms and generations
from a retained baseline.
`ChopEditorController` owns the workflow; `ChopEditorSession` matches/retries baselines.
Occupied pads must share a rate. PCM has no capture-session identity to verify ancestry.

## Interaction and preview

Cut 1 separates pads 0/1 and Cut 2 separates pads 1/2. Dragging or wheeling
clamps each cut between its neighbor and source edge. Cuts may coincide or meet
an edge, producing a zero-length pad. Empty edge slots begin at their source
edge and gain the prefix or suffix when moved inward. Buttons preview non-empty
proposed slices.
Ctrl-wheel zooms around the pointer; Shift-wheel pans. Modified wheel over a
handle controls the viewport; unmodified wheel moves the cut. Navigation,
save refresh, and Exit reset the viewport. Zoom preserves source offsets.
Zoom/scroll controls request raw 128-bin detail; drawing stays visible through the
[viewport cache](../../SMS-AnvilleSampler/Docs/ARCHITECTURE.md#project-state).

From entry through saving/loading, the editor owns MIDI exclusively; all notes
are silent until its waveform data is ready. Then the three displayed notes
trigger proposed slices. Split mode maps the target and following notes to the
prefix and virtual suffix. Other notes cannot sound or activate banks. Preview
is monophonic and one-shot, ignores note-off, and drives the raw playhead.

Exit (including Escape) and arrows save dirty cuts before leaving or loading the
previous/next eligible bank center. Clean transitions act immediately. Cancel
Changes is enabled only for edits; it stops preview and restores both loaded
cuts/MIDI ranges without reloading or leaving. Split retains explicit Apply/Cancel.

`chop_preview_request` carries the global source pads and inclusive/exclusive
frame range. The callback traverses existing blocks without allocation or
locking and bypasses per-pad Start/End, ADSR, and mixer settings. Playback gain
applies. Hidden output `chop_preview_position` drives the combined playhead.
`chop_midi_preview` publishes the current proposed ranges or an exclusive mute
map while the editor is loading or applying.

## Apply

`chop_apply_request` uses CH2: first pad, count three, two offsets, baseline
sequence, and expected generations. The parser understands old CH1 commands,
but the adapter rejects unguarded Apply requests. Durable state stays compatible.

The control worker retains a coherent source snapshot, validates bounds/rates,
and stages the repartitioned PCM. The callback rechecks generations, Arm, and
capacity before publishing the complete transaction. Any zero-length result
clears its pad. Empty results reset settings. On non-empty results, Start/End
retain their raw source positions, rebased to the new length and local direction.
Markers at source edges follow the new edges. A cut passing an interior marker
resets only that marker to its corresponding edge; Start at the new end or End
at the new start also resets to keep a playable region. Global Reverse does not
change stored markers. Pads touching a moved cut still reset ADSR and preserve
mixer settings; untouched pads retain settings. Unrelated audio and monitoring
continue. Ownership is specified in
[Storage handoff](PAD-STORAGE.md).

`chop_status` matches the request sequence. Success completes the pending
Exit/navigation once; navigation requests a fresh baseline. Failure abandons the transition, reports the error, discards offsets,
and reloads the current center. Controls stay disabled during saving; MIDI stays
muted until the destination/baseline is ready. Forced invalidation (Arm, bank or
layout changes) cancels the UI workflow and its pending transition. Replies and
statuses from earlier navigation or UI instances are ignored.

## Split Sample mode

**Split Sample...** becomes a two-pad insertion editor when an occupied target
has a later empty visible slot. The DSP records a non-mutating plan through the
nearest empty slot.

The target waveform starts split at `frames / 2`; the virtual second slot is the
suffix. Preview excludes the physical neighbor.
Navigation cancels the plan and opens the neighboring ordinary editor.

Apply revalidates pad generations, atomically shifts whole pads right, and
stores both halves. Both reset Start/End/ADSR and inherit source mixer settings;
shifted pads stay intact. Apply opens the ordinary editor around the halves.
Cancellation and failures do not mutate.

## Validation

- `sampler-core`: exact PCM/frame totals, interior/edge/crossed markers under
  local/global reverse, empty destinations/results, raw preview across storage,
  outside-note suppression, and cut termination.
- `sampler-transfer`: coherent baselines, stale generations, retained readers,
  and unrelated monitoring/playback continuity.
- `state-codec`: round-trips, malformed fields, and UI-bus reply capacity.
- `ui-geometry`: handles, viewport/cache mapping, empty edges, navigation,
  preview, readiness, retries, stale replies, automatic Exit/Prev/Next saves,
  clean transitions, cancellation, and failed-save recovery.
- Pad structure: gap collapse, nearest-empty split shifts, mixed rates, settings
  movement, adjusted splits, stale generations, and 8/12/16-pad boundaries.
- LV2: follow the [user workflow](../../SMS-AnvilleSampler/Docs/CHOP-EDITOR.md)
  from both main and Sample Editor menus, including empty/zeroed neighbors,
  automatic Exit/Prev/Next saves, Cancel Changes, and rejected-save recovery.
  Listen across both cuts; interior markers retain source positions, edge markers
  follow edges, crossed markers
  reset independently, and changed pads reset ADSR. Verify displayed MIDI notes
  before/after edits; other notes stay silent without switching banks.
- Split: preview midpoint/adjusted halves by buttons and MIDI, cancel, then
  Apply. Verify one shift, moved settings, ordinary editor opening, and intact
  hidden/next-bank slots.

## Limitation

Imported or fixed-capture pads can pass structural checks. A future source/session
identifier could remove this ambiguity.
