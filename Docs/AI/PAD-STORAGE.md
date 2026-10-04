# Pad storage and thread handoff

Audience: agents changing sample transfers, storage, state, or lifecycle.
Implemented by `SamplerEngine.*`, `RealtimeCommandDispatcher.hpp`, and the
adapter's `ControlWorker.hpp`. Parameter identities and PCM/settings codecs
are unchanged; cut-baseline transport adds only transient states.

## Ownership

The audio thread owns pad voices, recording, logical capacity, capture block
allocation, and descriptor changes. Imported/staged PCM is immutable. Capture
uses preallocated descriptors and block mappings; completed captures remain
immutable until their descriptor has no references.

A pad holds one descriptor reference. A control snapshot retains that descriptor
and captures metadata, settings, and generation together. Reader release only
updates a lock-free reference count. Imported allocation and destruction belong
to serialized control work; `collectImportedStorage()` deletes zero-reference
objects. Audio-side reclamation returns at most 1,024 retired capture blocks per
process call. A descriptor cannot be reused until its blocks are returned and a
zero-to-one reference CAS succeeds.

Logical shared capacity remains about eight minutes at the host rate. The
physical capture pool reserves twice that capacity so a retained old capture
can coexist with replacement recording. At 48 kHz the capture PCM reserve is
about 352 MiB, plus maps and pre-roll. Immutable imports and staging consume
additional control-thread memory proportional to their PCM. Reserve memory is
not extra user-visible capacity. Imports validate aggregate logical capacity
at commit, including replacement credit, so replacing a full pad does not
require another pad-sized allocation inside the logical pool.

## Transfer boundary

The engine's installed dispatcher runs only bounded, non-throwing capture and
commit callbacks at a block boundary. The adapter services one pending command
before applying audio settings and processing MIDI. Audio never waits for a
control caller, copies whole samples, locks, performs I/O, or destroys storage.

Export, clipboard Copy, and waveform scans retain completed storage, then copy
or scan outside the callback. Import, Paste, split, and rechop validate and
prepare PCM outside the callback. Commit checks affected generations and
capacity before replacing descriptors and complete settings. Pad moves transfer
descriptors without scanning sample-length block maps. Paste publishes its
copy-time PCM and settings together.

Recording start/completion, clear/reset, PCM changes, and setting changes bump
pad generations. A replacement prepared before a conflicting change fails;
ordinary cut Apply also checks the displayed baseline generations. A live
recording has no published PCM and is not exported as a mutable partial sample.
Import rejects a recording target; unrelated recording continues.

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
hosts restoring while callbacks continue. The adapter serializes pad workflows and sample-rate reconfiguration.
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

`sampler-transfer` compares every monitoring/performance frame against an oracle
through concurrent long transfers, checks exact recording onset, retained
capture reuse, generation rejection, full/aggregate capacity, host snapshots,
dispatch cancellation/lifecycle, and callback allocations/deletions. An optional
WAV argument exercises real long audio. Run the complete suite and recorded
VST3/LV2 host checks described in [Testing](TESTING.md).
