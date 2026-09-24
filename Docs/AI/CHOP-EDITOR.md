# Cut Point Editor contract

Audience: agents changing cut editing, raw preview, storage, or waveform input.

## Model

The action requires one context-menu pad and both immediate neighbors, so the
first and last bank pads cannot open it. Three raw 128-bin summaries form one
waveform. Two signed frame offsets stay UI-local until Apply; they are neither
host parameters nor saved state.

The initial center pad is occupied, but arrow navigation may center an empty
slot; either neighbor may also be empty. All responses must arrive and occupied
pads must share a rate. Per-pad PCM has no capture-session identity, so source
ancestry cannot be verified.

## Interaction and preview

Cut 1 separates pads 0/1 and Cut 2 separates pads 1/2. Dragging or wheeling
clamps each cut between its neighbor and source edge. Cuts may coincide or meet
an edge, producing a zero-length pad. Empty edge slots begin at their source
edge and gain the prefix or suffix when moved inward. Buttons preview non-empty
proposed slices.

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

`chop_apply_request` carries the first pad, count three, and two offsets. Before
mutation, the DSP validates occupancy/frame counts, sample rates, ordered source
bounds, per-pad limits, and pool capacity. Any zero-length result clears its pad.

Apply uses `RealtimeAccessGate` on the control thread, rebuilds one PCM sequence,
releases its storage, and repartitions the frames. Empty results reset all pad
settings. Non-empty pads touching a moved cut reset Start/End/ADSR but preserve
Gain/Pan/Tune; untouched pads retain settings. `chop_status` reports completion.
Success stays in the editor, reloads its baseline, and disables Apply until the
next change.

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
Exit, Escape, stale plans, and failures do not mutate.

## Validation

- `sampler-core`: positive and negative cut moves preserve PCM order and total
  frames; empty edge slots can receive prefixes or suffixes; zero-length edge
  and middle results clear their pad and settings; bounded raw preview skips
  empties, crosses original storage boundaries, and stops at the proposed cut;
  MIDI selects proposed ordinary and split slices while outside notes are silent.
- `state-codec`: apply and bounded-preview commands round-trip and reject bad
  versions, counts, ranges, missing fields, and invalid numbers.
- `ui-geometry`: both cut handles, edge placement for empty neighbors,
  drag/wheel clamping, duration transfer, combined-waveform ordering,
  playhead mapping, preview-button enablement, navigation bounds and action hit
  testing; split mode exposes one handle and two previews.
- Pad structure: single/multiple empty-gap collapse, nearest-empty split shifts,
  mixed source rates, settings movement, midpoint and adjusted splits, stale
  generation rejection, and 8/12/16-pad visible boundaries.
- In an LV2 host, right-click a middle pad from the main or Sample Editor view.
  Check empty-neighbor behavior, zeroing each pad position, both drags,
  preview-button enablement, arrows, Exit, and Apply. Confirm Apply stays open
  and becomes disabled after its refresh. Listen across both edited cuts and
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
