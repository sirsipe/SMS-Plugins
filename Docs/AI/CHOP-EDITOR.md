# Cut Point Editor contract

Audience: agents changing three-pad cut editing, raw preview, pad storage,
context-menu actions, or waveform interaction.

## Model

The action starts from one context-menu pad and requires its immediate left and
right neighbors in the active bank. The first and last local pads cannot open
it. The UI requests three raw 128-bin summaries and shows them as one ordered
waveform. The two signed frame offsets remain UI-local until Apply; they are not
host parameters or saved project state.

The context-menu action starts from an occupied selected pad, but arrow
navigation may later center an empty slot. Either neighboring slot may be
empty. All three waveform responses must arrive, and all non-empty pads must
share a sample rate. The editor cannot verify recording ancestry because the
engine stores PCM per pad without capture-session identity.

## Interaction and preview

Cut 1 is the boundary between pads 0 and 1; Cut 2 is between pads 1 and 2.
Dragging or wheeling clamps each cut between its neighbor or source edge. A cut
may meet the other cut or a source edge, producing a zero-length pad. An empty
left slot starts with Cut 1 at the far-left edge; dragging right assigns it the
source prefix. An empty right slot starts with Cut 2 at the far-right edge;
dragging left assigns it the source suffix. Pad buttons preview corresponding
non-empty proposed slices and are disabled at zero length.

The right-panel arrows move to the previous or next eligible center pad within
the current bank and reload its three-pad window. Navigation discards pending
offsets. Exit also discards pending offsets and returns to the main view.

`chop_preview_request` carries the three-pad global range plus an inclusive
start/exclusive end frame range. The audio callback traverses existing pad
blocks without allocation or locking, stops at the proposed cut, and bypasses
per-pad Start/End and ADSR. Global output gain still applies. The hidden
`chop_preview_position` output drives the playhead on the combined waveform.
Per-pad mixer settings are also bypassed.

## Apply

`chop_apply_request` carries the first global pad, count three, and two frame
offsets. The DSP validates the complete request before mutation: occupancy and
frame counts must agree, non-empty pads must use one sample rate, boundaries
must remain ordered and inside the source, slices must remain within the per-pad
frame limit, and the result must fit the existing block pool. Zero-length edge
slices and a zero-length middle slice are valid and clear the corresponding
pad.

Apply uses `RealtimeAccessGate` on the control thread. It reconstructs one PCM
sequence, releases participating storage, and repartitions the same frames at
the proposed cuts. A zero-length result empties that pad and resets all of its
playback and mixer settings. Non-empty pads touching a moved cut reset Start,
End, and ADSR while preserving Gain, Pan, and Tune. Untouched non-empty pads
retain all settings. `chop_status` reports completion. A successful Apply stays
in the editor, reloads the three waveforms as the new baseline, and disables
Apply until another cut changes.

## Validation

- `sampler-core`: positive and negative cut moves preserve PCM order and total
  frames; empty edge slots can receive prefixes or suffixes; zero-length edge
  and middle results clear their pad and settings; bounded raw preview skips
  empties, crosses original storage boundaries, and stops at the proposed cut.
- `state-codec`: apply and bounded-preview commands round-trip and reject bad
  versions, counts, ranges, missing fields, and invalid numbers.
- `ui-geometry`: both cut handles, edge placement for empty neighbors,
  drag/wheel clamping, duration transfer, combined-waveform ordering,
  playhead mapping, preview-button enablement, navigation bounds and action hit
  testing.
- In an LV2 host, right-click a middle pad from the main or Sample Editor view.
  Check empty-neighbor behavior, zeroing each pad position, both drags,
  preview-button enablement, arrows, Exit, and Apply. Confirm Apply stays open
  and becomes disabled after its refresh. Listen across both edited cuts and
  confirm shaping resets only on non-empty pads touching a changed cut.

## Limitation

Imported or fixed-capture pads can pass the structural checks. A future shared
source/session identifier can remove that ambiguity without changing the
three-pad transaction.
