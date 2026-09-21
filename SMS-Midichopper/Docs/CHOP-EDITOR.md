# Adjust cut points

Right-click an occupied pad and choose **Adjust cut points** to correct the two
cuts around it. The action requires occupied pads immediately before and after
the selected pad, so it is unavailable on the first or last pad in a bank.

The editor combines the left neighbor, selected pad, and right neighbor into
one raw waveform. **Cut 1** divides the left and selected pads; **Cut 2** divides
the selected and right pads. Drag either line to move audio across that cut.

The three buttons below the waveform represent those three pads. Click one to
hear only its proposed raw slice. Preview ignores Start/End and ADSR but obeys
the two pending cut positions.

Choose **Apply** to rewrite the three samples at the proposed cuts. Start, End,
and ADSR return to defaults on pads touching a changed cut. Choose **Cancel** to
discard the proposed cuts without changing any sample.

All three samples must be occupied and use the same sample rate. The plug-in
does not store capture-session identity, so it cannot prove that neighboring
pads came from one sequential recording. Do not combine unrelated imports or
fixed-length captures merely because their sample rates match.
