# DPF VST3 state and output constraints

Audience: agents changing VST3 communication or audio-port topology. This page
records verified behavior of the pinned DPF revision and approved architectural
direction; it is not a promise of dynamic outputs.

## DSP-to-UI state transport

DPF's VST3 wrapper constructs its plug-in exporter with a null
`updateStateValue` callback. Anvil Sampler can therefore receive UI-to-DSP state,
but a DSP call to `updateStateValue()` cannot return transient data to the UI.
The wrapper logs `updateStateValueCallback (nil)`. DPF's maintainer says this
API was first implemented for LV2; the same limitation is tracked in
[DPF issue #410](https://github.com/DISTRHO/DPF/issues/410).

Anvil Sampler now builds a separate VST3 DPF target with direct instance access.
`publishUiState()` uses DPF's callback where available and sends failed VST3
updates to a per-instance, bounded message bus. The UI reads that bus on idle
and feeds messages through `stateChanged()`. Each view has its own sequence
cursor; the oldest events are dropped if more than 64 messages arrive before a
view reads them. The bus reports that gap; the UI clears pending pad operations,
refreshes the selected waveform, and tells the user to check the pad and retry.
Keys are limited to 47 bytes and values to 8191 bytes. The
three-waveform coherent cut baseline and split-plan replies are covered by
bound tests. No audio callback uses the bus. LV2 retains separate DSP/UI
binaries and uses its existing DPF callback; VST3 uses a combined controller
so its view receives the same plug-in instance pointer.

Capture buttons use `capture_action_request` (`finalize`, `undo`, `clear`). The
pinned VST3 `parameter-set` handler skips trigger writes. The adapter queues
commands atomically for the next block, sharing command
bits. Parameter IDs stay fixed; empty restored values are inert. Clear requires
confirmation.

Selection, editors, import, clipboard, and cut actions use this return path
for waveform/settings/status. The
hidden `pad_file_result_event` still provides a small completion signal. In
Carla on Linux at 48 kHz/2048 frames, WAV import displayed its waveform,
editor settings survived pad switching, and Split Sample opened and applied.
VST3 restoration ignores empty transient command keys so defaults do not fire
file, clipboard, chop, or pad-structure actions. The maintainer reports successful
VST3 preset exchange in both directions between Ardour and REAPER.

Durable state is separate. Pad PCM is marked DSP-only Base64 state, while cut
points and ADSR are normal versioned per-pad state. UI edits reach the DSP and
affect playback. DPF's VST3 component queries these values when the DAW saves.
A 64-second stereo song restored in Carla after removing the source copy and
plug-in instance; playback, waveform, edited Start and gain returned. Recorded
VST3 and LV2 import/export/save checks at 48 kHz/2048 frames preserved continuous
monitoring and an unrelated repeating pad with no zero-output frames or xruns.
VST3 Copy/Paste, split, and cut Apply also passed UI checks.

An earlier Carla chunk-loading check observed about 0.47 seconds of silence
without identifying a plugin cause. The maintainer reports that
[issue #18](https://github.com/sirsipe/SMS-Plugins/issues/18) no longer reproduces.
Treat that observation as historical, not a confirmed current preset defect.
Codec regression does not exercise DAW project restoration or simultaneous views;
manual host evidence matters. Public compatibility begins with v1.0.0 under the
[release policy](../RELEASING.md#public-version-compatibility).
The pinned VST3 wrapper also sends every state key to a newly opened view,
without filtering the DSP-only hint; loaded pad PCM may therefore cross that
initial view path despite the hint. Review this when changing VST3 transport.

Direct instance access is local to one process and cannot serve a remote UI.
If remote VST3 views become a requirement, add a bounded component-to-view
message path in DPF. Keep PCM out of the plug-in message bus and never allocate
in the audio callback. VST3 transient UI transfers run on an instance-owned
worker; LV2 uses its host worker. Both share engine snapshots and commits. Synchronous VST3 save/open queries read retained
published pad snapshots without requiring another audio callback; see
[Storage handoff](PAD-STORAGE.md).

## Multi-output topology

VST3 itself supports multiple audio buses, host bus activation, and notifying a
host of a changed bus configuration or count through
`restartComponent(kIoChanged)`. Plug-in-requested bus activation is an optional
host interface. These mechanisms run outside audio processing and hosts may
interrupt and rebuild their graphs.

The pinned DPF API is more restrictive. `DISTRHO_PLUGIN_NUM_OUTPUTS` fixes the
channel count at compile time; `initAudioPort()` and port groups describe that
fixed topology. Its VST3 wrapper maps fixed groups to buses and accepts host
activation, but DPF exposes no plug-in API for creating buses at runtime,
requesting activation, or issuing `kIoChanged`. DPF's
[feature table](../../third_party/DPF/FEATURES.md) confirms VST3 audio-port
groups; the pinned source initializes them from the compile-time port list.

LV2 ports are also fixed when the plug-in is discovered. This does not prevent
individual outputs: both LV2 and current DPF VST3 can expose a predetermined
set of stereo buses. Literal stereo output for all 64 pads would require 128
pad channels, plus any main output, and needs host-limit, routing-UI, CPU,
project-compatibility, and saved-connection testing before approval.

Prefer a format-independent engine routing model with pads assigned to a fixed,
product-defined number of stereo output buses. Choose that number from concrete
workflows and host tests; do not assume 8, 16, or 64 yet. A separate multi-out
plug-in variant may preserve the released stereo topology better than changing
the existing identity. Truly dynamic VST3 buses require a scoped DPF extension
and a cross-host acceptance matrix. Treat that work separately from repairing
DSP-to-UI state transport even if both modify the VST3 wrapper.

VST3 references: Steinberg's
[component interface](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IComponent.html),
[`kIoChanged`](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/namespaceSteinberg_1_1Vst.html),
and [bus-activation request](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Change%2BHistory/3.6.8/IComponentHandlerBusActivation.html).
