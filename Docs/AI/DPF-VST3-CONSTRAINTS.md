# DPF VST3 state and output constraints

Audience: agents changing VST3 communication or audio-port topology. This page
records verified behavior of the pinned DPF revision and approved architectural
direction; it is not a promise of dynamic outputs.

## DSP-to-UI state limitation

DPF's VST3 wrapper constructs its plug-in exporter with a null
`updateStateValue` callback. Midichopper can therefore receive UI-to-DSP state,
but a DSP call to `updateStateValue()` cannot return transient data to the UI.
The wrapper logs `updateStateValueCallback (nil)`.

Pad selection, Sample Editor entry, MIDI selection, and import completion all
request a fresh waveform and editor snapshot. The DSP generates them, but VST3
drops the reply. Closing and reopening the UI or reselecting the pad only
repeats this failed exchange. The hidden `pad_file_result_event` confirms that
a file action finished; it cannot carry a waveform or six editor values.

Durable state is separate. Pad PCM is embedded as DSP-only Base64 state, while
cut points and ADSR are normal versioned per-pad state. UI edits reach the DSP
and affect playback. DPF's VST3 component queries these values when the DAW
saves state, so persistence is expected, but a complete VST3
save-close-delete-source-reload test remains required. The current UI cannot
reliably reload editor values after switching pads or reopening. It may show
defaults while the DSP still uses saved values; committing another edit can
then overwrite those values.

A complete fix needs a bounded DSP-to-controller-to-view message path in DPF,
with no PCM transfer and no audio-thread allocation. An import-only UI waveform
cache would improve immediate feedback but would not cover captured audio,
project restoration, another UI instance, or editor-state resynchronization.
Keep the primary LV2 workflow as the supported reference until this is fixed
and host-tested.

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
