# Chop Editor

The Chop Editor corrects boundaries between adjacent pad samples while keeping
the active 16-, 12-, or 8-pad layout visible.

Open it from **Play** mode. Occupied pads display their raw waveforms without
Start/End trimming or ADSR. A separator appears when two neighboring pads are
occupied and use the same sample rate. Pull it left or right to roll audio
between the pads. The lever springs back when released while the proposed
waveforms retain the adjustment.

Drag inside a waveform to seek. **Play** continues through that contiguous pad
run as raw audio; **Pause** keeps the last playhead position. The preview ignores
pad regions and ADSR so they cannot hide the boundary being adjusted.

Edits remain a preview until **Apply**. Applying reconstructs the contiguous raw
audio and rewrites the pads at the proposed boundaries. Start, End, and ADSR
return to defaults on pads touching a changed boundary. Other pads keep their
settings. **Revert** discards every unapplied boundary adjustment. Apply or
Revert before leaving the editor or changing its bank or layout.

Use one sequential capture per contiguous run. The current plug-in does not
store capture-session identity, so occupancy and sample rate cannot distinguish
a real sequential recording from unrelated imported or fixed-length samples.
Do not join unrelated pads. A separator is unavailable beside an empty pad or
between samples with different rates.
