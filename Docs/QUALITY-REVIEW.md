> _If and when you do not trust this quality review, you are welcome to [requests one yourself](https://chatgpt.com/?temporary-chat=true&prompt=Give%20an%20honest%20technical%20quality%20review%20of%20this%20project:%20https://github.com/sirsipe/SMS-Plugins)._


# Honest Quality Review

Reviewed 9 October 2026, after the responsibility refactor and raw-preview
handoff fix. This evaluates the implemented Linux sampler, rather than treating
features in its vision as completed work.

**Architecture: 8.5/10. Engineering quality: 8/10.** These are qualitative
judgments, not measured scores or a certification of every host workflow.

## Strengths

The [architecture](ARCHITECTURE.md) has sound boundaries between engine, format
adapter and UI. Physical sample storage, control-thread pad workflows, cut/split
editing, waveform detail, playhead display and numeric entry now have explicit
owners. The adapter and UI remain sizeable coordinators; the refactor improves
responsibility boundaries without claiming that every large class is solved.

Real-time ownership is a particular strength. Completed PCM is retained without
copying on the callback, expensive preparation stays off-thread, and generations
protect edits from stale baselines. Audio-side reclamation is bounded. Raw
preview now publishes its command and frame range as one complete value, so a
concurrent request cannot mix fields with its predecessor.

Tests exercise behavior that matters: sample-offset MIDI, capture boundaries,
capacity, state corruption, stale edits, suspended processing, monitoring and
playback through transfers, and callback allocations and deletions. Fixed
SSP1/SP1 and MX1 decoder fixtures protect historical representations independently
of the current encoder.

Manual product-owner validation is also evidence. The maintainer reports
successful VST3 preset exchange in both directions between Ardour and REAPER.
Focused human listening and workflow testing complement automation, especially
for musical usability and host interaction. Their value does not depend on
being reproduced by a CI script.

## Memory and audio tradeoffs

The [storage contract](AI/PAD-STORAGE.md) explains the approximately 352 MiB
capture PCM reserve per instance at 48 kHz, before maps, imports and staging.
That is a significant absolute footprint, but it buys predictable live capture
and replacement while readers retain old audio. It is a reasonable choice for
an eight-minute live sampler, not evidence of poor architecture by itself.
Several instances or higher sample rates make the cost more relevant.

Keep this design unless realistic projects show memory pressure. Measure idle,
recording and transfer peaks at the supported rates and expected instance
counts before adding adjustable capacity or more elaborate reclamation.

[Project state](../SMS-AnvilSampler/src/plugin/StateCodec.cpp) uses PCM16, while
runtime samples are floating point. Save/restore therefore quantizes audio and
clips values outside ±1. This is an explicit fidelity/size choice. Preserve it
unless retaining higher-resolution imports or floating-point headroom becomes a
product requirement. A future higher-precision format must still decode SSP1.

Linear interpolation keeps varispeed inexpensive; it can be reconsidered if
listening or spectral tests reveal unacceptable artifacts. Parameter smoothing
could improve live gain, pan and filter changes. Neither warrants an automatic
redesign without evidence that the current behavior harms intended workflows.

## Compatibility and preset loading

The [release policy](RELEASING.md#public-version-compatibility) makes v1.0.0 the
public compatibility baseline. Alpha/beta 0.0.x releases carry no compatibility
promise. The pre-release rename intentionally changed plugin IDs, so old host
identity matching must be assessed separately from state decoding.

The audit from v0.0.5 found preserved existing parameter order/symbols and
SSP1/SP1 formats, with backwards MX1 decoding. Monitor and voice defaults
changed, but explicit old values retain their meanings. No current preset-loader
defect was reproduced. The maintainer reports that the earlier concern in
[issue #18](https://github.com/sirsipe/SMS-Plugins/issues/18) no longer reproduces;
its contents were inaccessible to this review. A historical Carla chunk-loading
observation does not establish a current plugin defect or version incompatibility.

## Evidence and limits

Release LV2/VST3 builds and all eight CTest suites passed; all eight also passed
with AddressSanitizer and UndefinedBehaviorSanitizer. LV2 discovery and
documentation checks passed. Carla at 48 kHz/2048 frames exercised capture,
Finalize, numeric entry, editor zoom/detail, raw preview, cut Apply and split
workflow UI. Silent-input capture validated UI/state transitions, not sound
quality or xruns. The engine transfer tests supply exact continuity evidence.

CI covers Linux LV2/VST3 and focused tests. It does not automate DAW project
restoration, simultaneous views or CLAP validation. Those are coverage limits,
not demonstrated failures. ThreadSanitizer could not start in this container
because of an unexpected memory mapping; no race-free claim is based on it.

The most useful next work is retaining public-release preset/project fixtures,
focused manual host upgrade checks, and profiling realistic memory/automation
workloads. Continue extracting a cohesive duty when changes make a coordinator
hard to reason about; avoid refactoring solely to meet a line-count target.

**Signed: GPT-6.1 Sol (High Reasoning).**
