# Resampling processing benchmarks

Measured 2026-11-02: baseline `dd8014c` versus the 96 kHz storage cap and
coefficient-reuse/stereo-convolution optimization. Filter coefficients are
unchanged. [Storage behavior](WAV-FILES.md#stored-sample-rates)
explains conversion and project compatibility.

## Configuration

AMD Ryzen 7 5700X, Linux x86-64, GCC 13.3, Release `-O3 -DNDEBUG`, C++20,
PIC core, default CPU target; no fast-math or `-march=native`. Pinned to CPU 15;
no cgroup CPU quota. These are shared-machine wall-clock measurements, including
scheduling noise, not exclusive-core or hardware-device deadline guarantees.

The benchmark calls the actual engine, including storage reads, voice processing
and mixing. Voices remain active; outputs are finite.
Sources contain 65,536 stereo frames of deterministic sine/cosine mixtures;
voices hold-loop the complete sample with unity sustain. Effects and monitoring
are off. Captured-source rows record first at the input rate, then play at the
listed host rate. Import, recording/conversion, setup and waveform/state work
are excluded from timing. The retrigger row includes 16 note-ons every callback.

Each case has 16 warm-up and 200 measured callbacks. Three before/after pairs
alternate run order, giving 600 measured callbacks per build/case. The table
shows the median of the three medians and the largest run p95; it does not pool
percentiles. [Raw CSV](benchmarks/resampling-2026-11-02.csv) includes actual stored
rates/frames, per-run medians, p95, peaks, deadline counts and checksums.

## Ordinary and extreme workloads

**R** means a reasonable everyday workload, not a usage recommendation.
**X** is an extreme worst-case stress configuration: all 16 voices, the highest
stored rate, and maximum combined +48-semitone tuning (pad +24, global +24:
four octaves up). It is deliberately far beyond ordinary playback. These rows
stress particular configurations; they are not proof of an absolute worst case
across every host rate, source, effect, buffer size and machine.

Host rate is 48 kHz except the 44.1k-host row. Input→stored shows the new cap;
before this change, the stored rate equaled the input rate. Tune is total
semitones. The 128-frame/48 kHz deadline is **2.667 ms**; 64 frames allow 1.333 ms,
512 frames 10.667 ms, and 128 frames at 44.1 kHz 2.902 ms. Lower times are better.

| Workload | Input→stored kHz | Voices / tune | Frames | Before median / p95 ms | After median / p95 ms | After late / 600 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| R: Native | 48→48 | 4 / 0 | 128 | 0.008 / 0.008 | 0.011 / 0.011 | 0 |
| R: Native | 48→48 | 8 / 0 | 128 | 0.014 / 0.014 | 0.019 / 0.019 | 0 |
| R: 44.1k import | 44.1→44.1 | 4 / 0 | 128 | 0.051 / 0.101 | 0.054 / 0.100 | 0 |
| R: 96k import | 96→96 | 8 / 0 | 128 | 0.140 / 0.173 | 0.079 / 0.144 | 0 |
| R: 96k octave up | 96→96 | 8 / 12 | 128 | 0.255 / 0.388 | 0.131 / 0.242 | 0 |
| R: 96k fractional up | 96→96 | 8 / 7.3 | 128 | 0.198 / 0.236 | 0.195 / 0.231 | 0 |
| R: 48k captured | 48→48 | 8 / 0 | 128 | 0.015 / 0.016 | 0.020 / 0.039 | 0 |
| X: 96k maximum | 96→96 | 16 / 48 | 128 | 3.875 / 4.797 | 1.723 / 2.463 | 3 |
| X: 192k import | 192→96 | 16 / 48 | 128 | 7.609 / 8.606 | 1.726 / 2.552 | 6 |
| X: 384k import | 384→96 | 16 / 48 | 128 | 15.871 / 16.287 | 1.734 / 2.448 | 2 |
| X: 192k captured | 192→96 | 16 / 48 | 128 | 8.528 / 9.479 | 1.744 / 2.457 | 3 |
| X: 384k captured | 384→96 | 16 / 48 | 128 | 17.488 / 18.166 | 1.756 / 2.461 | 5 |
| X: 44.1k host | 96→96 | 16 / 48 | 128 | 4.126 / 5.188 | 3.865 / 4.791 | 600 |
| X: 64-frame block | 96→96 | 16 / 48 | 64 | 1.939 / 2.753 | 0.857 / 1.211 | 14 |
| X: 512-frame block | 96→96 | 16 / 48 | 512 | 15.070 / 16.343 | 6.963 / 7.988 | 0 |
| X: Retrigger all voices | 96→96 | 16 / 48 | 128 | 3.784 / 4.681 | 1.743 / 2.438 | 2 |

The ordinary converted cases improve substantially; native playback is about
3–5 microseconds slower, still far below budget. Maximum 48 kHz-host workloads
now usually fit, but measured outliers remain. At 44.1 kHz with maximum tuning,
the fractional playback ratio misses **all 600 deadlines** after the change.
The aligned-coefficient optimization does not cover arbitrary ratios. The cap
and optimization therefore improve headroom without guaranteeing every supported
configuration. CPU load from other plugins reduces the remaining headroom.

## Reproduction and validation

Build with testing enabled using the [development commands](../../Docs/AI/DEVELOPMENT.md).
The benchmark is a separate executable, not a timing-based CTest gate:

```sh
./build/anvillesampler_resampling_benchmarks --expect-cap96 --iterations 200 --warmups 16
```

For a paired comparison, build the same
[benchmark source](../tests/resampling_benchmarks.cpp) against both the baseline
core and the changed core, using identical compiler/optimization flags. Then:

```sh
python3 SMS-AnvilleSampler/tests/benchmark_resampling.py \
  --before /path/to/before-benchmark --after build/anvillesampler_resampling_benchmarks \
  --cpu 15 --iterations 200 --warmups 16 --rounds 3 --output results.csv
```

Choose a CPU available on the test machine. The script alternates execution and
checks the 96 kHz cap in the changed build. CSV `step` is actual stored rate / host
rate × `2^(total_tune/12)`; setup also verifies conversion preserves duration.

All 41 CTest suites passed, including allocation/transfer, state and new storage-rate
coverage. Resampling/storage-rate suites passed ASan/UBSan. A native LV2 host harness
also captured three sample-offset slices at 48/192/384 kHz with 128-frame blocks,
verified unchanged monitoring, and saved the expected 48/96/96 kHz PCM and frame
counts through the LV2 state interface. LV2 discovery passed. No UI behavior was
changed; no new interactive UI or Windows/macOS host validation was performed.
