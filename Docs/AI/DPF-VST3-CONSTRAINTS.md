# DPF VST3 state and output constraints

Audience: agents changing VST3 communication or audio-port topology. This page
records verified behavior of the pinned DPF revision and approved architectural
direction; it is not a promise of dynamic outputs.

## DSP-to-UI state transport

DPF's VST3 wrapper constructs its plug-in exporter with a null
`updateStateValue` callback. Midichopper can therefore receive UI-to-DSP state,
but a DSP call to `updateStateValue()` cannot return transient data to the UI.
The wrapper logs `updateStateValueCallback (nil)`. DPF's maintainer says this
API was first implemented for LV2; the same limitation is tracked in
[DPF issue #410](https://github.com/DISTRHO/DPF/issues/410).

Midichopper now builds a separate VST3 DPF target with direct instance access.
`publishUiState()` uses DPF's callback where available and sends failed VST3
updates to a per-instance, bounded message bus. The UI reads that bus on idle
and feeds messages through `stateChanged()`. Each view has its own sequence
cursor; the oldest events are dropped if more than 64 messages arrive before a
view reads them. The bus reports that gap; the UI clears pending pad operations,
refreshes the selected waveform, and tells the user to check the pad and retry.
Keys are limited to 47 bytes and values to 8191 bytes. The
largest current reply, a split plan with a 128-bin waveform, is covered by a
bound test. No audio callback uses the bus. LV2 retains separate DSP/UI
binaries and uses its existing DPF callback; VST3 uses a combined controller
so its view receives the same plug-in instance pointer.

Pad selection, Sample Editor entry, MIDI selection, import, clipboard and cut
actions use this return path for waveform, settings and status messages. The
hidden `pad_file_result_event` still provides a small completion signal. In
Carla on Linux at 48 kHz/2048 frames, WAV import displayed its waveform,
editor settings survived pad switching, and Split Sample opened and applied.
A saved 0.5-second WAV restored after removing the plug-in instance and source
file: its waveform and 6102-frame Start value returned, and a MIDI note produced
nonzero output.
VST3 restoration ignores empty transient command keys so defaults do not fire
file, clipboard, chop, or pad-structure actions. Another VST3 host, long files,
and simultaneous views still need integration testing.

Durable state is separate. Pad PCM is marked DSP-only Base64 state, while cut
points and ADSR are normal versioned per-pad state. UI edits reach the DSP and
affect playback. DPF's VST3 component queries these values when the DAW saves
state, so persistence is expected, but a complete VST3
save-close-delete-source-reload test with longer recordings and a DAW project
remains required.
The pinned VST3 wrapper also sends every state key to a newly opened view,
without filtering the DSP-only hint; loaded pad PCM may therefore cross that
initial view path despite the hint. Review this when changing VST3 transport.

Direct instance access is local to one process and cannot serve a remote UI.
If remote VST3 views become a requirement, add a bounded component-to-view
message path in DPF. Keep PCM out of the plug-in message bus and never allocate
in the audio callback.

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
