# Pad and global mixing

The main PLAY view has a **Global Mixer**: Pan and Dirty, then Tune, Lowpass,
and Highpass. ARM hides it. **SAMPLE EDITOR** has a **Pad Mixer** for the selected
pad: Gain and Pan beside Dirty, Tune, Lowpass, and Highpass, with a divider
between Pan and Dirty. Each mixer places Slope below the filters.

The vertical faders at the window edges are available in every view. **IN**
sets the monitored input level; **OUT** sets sample playback and raw-preview
level. Their gains are independent: OUT leaves monitoring unchanged, and IN
leaves playback and recorded PCM unchanged. Both range from −24 to +12 dB and
default to 0 dB. Monitor Off/On/Auto still controls whether input is audible.
The surrounding stereo LEDs show raw input and the final combined output.

Drag the fader cap or click its slot; wheel to adjust. Double/middle-click a
fader to reset to 0 dB. Double-click its value below the meter for exact entry;
Enter commits and Escape cancels. Levels are host parameters saved with projects.

**Lowpass** removes treble as you raise it from 0% to 100%. **Highpass** removes
bass. At 0%, each filter is off. Both add resonance as you turn them farther
from off. **Slope** selects 6, 12, or 24 dB per octave; click or drag its
slider, or use the wheel. A steeper slope removes unwanted frequencies faster.

**Dirty** is a sliding on/off switch. It folds pad playback to mono, holds it at a
26.04 kHz source clock, and reduces it to 12-bit resolution. Dirty runs before
filtering and tune. You can enable it for one pad or for every pad using the
global switch. Monitoring the input remains clean. The pad effects are saved
with each pad; global effects are host parameters saved with the project.

Drag or wheel Lowpass and Highpass. Double-click their values to type a
percentage, or middle-click to reset. Click Dirty to toggle it.
