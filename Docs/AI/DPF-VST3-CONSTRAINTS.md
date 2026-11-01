# DPF VST3 state and bus constraints

Audience: agents changing DPF transport, parameters, or audio ports.

## Offline host signal

DPF `isOffline()` reports VST3 `V3_OFFLINE`; `V3_PREFETCH` uses normal allocation.
LV2 appends optional `lv2_freewheel` at port 78. Missing or unsupported signals
are false. Ports 6–77 and parameter IDs stay fixed. See
[Development](DEVELOPMENT.md#pinned-dpf-patch) and [storage handoff](PAD-STORAGE.md).

## Ardour automation limit

Ardour 8.4 defaults `limit-n-automatables` to 512. DPF's 2,080 hidden MIDI
parameters exhaust this quota before Mode. For automated host tests, set
`ARDOUR.config():set_limit_n_automatables(0)` before instantiation; zero means
unlimited. Recreate existing instances. See [Ardour's limit enforcement](https://github.com/Ardour/ardour/blob/8.4/libs/ardour/plugin_insert.cc#L553).

## DSP-to-UI state transport

[Startup preferences](LAYOUT-PREFERENCE.md) documents parameter-cache initialization.

DPF's VST3 wrapper constructs its plug-in exporter with a null
`updateStateValue` callback. Anville Sampler can therefore receive UI-to-DSP state,
but a DSP call to `updateStateValue()` cannot return transient data to the UI.
The wrapper logs `updateStateValueCallback (nil)`. DPF's maintainer says this
API was first implemented for LV2; the same limitation is tracked in
[DPF issue #410](https://github.com/DISTRHO/DPF/issues/410).

Anville Sampler now builds a separate VST3 DPF target with direct instance access.
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
Carla checks at 48 kHz/2048 frames covered 64-second song restoration without
the source, preserved playback/waveform/edits, VST3/LV2 import/export/save during
continuous monitoring and repeating-pad playback, and VST3 clipboard/split/cut UI actions.

Codec regression does not exercise DAW project restoration or simultaneous views;
manual host evidence matters. Public compatibility begins with v1.0.0 under the
[release policy](../RELEASING.md#public-version-compatibility).
The maintained DPF patch skips DSP-only state getters during editor initialization
and filters every DSP-to-UI state send, including restoration with an editor open.
Host save/restore still includes all durable pad PCM. `vst3-state-transport` tests
the real wrapper with a headless fixture: initial/reopened views, cached DSP-only
blobs, UI-visible state, and complete host state round trips.

Direct instance access is local to one process and cannot serve a remote UI.
If remote VST3 views become a requirement, add a bounded component-to-view
message path in DPF. Keep PCM out of the plug-in message bus and never allocate
in the audio callback. VST3 transient UI transfers run on an instance-owned
worker; LV2 uses its host worker. Both share engine snapshots and commits. Synchronous VST3 save/open queries read retained
published pad snapshots without requiring another audio callback; see
[Storage handoff](PAD-STORAGE.md).

## Stereo sidechain input

The VST3 target defines `ANVILLESAMPLER_VST3` for shared DSP sources as well as
the wrapper. `initAudioPort()` marks both existing stereo inputs with
`kAudioPortIsSidechain`; DPF exports one `kAux` input bus, inactive until the
host enables it. Outputs remain main; LV2/CLAP metadata is unchanged.
This supports Cubase's instrument-sidechain routing. Native Cubase verification
remains pending: check routing, stereo capture, deactivation/reactivation and
project restoration, plus existing REAPER/Ardour connections. [Port tests](TESTING.md)
check the format-specific hints, grouping and symbols.

## Multi-output topology

VST3 supports multiple audio buses, host activation, and bus-configuration/count
notifications through `restartComponent(kIoChanged)`. Plug-in-requested activation
is optional. These operations run outside processing; hosts may rebuild their graphs.

DPF fixes channel counts through `DISTRHO_PLUGIN_NUM_OUTPUTS`; `initAudioPort()`
and groups describe this topology. Its VST3 wrapper maps groups to buses and
accepts host activation, but exposes no plug-in API for runtime bus creation,
activation requests, or `kIoChanged`. See its
[feature table](../../third_party/DPF/FEATURES.md).

LV2 ports are fixed at discovery too. Both formats can expose predetermined
stereo buses. All 64 pads individually require 128 channels plus main outputs;
test host limits, routing UI, CPU, project compatibility and saved connections
before approval.

Prefer format-independent engine routing to a fixed bus count chosen from
workflows and host tests. A separate multi-out variant may preserve released
stereo topology. Dynamic VST3 buses require a scoped DPF extension and cross-host
acceptance, separate from DSP-to-UI transport repairs.

VST3 references: Steinberg's
[component interface](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/classSteinberg_1_1Vst_1_1IComponent.html),
[`kIoChanged`](https://steinbergmedia.github.io/vst3_doc/vstinterfaces/namespaceSteinberg_1_1Vst.html),
and [bus-activation request](https://steinbergmedia.github.io/vst3_dev_portal/pages/Technical%2BDocumentation/Change%2BHistory/3.6.8/IComponentHandlerBusActivation.html).
