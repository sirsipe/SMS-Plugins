# Pad storage and thread handoff

Audience: agents changing sample transfers, storage, state, or lifecycle.
Implemented by `SamplerEngine.*`, `SampleStoragePool.*`,
`RealtimeCommandDispatcher.hpp`, and adapter `ControlWorker.hpp`.

## Ownership

`SamplerEngine` owns voices, recording, logical capacity, and publication.
`SampleStoragePool` owns PCM, capture maps, and imported allocations. Its worker
prepares capture chunks and mapping pages; audio adopts chunks and maps blocks.
Imported/staged PCM and completed captures are immutable while referenced.

A pad and each control snapshot hold descriptor references. Snapshots capture
metadata, settings, and generation together. Reader release only updates a
lock-free reference count. Serialized control work allocates and collects
imports. Audio reclaims at most 1,024 blocks per call; a descriptor is reused
only after its references and blocks are released. Cleared/replaced blocks
return to the free list; chunks remain owned until teardown.

The default engine starts with 30 seconds of capture blocks at the host rate.
At 15 seconds or less free, audio requests a 60-second refill from the
per-instance worker, in 1,024-frame blocks. Arming alone does not grow storage.
The worker prepares mapping pages too; audio adopts chunks and maps blocks
without allocating. This is on-demand growth. Imports and staging add
control-thread memory proportional to their PCM.

The aggregate ceiling is the largest block-rounded 32-bit frame count (about
4.29 billion), not a memory guarantee. Each pad/codec limit is 184,320,000
frames (about 64 minutes at 48 kHz or 32 at 96 kHz). Positive
`maxRecordSeconds` retains the bounded core pool for tests and clients.

Refill allocation failures latch; prepared blocks remain usable, and retry
occurs on sample-rate reconfiguration or the next Arm. If blocks run out,
recording finalizes its captured PCM and reports that audio was kept. Refills
remain best effort: normal mode adds 60 seconds at 15 seconds free; explicit
offline mode adds 120 seconds at 30 seconds free. Unknown/unsupported signals
use normal mode. See [DPF host signal](DPF-VST3-CONSTRAINTS.md#offline-host-signal).
Offline allocation can still fall behind; playback and monitoring continue.

## Transfer boundary

The dispatcher runs bounded, non-throwing capture and commit callbacks at block
boundaries. The adapter services one command before audio settings and MIDI.
Audio never waits, allocates/frees storage, locks, performs I/O, or makes system
calls. Chunk adoption and mapping use bounded lock-free operations.

Export, Copy, and waveform scans retain storage, then copy or scan outside audio.
Import, Paste, split, and rechop prepare PCM off audio. Commit checks generations
and capacity before replacing descriptors/settings. Pad moves transfer
descriptors; Paste publishes copy-time PCM/settings together.

Recording, clear/reset, PCM, and setting changes bump generations. Stale
replacements and cut Apply fail. Live recording has no published PCM; import
rejects its target while unrelated recording continues.

## Published host snapshots

Host save/open queries must also work while processing is suspended. Each pad
publishes an atomic descriptor pointer and an even/odd publication sequence.
Nested mutations keep that sequence odd until PCM, metadata, and settings agree.
A control reader captures atomics, pins a positive-reference descriptor by CAS,
and validates the sequence before copying any PCM. Failed validation retries
on the control thread. Permanent capture descriptors and serialized imported
collection make candidate-pointer inspection safe; retired descriptors cannot
be resurrected. Multi-pad edit baselines instead use one dispatcher capture.

DPF still saves durable keys separately: this does not introduce a whole-project
transaction across all keys. VST3's initial view transfer can still serialize
DSP-only PCM through its wrapper; the bounded plug-in UI bus never carries PCM.

## Adapter and lifecycle

VST3 queues transient UI workflows on an instance-owned worker with 64 pending
jobs; rejection produces workflow-specific failure status. LV2 executes these
workflows on its required host worker, preserving wrapper state-map ownership.
LV2 publication suppresses the wrapper's synchronous DSP echo so replies cannot
reenter mutation locks or change generations. Durable state decodes synchronously and queues immutable per-pad desired
updates without waiting for a callback. Published getters include pending
updates, so save/UI reads work before processing resumes. The audio owner
consumes at most 64 pending updates at a block boundary, validates aggregate
capacity, and applies PCM/settings together; invalid batches preserve existing
pads and set `stateRestoreFailed()`. Mailbox merging uses CAS so settings cannot
replay PCM that the callback already consumed. This supports VST3 hosts that
activate an instance but suspend processing during state load, as well as
hosts restoring while callbacks continue. `PadWorkflows` owns clipboard/split workflows and their mutation mutex, which the
adapter also uses for durable state and sample-rate reconfiguration.
The UI's `ChopEditorSession` owns coherent cut baselines and Apply readiness.
`EditorSnapshotSession` matches one waveform/settings reply by sequence and pad.
Detail retries reuse the pending sequence; changing selection/range invalidates
it. Split retains its existing planned transaction.

Activation serializes with inactive commands. Inactive commands run inline;
deactivation requires host callback quiescence. Active control callers wait
outside audio. After two seconds without service, only an unclaimed command can
be canceled; a claimed command must finish before its stack context disappears.
Shutdown stops dispatch and joins the worker before engine destruction.

The access gate remains only for sample-rate reconfiguration, which may rebuild
storage after serialized readers finish. Large pad transfers never use it.
Cut-preview MIDI takeover still intentionally stops ordinary voices; that is
separate from transfer continuity.

## Validation

`capture-storage` injects delayed/failed allocations and checks exhaustion recovery
and uninterrupted monitoring/playback.

`sampler-transfer` compares every monitoring/performance frame against an oracle
through concurrent long transfers, checks exact recording onset, retained
capture reuse, generation rejection, full/aggregate capacity, host snapshots,
dispatch cancellation/lifecycle, and callback allocations/deletions. An optional
WAV argument exercises real long audio. Run the complete suite and recorded
VST3/LV2 host checks described in [Testing](TESTING.md).
