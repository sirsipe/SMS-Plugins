# Chop Editor contract

Audience: agents changing rolling boundaries, raw preview, pad storage, state,
or layout-aware waveform UI.

## Model

The current engine owns PCM per pad; it does not persist a shared recording or
capture-session identity. The Chop Editor therefore treats a contiguous run of
occupied, same-rate pads as one virtual source. The UI holds signed frame
offsets relative to the original boundaries. It derives preview durations and
approximate rolling waveforms from the existing 128-bin pad summaries. These
offsets are intentionally neither host parameters nor saved project state.

Only boundaries with compatible occupied neighbors are interactive. Row-wrap
boundaries use an outgoing lever on the earlier pad. The active layout and MIDI
bank mode determine the visible global pad range.

## Apply and state

`chop_apply_request` encodes the first global pad, run length, and one frame
offset per boundary. The DSP validates the complete request before mutation:
all pads must be occupied, have one sample rate, remain non-empty, remain within
the per-pad frame limit, and fit the existing sample-block pool.

Apply uses `RealtimeAccessGate` on the control thread. It exports one virtual
PCM sequence, releases the participating storage, and repartitions the sequence
at the proposed boundaries. Pads touching a non-zero boundary reset their
Start, End, and ADSR; untouched pads retain their settings. The total frame
order and count do not change. `chop_status` returns success or a short error,
after which the UI requests fresh summaries. UI code must never rewrite storage
on pointer motion.

`chop_preview_request` publishes a small atomic play/stop/seek command. The
audio callback traverses existing pad blocks, performs no allocation or lock,
and bypasses per-pad region and ADSR processing. Global output gain still
applies. The hidden `chop_preview_position` output encodes global pad plus
normalized local position for the playhead.

## Validation

- `sampler-core`: verify positive and negative boundary moves preserve PCM
  order and total frames, reject empty/oversized results, reset only touched
  shaping, and cross pad boundaries in raw preview.
- `state-codec`: round-trip apply and preview commands; reject bad versions,
  counts, ranges, missing offsets, extra fields, and invalid numbers.
- `ui-geometry`: verify horizontal and row-wrap handles, compatible-neighbor
  hit testing, clamping to one frame, duration transfer, waveform rolling, and
  interactive transport targets.
- In an LV2 host, capture adjacent sequential pads. Check every layout, bank,
  separator drag, waveform seek, Play/Pause, Revert, Apply, and project reload.
  Listen across changed boundaries and confirm Apply resets touched Start/End
  and ADSR. Empty or different-rate neighbors must not expose a separator.

## Limitations

The editor cannot prove that adjacent pads came from one sequential capture.
Imported or fixed-capture pads can satisfy its structural checks but should not
be joined. A future shared-source/session model can replace this inference while
retaining the UI and transaction semantics.
