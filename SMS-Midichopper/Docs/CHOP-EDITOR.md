# Adjust cut points

Choose **Adjust cut points** on an occupied pad to correct its surrounding cuts.
It needs slots immediately before and after, so bank edge pads cannot open it.
Either neighbor may be empty.

The editor combines the pad and neighbors into one raw waveform. **Cut 1**
divides the left/selected pads; **Cut 2** divides selected/right. Drag or wheel a
line to move audio across it.
Over the waveform, **Ctrl + wheel** zooms around the pointer and **Shift +
wheel** scrolls left or right. Each zoomed view requests detail from the
visible raw audio. Ordinary wheel still moves a cut; Ctrl/Shift wheel over a
cut controls the view. Navigating to another pad or leaving the editor resets zoom.

With an empty left or right pad, its cut starts at that source edge. Move it
inward to give the empty pad the source prefix or suffix; otherwise it stays empty.

The three buttons preview the proposed raw slices. Zero-length slices are
disabled; applying one empties its pad and clears its settings. Preview follows
pending cuts but ignores Start/End, ADSR, and mixer settings.

You can also trigger the three proposed slices from their corresponding MIDI
notes. While this view is open, other notes are silent and do not switch banks.
MIDI preview is one-shot, follows every pending cut adjustment, and moves the
waveform playhead just like clicking a pad button.

**Apply** rewrites the three samples and stays open, disabled until another
change. Affected non-empty pads reset Start/End/ADSR and preserve Gain/Pan/Tune.

Arrows load the previous or next eligible bank pad and discard pending cuts.
The round **Exit** button also discards them and leaves the view.

## Split a sample and insert a pad

Right-click an occupied pad and choose **Split Sample...** when an empty slot is
available later in the visible bank. The editor shows the selected raw sample
split at its midpoint. Drag the single **SPLIT** line and use the two pad buttons
to preview the proposed halves. The target pad's MIDI note previews the first
half, and the following pad's note previews the second, virtual half. All other
notes are silent until you apply or leave the view.

Choose **Apply** to insert both halves. Occupied pads between the sample and the
nearest empty slot move right by one. Their audio and settings move together.
The new halves keep the original Gain, Pan, and Tune, while Start/End and ADSR
return to defaults. Apply performs the shift and split together, then exits the
split view and opens Adjust Cut Points with both new halves visible. **Exit** or
Escape cancels the complete operation without moving pads.
If an affected pad changed while the editor was open, Apply is rejected.
Using an arrow also cancels the split before opening the neighboring cut view.

To close holes without splitting, right-click an empty pad and choose
**Collapse Gap**. Confirm the action to move the following uninterrupted group
of occupied pads left into the complete empty gap. Collapse stops at the next
empty pad and never crosses the visible bank.

Adjust Cut Points requires all non-empty samples in its three-pad window to use
the same sample rate. The plug-in does not store
capture-session identity, so it cannot prove that neighboring pads came from
one sequential recording. Do not combine unrelated imports or fixed-length
captures merely because their sample rates match.
