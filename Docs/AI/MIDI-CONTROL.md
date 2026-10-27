# MIDI controllers and host automation

Audience: agents changing MIDI control, parameter metadata, or host integration.
The [manual](../../SMS-AnvilleSampler/README.md#midi-control-and-automation)
owns the supported learning workflow: use the DAW's MIDI Learn.

## Native learning experiment

The local `feature/midi-learn` branch retains an unfinished VST3-only experiment
for possible future development. It adds global-control menus and a tracked DPF
patch exposing `IMidiLearn` and custom `IMidiMapping` assignments. Main does not
include this implementation.

Learning and controller-driven knob movement worked, but maintainer testing in
REAPER, Ardour, and Carla did not produce a satisfactory automation workflow.
Use those hosts' learning facilities so controller gestures follow their own
automation paths. This is the project's integration decision, not a claim that
native VST3 learning cannot work in any host.

Receiving a parameter value is different from recording a gesture. Mouse edits
notify the host through `beginEdit`, `performEdit`, and `endEdit`. In the
[Ardour implementation inspected](https://github.com/Ardour/ardour/blob/master/libs/ardour/vst3_plugin.cc),
mapped CC input updates VST3 processing and cached values without the equivalent
automation-control/touch notifications. The plugin knob can move while Ardour's
automation control or recording does not follow. Tests of mapping interfaces,
codecs, or visible knob movement do not establish automation recording support.
Do not echo all incoming parameter changes as edits: that would also echo host
automation playback and can create feedback or overwrite recorded automation.

Any resumed native-learning work needs host tests for live CC input, playback
of recorded CC, parameter-envelope recording in write/touch modes, automation
playback, closed plugin views, and save/restore. Keep host/version-specific
outcomes distinct; do not infer REAPER recording support from Ardour or Carla.

## Ardour pickup rounding

Ardour's [Generic MIDI pickup check](https://github.com/Ardour/ardour/blob/master/libs/surfaces/generic_midi/midicontrollable.cc)
uses exact comparisons against the current parameter value. Its float cache
and DPF's normalized/plain conversion can introduce tiny differences for TUNE's
native -24..+24 range, rejecting subsequent controller movement. A numerical
reproduction using the pinned DPF conversion stalled a center-out TUNE sweep
after one step; PAN (-1..+1) and the filters (0..1) traversed their full ranges.
The maintainer confirmed that enabling Generic MIDI's Motorised setting
restored TUNE movement. It bypasses pickup and allows value jumps, even with a
nonmotorised controller. This is separate from native-learning automation.

## Hidden parameter metadata

DPF is pinned to `4238e1c7f0351bbe488d79f0899c540543ac7583`.
Its VST3 and CLAP wrappers ignore `kParameterIsHidden` on plugin parameters;
LV2 exports `notOnGUI` correctly. Fourteen sampler parameters carry the hint:
twelve output meters/status/playhead signals, plus the writable internal
Capture Target Request and Stop All Playback commands. They may appear in host
parameter lists. The common automatable hint also reaches those two inputs.

Upstream `develop` contains [fix 906bbe4](https://github.com/DISTRHO/DPF/commit/906bbe4cee6a1d816b9105631c7159fb1481db98),
adding three lines per affected format; upstream `main` remains at our pin at
the time of this investigation. No backport is applied. A future fix must retain
parameter IDs and verify the two writable command paths: VST3's
[hidden flag](https://github.com/steinbergmedia/vst3_pluginterfaces/blob/master/vst/ivsteditcontroller.h)
also implies external read-only/nonautomatable semantics.

DPF separately prepends 16 x 130 MIDI proxy parameters for CC, channel pressure,
and pitch bend. Those 2,080 entries already carry VST3's hidden flag; the missing
plugin-hint mapping does not explain their appearance in REAPER's lists.
