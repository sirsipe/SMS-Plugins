# Cut Point Editor contract

Audience: agents changing three-pad cut editing, raw preview, pad storage,
context-menu actions, or waveform interaction.

## Model

The action starts from one context-menu pad and requires its immediate left and
right neighbors in the active bank. The first and last local pads cannot open
it. The UI requests three raw 128-bin summaries and shows them as one ordered
waveform. The two signed frame offsets remain UI-local until Apply; they are not
host parameters or saved project state.

The editor becomes usable only when all three pads are occupied and share a
sample rate. It cannot verify recording ancestry because the engine stores PCM
per pad without capture-session identity.

## Interaction and preview

Cut 1 is the boundary between pads 0 and 1; Cut 2 is between pads 1 and 2.
Dragging clamps each cut between its neighboring cut or source edge, preserving
at least one frame per pad. The pad buttons below the waveform preview the
corresponding proposed slice.

`chop_preview_request` carries the three-pad global range plus an inclusive
start/exclusive end frame range. The audio callback traverses existing pad
blocks without allocation or locking, stops at the proposed cut, and bypasses
per-pad Start/End and ADSR. Global output gain still applies. The hidden
`chop_preview_position` output drives the playhead on the combined waveform.

## Apply

`chop_apply_request` carries the first global pad, count three, and two frame
offsets. The DSP validates the complete request before mutation: all pads must
be occupied, use one sample rate, remain non-empty, remain within the per-pad
frame limit, and fit the existing block pool.

Apply uses `RealtimeAccessGate` on the control thread. It reconstructs one PCM
sequence, releases participating storage, and repartitions the same frames at
the proposed cuts. Pads touching a non-zero cut reset Start, End, and ADSR;
untouched pads retain their settings. `chop_status` reports completion. Cancel
only clears UI-local state.

## Validation

- `sampler-core`: positive and negative cut moves preserve PCM order and total
  frames; bounded raw preview crosses original storage boundaries and stops at
  the proposed cut.
- `state-codec`: apply and bounded-preview commands round-trip and reject bad
  versions, counts, ranges, missing fields, and invalid numbers.
- `ui-geometry`: both cut handles, one-frame clamping, duration transfer,
  combined-waveform ordering, playhead mapping, and all three preview buttons.
- In an LV2 host, right-click a middle pad from the main or Sample Editor view.
  Check missing-neighbor behavior, both drags, all preview buttons, Cancel, and
  Apply. Listen across both edited cuts and confirm shaping resets only on pads
  touching a changed cut.

## Limitation

Imported or fixed-capture pads can pass the structural checks. A future shared
source/session identifier can remove that ambiguity without changing the
three-pad transaction.
