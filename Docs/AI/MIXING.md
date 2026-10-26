# Mixing implementation

Pad mixer settings are stored in `SampleMixerSettings`. The `MX4` codec saves
gain, pan, tune, lowpass, highpass, slope, Dirty, reverse (0=Forward, 1=Reverse),
and `globalMixer` (default 1=enabled). `MX1`–`MX3` load with Global Mixer enabled.
`MX1` and `MX2` load Forward; `MX1` effects default to off and slope
to 12 dB/octave. Direction is a saved per-pad mixer setting, transported through
existing `pad_mix_01..64` states and editor snapshots; host parameter IDs stay fixed.
See [playback](../../SMS-AnvilleSampler/Docs/ARCHITECTURE.md#real-time-behavior)
for voice latching, live editor flips, and playback-coordinate boundaries.
Transient `pad_direction_request` carries the desired playback/mixer settings
together. Its mailbox commit mirrors an active voice without retriggering;
durable setting restores retain next-trigger direction latching.
Appended `global_reverse` (default Forward) combines with pad direction by XOR.
`setSettings` turns active voices with Global Mixer enabled when its master value changes;
new triggers and live pad actions use the combined direction. Stored pad cuts
remain local; playback and editor display mirror them under the master toggle.
Raw previews and per-pad exports exclude the master. New pads and global
controls default to a 6 dB/octave slope. The per-pad Global Mixer power toggle
bypasses global pan, tuning, Dirty, filters, and Reverse at block boundaries.
Changing it under Global Reverse mirrors the active cursor/cuts without
retriggering; local direction stays latched. The separate OUT master gain remains
active. The flag follows mixer state, clipboard, transfers, and editor snapshots.
Global effect
parameters are appended after the released host parameter IDs.

Global OUT, Pan, Tune, Lowpass, and Highpass each have private durable settings
(`global_output_settings`, `global_pan_settings`, `global_tune_settings`,
`global_lowpass_settings`, `global_highpass_settings`), encoded by the KC1
codec in `GlobalValueMode.hpp` (`KC1;mode;min;max`, modes 0/1/2). They are not
host automation parameters. Range RESET restores only that control's native
limits, preserving its mode; CLEAR PADS preserves all global settings/values.
No host parameter IDs are added; existing IDs through Global Reverse remain
fixed. IN and pad controls are unaffected. The [knob settings manual](../../SMS-AnvilleSampler/README.md#knob-settings)
owns user-facing behavior; [startup preferences](LAYOUT-PREFERENCE.md) owns
shared defaults and host precedence.

The custom range remaps the complete original host/MIDI range into its selected
audible range. Raw host values are unchanged by settings edits; displayed and
audible values use the mapped range. Quantized mode uses the existing Ctrl+wheel
steps listed in the knob settings manual. Zero Snap holds a region
around zero at zero and continuously rescales either side to preserve endpoints.
Each side's width is 5% of the selected span, capped at half its distance from
zero to keep asymmetric endpoints reachable. `rawGlobalValue` inverts the
mapping for mouse/text/wheel edits. Quantized steps and Ctrl+wheel use the
zero-anchored grid, restricted to in-range points; narrow ranges without a
grid point cannot use Quantized. Text/wheel round trips tolerate float ULPs
without moving the grid. Excluding zero via range editing selects Normal if
Zero Snap was active. Strict KC1 decoding rejects invalid ranges/modes.
All five settings are published as one coherent snapshot and consumed at audio
block boundaries through `RealtimeLatestMailbox`; the audio callback does not
lock, allocate, or perform I/O. The UI decodes matching private state and
interprets raw parameters with the same helper; changes never rewrite raw
host values. `GlobalControlMenu` owns placement, confirmation and original
limits; `MixerValueNavigation` owns numeric focus order and keypad Enter
variants forwarded by pinned DGL/Pugl.

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
