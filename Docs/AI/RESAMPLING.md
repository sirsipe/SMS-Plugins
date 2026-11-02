# Sample resampling

Audience: agents changing source-rate conversion or varispeed. The shared
implementation is [SincResampler.hpp](../../Common-Src/DSP/SincResampler.hpp).

## Consumers and boundaries

- [PlaybackVoice](../../SMS-AnvilleSampler/src/core/PlaybackVoice.cpp) combines
  source/host rate with pad and enabled global tune. Stereo coefficients are
  shared. Capture maps use paired storage reads. Dirty deliberately retains its
  original linear, held/quantized mono path; see [mixing](MIXING.md).
- [SampleStoragePool](../../SMS-AnvilleSampler/src/core/SampleStoragePool.cpp)
  prepares imports and restored PCM outside audio using the stored-rate policy
  below. A smaller-host-rate fallback can still resolve capacity limits.
- [processed WAV rendering](../../Common-Src/Audio/WavCodec.cpp) uses the same
  filter for pad tune. Raw exports and raw editor previews read the stored PCM; monitoring
  stays at host rate. Global settings remain excluded from pad exports.

The caller retains the existing source cursor and output-frame duration.
The playback filter retains no history, delayed output, or tail. Reverse maps each requested
playback-coordinate frame to the original source. Playback taps stay within the
selected region: one-shots extend the edge samples; loops wrap each tap inside
the current Start/End, including regions shorter than the filter. No neighboring
chop audio contributes. Existing fractional loop overshoot, live cuts/direction,
ADSR, voice ownership and saved-state/parameter identities remain unchanged.
Pitched or rate-converted playback from old projects can sound different.

## Stored PCM and live conversion

The [stored-rate policy](../../SMS-AnvilleSampler/Docs/WAV-FILES.md#stored-sample-rates)
is authoritative for user-visible behavior. `kMaximumStoredSampleRate` lives in
`SampleData.hpp`. `prepareImport()` enforces it for import, paste, edit staging
and restoration; converted peak/RMS are recomputed from the retained PCM.
The state codec still accepts older higher-rate data; engine staging converts it.
No host parameter or saved-state format changes.

`SamplerEngine` separates host input counts from stored recording frames and
sizes capture reserves/peak history at the stored rate. Above that rate,
`StreamingDownsampler` uses the same filter with one preallocated FIR history
window. It waits for right-hand taps and edge-flushes at finalization, without
extending the recording or delaying monitoring. Pre-roll feeds raw history into
that converter; trimming follows conversion of the untrimmed capture and maps
retained host duration to stored frames.
Duration is rounded once, with at least one stored frame for nonempty input.
Rate changes finalize the old capture before reconfiguring history outside audio.
Capture conversion and pre-roll setup consume CPU; playback benchmarks exclude them.

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
At integer cursor phases and integral table strides, cache normalization once
per rate change and reuse coefficients directly. SSE2 stereo double accumulators
have a scalar fallback. Both keep taps in ascending order. Keep the two halves
explicit: nested helper lambdas previously prevented GCC inlining and lost the
engine-level gain. Regrouped double sums may change last-bit rounding.

Between steps 1 and 1.1, continuously blend the scaled upsampling kernel into
the downsampling kernel to avoid a response jump at unity. Rejection is weaker
in this interval. From steps 1.1 through 256, spectral regression tones test
over 50 dB of rejection at/above destination Nyquist and less than 0.002 RMS
error for unit-amplitude passband tones through 70% of destination Nyquist.
These are tested targets, not guarantees for every signal or boundary.

Support is capped at a 4,096-frame radius (8,193 taps/output); extreme rate ratios
lose rejection. CPU work grows with playback step. No benchmark can guarantee
deadlines on every machine, particularly with large source/host rate differences
and high polyphony. No runtime quality control or new saved-state field is introduced.

## Validation

`resampling` tests analytic tones, unity continuity, DC/bounds on tiny regions,
extreme-rate work limits, import/playback agreement, processed-export parity,
duration and no added output tail. `sampler-core`, `playback-voice`,
`buffer-timing` and `wav-codec` compare against a separate analytic sinc oracle;
timing/ownership and Dirty assertions remain independent.

`storage-rate` checks capped imports/restoration, streamed-versus-batch FIR,
fixed duration, pre-roll/trim metadata and callback allocation/deletion across
high and fractional input rates and callback partitions.

Run `build/anvillesampler_resampling_benchmarks --expect-cap96` in Release.
The [benchmark report](../../SMS-AnvilleSampler/Docs/RESAMPLING-BENCHMARKS.md)
contains configuration, ordinary/extreme workloads, paired results and commands.
Timings have no machine-dependent CTest threshold. Run the allocation/transfer
and state suites plus [host checks](TESTING.md).
