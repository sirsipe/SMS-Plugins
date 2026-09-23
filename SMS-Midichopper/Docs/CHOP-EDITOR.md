# Adjust cut points

Right-click an occupied pad and choose **Adjust cut points** to correct the two
cuts around it. The selected pad needs a pad slot immediately before and after
it, so the action is unavailable on the first or last pad in a bank. Either
neighbor may be empty.

The editor combines the left neighbor, selected pad, and right neighbor into
one raw waveform. **Cut 1** divides the left and selected pads; **Cut 2** divides
the selected and right pads. Drag either line, or use the wheel over it, to move
audio across that cut.

When the left pad is empty, Cut 1 starts at the far left. Drag it right to give
the empty pad the beginning of the selected sample. When the right pad is empty,
Cut 2 starts at the far right. Drag it left to give that pad the end of the
selected sample. An empty neighbor stays empty if its boundary is not moved.

The three buttons below the waveform represent those three pads. Click one to
hear only its proposed raw slice. A zero-length pad button is disabled until a
cut gives it audio. You can also move a cut all the way to a source edge or the
other cut. Applying a zero-length result empties that pad and clears all of its
settings. Preview ignores Start/End, ADSR, and mixer settings but obeys the two
pending cut positions.

Choose **Apply** to rewrite the three samples at the proposed cuts. Apply stays
in the editor and becomes disabled until you make another change. Start, End,
and ADSR return to defaults on non-empty pads touching a changed cut. Gain, Pan,
and Tune are preserved unless the pad is emptied.

Use the left and right arrows to adjust the previous or next eligible pad in
the current bank. Pending, unapplied cuts are discarded when you navigate. Use
the round **Exit** button between the arrows to discard pending cuts and leave
the view.

All non-empty samples must use the same sample rate. The plug-in does not store
capture-session identity, so it cannot prove that neighboring pads came from
one sequential recording. Do not combine unrelated imports or fixed-length
captures merely because their sample rates match.
