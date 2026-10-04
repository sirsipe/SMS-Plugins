# Pad and global mixing

The main view has a **Global Mixer**. The Sample Editor has a **Pad Mixer** for
the selected pad. All controls stay visible. The global mixer has Volume, Pan,
and Dirty on its first row, then Tune, Lowpass, and Highpass. The pad mixer puts
Gain and Pan beside Dirty, Tune, Lowpass, and Highpass, with a vertical divider
between Pan and Dirty. Each mixer places its Slope slider below the filters.

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
