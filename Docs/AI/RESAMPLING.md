# Sample resampling

Audience: agents changing source-rate conversion or varispeed. The shared
implementation is [SincResampler.hpp](../../Common-Src/DSP/SincResampler.hpp).

## Consumers and boundaries

- [PlaybackVoice](../../SMS-AnvilleSampler/src/core/PlaybackVoice.cpp) combines
  source/host rate with pad and enabled global tune. Stereo coefficients are
  shared. Capture maps use paired storage reads. Dirty deliberately retains its
  original linear, held/quantized mono path; see [mixing](MIXING.md).
- [SampleStoragePool](../../SMS-AnvilleSampler/src/core/SampleStoragePool.cpp)
  normally preserves imported PCM/rate. Only its capacity fallback converts to
  the host rate, outside audio, with the same resampler.
- [processed WAV rendering](../../Common-Src/Audio/WavCodec.cpp) uses the same
  filter for pad tune. Raw exports, captured PCM, monitoring and raw editor
  previews are unaffected. Global settings remain excluded from pad exports.

The caller retains the existing source cursor and output-frame duration.
The filter has no history, delayed output, or tail. Reverse maps each requested
playback-coordinate frame to the original source. All taps stay within the
selected region: one-shots extend the edge samples; loops wrap each tap inside
the current Start/End, including regions shorter than the filter. No neighboring
chop audio contributes. Existing fractional loop overshoot, live cuts/direction,
ADSR, voice ownership and saved-state/parameter identities remain unchanged.
Pitched or rate-converted playback from old projects can sound different.

## Filter and real-time contract

Construct resamplers outside audio. The first constructor computes two immutable
shared float tables; later reads/configuration never initialize statics,
allocate, lock, perform I/O, or call transcendental functions.

Upsampling uses a Blackman-windowed sinc with a 16-source-frame half-width and
unity cutoff. Integer phases at rates up to unity read exact PCM. Downsampling
uses a Kaiser window, beta 5.5, half-width 12 times the playback step, and cutoff
0.85 divided by the step. Tables have 256 points per source-frame unit and
interpolated coefficients. Per-phase normalization preserves DC. Interior reads
skip region mapping; boundary reads clamp or wrap before accessing storage.

Between steps 1 and 1.1, continuously blend the scaled upsampling kernel into
the downsampling kernel to avoid a response jump at unity. Rejection is weaker
in this interval. From steps 1.1 through 256, spectral regression tones test
over 50 dB of rejection at/above destination Nyquist and less than 0.002 RMS
error for unit-amplitude passband tones through 70% of destination Nyquist.
These are tested targets, not guarantees for every signal or boundary.

Support is capped at a 4,096-frame radius (8,193 taps/output); extreme rate ratios
lose rejection. CPU work grows with playback step. No benchmark can guarantee
deadlines on every machine, particularly with large source/host rate differences
and high polyphony. No quality control or new state field is introduced.

## Validation

`resampling` tests analytic tones, unity continuity, DC/bounds on tiny regions,
extreme-rate work limits, import/playback agreement, processed-export parity,
duration and no added output tail. `sampler-core`, `playback-voice`,
`buffer-timing` and `wav-codec` compare against a separate analytic sinc oracle;
timing/ownership and Dirty assertions remain independent.

Run `build/anvillesampler_resampling_tests --benchmark` in a Release build for
16 imported/captured stereo voices at 48 kHz/128 frames, including mismatched
rates and maximum combined tuning. It reports median/p95 callback times against
the block budget without a machine-dependent CTest timing threshold.
Run the allocation/transfer and state suites plus [host checks](TESTING.md).
