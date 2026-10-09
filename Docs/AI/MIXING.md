# Mixing implementation

Pad mixer settings are stored in `SampleMixerSettings`. The `MX2` codec saves
gain, pan, tune, lowpass, highpass, slope, and Dirty. `MX1` pad states still load;
their new effects default to off and slope to 12 dB/octave. New pads and global
controls default to a 6 dB/octave slope. Global effect
parameters are appended after the released host parameter IDs.

Monitor Gain (`monitor_gain_db`) is appended after Any Playback Active; all
existing parameter IDs and symbols stay fixed. It and Global Volume
(`output_gain`) default to 0 dB and range from -24 to +12 dB. The adapter converts
each to linear gain per block. Engine gains default to 1; non-finite values
fall back to 1 and negative values become 0.

Global Volume scales the sum of pad playback and raw Cut Point Editor previews.
Monitor Gain scales only the gated stereo input. These two paths sum at the
output; Global Volume, pad/global pan, tuning, and effects do not affect monitor
input. Off/On/Auto still controls the monitor gate, with Auto enabled while
armed. Recording and pre-roll store the original input PCM independently of
both gains. Neither gain changes stored or exported PCM.

At playback, either pad or global Dirty enables the same source conversion.
It folds stereo to mono, holds source frames at a 26.04 kHz clock, then quantizes
the held signal to 12 bits before varispeed interpolation. It applies only to
pad playback; recording, monitor input, raw previews, and exported PCM remain
unchanged. Enabling Dirty in both places does not compound its conversion.

Each active voice then passes through its pad lowpass/highpass and the global
lowpass/highpass before level and pan are applied. Filters have 6, 12, or 24
dB/octave choices. Zero amount bypasses a filter. The amount maps the cutoff
logarithmically from 20 kHz to 20 Hz for lowpass and 20 Hz to 20 kHz for
highpass, clamped below the host Nyquist limit. In 12 and 24 dB modes, the
filter Q rises from about 0.71 to 1.26 as amount increases. The 6 dB mode has
no Q parameter. This implements the request for more resonance as the cutoff
moves farther from off while keeping slope independent. The global filter
operates per voice before the voices sum, so it does not color monitored input.

Filter coefficients are updated at block boundaries as live settings change.
They and all per-voice state are preallocated; the audio callback performs no
allocation, I/O, lock, or state serialization. Source/sample state copying
uses the retained-snapshot and bounded-commit [storage contract](PAD-STORAGE.md).
