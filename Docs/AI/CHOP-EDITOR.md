# Cut Point Editor contract

Contract for cut editing, preview, storage, and waveform input.

## Model

The action needs a pad and both immediate neighbors; bank edge pads cannot
open it. Three raw 128-bin summaries form one waveform. Two signed frame offsets stay UI-local until Apply; they are neither
host parameters nor saved state.

The initial center pad is occupied, but arrow navigation may center an empty
slot; either neighbor may also be empty. One `chop_snapshot_request/data` reply
carries all three waveforms and generations from a retained baseline.
`ChopEditorSession` matches the request sequence and retries pending requests.
Occupied pads must share a rate. Per-pad PCM has no capture-session identity, so source
ancestry cannot be verified.

## Interaction and preview

Cut 1 separates pads 0/1 and Cut 2 separates pads 1/2. Dragging or wheeling
clamps each cut between its neighbor and source edge. Cuts may coincide or meet
an edge, producing a zero-length pad. Empty edge slots begin at their source
edge and gain the prefix or suffix when moved inward. Buttons preview non-empty
proposed slices.
Ctrl-wheel zooms around the pointer; Shift-wheel pans within the source. The
visible range gets a fresh raw 128-bin summary. Modified wheel input over a
handle controls the viewport; unmodified wheel still moves the cut. Navigation,
Apply refresh, and Exit reset the viewport. Zoom never changes source offsets.
The vertical zoom control and horizontal scroll window use the same viewport
and trigger the same detail request when dragged.

From entry through Apply/loading, the editor owns MIDI exclusively; all notes
are silent until its waveform data is ready. Then the three displayed notes
trigger proposed slices. Split mode maps the target and following notes to the
prefix and virtual suffix. Other notes cannot sound or activate banks. Preview
is monophonic and one-shot, ignores note-off, and drives the raw playhead.

Arrows load the previous or next eligible center in the bank and discard pending
offsets. Exit also discards them and returns to the main view.

`chop_preview_request` carries the global source pads and inclusive/exclusive
frame range. The callback traverses existing blocks without allocation or
locking and bypasses per-pad Start/End, ADSR, and mixer settings. Global gain
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
clears its pad. Empty results reset settings. Non-empty pads touching a moved
cut reset Start/End/ADSR but preserve mixer settings; untouched pads retain
settings. Unrelated audio and monitoring continue. Ownership is specified in
[Storage handoff](PAD-STORAGE.md).

`chop_status` matches the request sequence. Completion refreshes the baseline
and disables Apply; a stale failure discards offsets and loads current samples.
The editor mutes its MIDI until the refreshed baseline is ready. Replies and
statuses belonging to an earlier navigation or UI instance are ignored.

## Split Sample mode

**Split Sample...** becomes a two-pad insertion editor when an occupied target
has a later empty visible slot. The DSP records a non-mutating plan through the
nearest empty slot.

Only the target raw waveform appears. Its boundary starts at `frames / 2`; the
virtual second slot is the suffix. Preview excludes the physical neighbor.
Navigation cancels the plan and opens the neighboring ordinary editor.

Apply revalidates pad generations, atomically shifts whole pads right, and
stores both halves. Both reset Start/End/ADSR and inherit source mixer settings;
shifted pads stay intact. Apply opens the ordinary editor around the halves.
Cancellation, stale plans, and failures do not mutate.

## Validation

- `sampler-core`: cut moves preserve PCM order/frame totals, including empty
  edge destinations and zero-length results. Raw preview crosses storage
  boundaries, ignores outside notes, and stops at the proposed cut.
- `sampler-transfer`: coherent baselines, stale generation rejection, retained
  readers, and continuous unrelated monitoring/playback during edits.
- `state-codec`: commands/replies round-trip; malformed versions, counts,
  ranges, sequences, and generations fail; snapshot replies fit the UI bus.
- `ui-geometry`: handles, viewport/drag/wheel clamping, empty-edge geometry,
  navigation, previews, coherent session readiness, retry, and stale replies.
- Pad structure: single/multiple empty-gap collapse, nearest-empty split shifts,
  mixed source rates, settings movement, midpoint and adjusted splits, stale
  generation rejection, and 8/12/16-pad visible boundaries.
- In an LV2 host, right-click a middle pad from the main or Sample Editor view.
  Test empty/zeroed neighbors, drags, previews, arrows, Exit, and Apply.
  Apply stays open and disables after refresh. Listen across both edited cuts and
  confirm shaping resets only on non-empty pads touching a changed cut. Trigger
  all three displayed pads from MIDI before and after moving both cuts; confirm
  other notes are silent and do not change the active bank.
- Right-click an occupied pad with a later empty slot and choose **Split
  Sample...**. Preview both midpoint halves, adjust the split, cancel once, then
  Apply. Confirm MIDI for the target and following pad previews the proposed
  prefix and virtual suffix, while other notes remain silent. Confirm later pads
  shift once, settings follow them, the editor exits, and no hidden or next-bank
  slot changes.

## Limitation

Imported or fixed-capture pads can pass the structural checks. A future shared
source/session identifier can remove that ambiguity without changing the
three-pad transaction.
