# Pad context menu

Audience: agents implementing pad interactions. The shared menu foundation and
Clear Pad action are implemented; later actions remain approved direction. It is related to
[issue #10](https://github.com/sirsipe/SMS-Plugins/issues/10), which requests
copy, delete, MIDI assignment, and optional color. The menu is a general pad
action surface; WAV operations are one consumer, not its defining purpose.

## Interaction contract

Right-clicking a visible pad in Play or Sample Editor opens a custom SMS menu
and selects that pad without auditioning it. Arm mode has no context menu
because pad clicks control capture destinations. A click outside, Escape,
opening another pad menu, a mode/bank/layout change, or a successfully mapped
MIDI note-on closes it. A note-off does not close it. Clamp the overlay to the
logical canvas and preserve behavior under UI scaling. Disabled actions remain
visible and cannot be triggered. Consume a pointer event that dismisses the
menu so it cannot activate the control underneath. If the target becomes hidden
or its state changes externally, close the menu or recompute action enablement
before invocation.

Menu entries highlight while the pointer hovers over an enabled action.
Disabled entries may expose a distinct non-interactive hover appearance only if
the shared theme requires it. Motion and pointer-leave handling must repaint
only when the hovered target changes.

Hover is also an early direction for every clickable SMS control. Do not make
the menu's hover implementation a dead end: keep reusable hover identity,
transition, geometry, and themed drawing support in `Common-UI`, separate from
product action handling. Applying hover to all existing controls is not required
for the first context-menu slice, but its architecture must permit that work
without replacing the menu foundation.

## Extensible action model

Product code owns each action's stable identity, label, enabled state, and
invocation. Reusable overlay layout, hit testing, keyboard/pointer dismissal,
disabled-item behavior, hover transitions, and DPF drawing belong in
`Common-UI`. Keep framework-neutral geometry and state outside `Common-UI/DPF`;
the NanoVG adapter belongs inside it. The menu must not own sampler, host,
clipboard, MIDI-mapping, or filesystem responsibilities.

The model must admit Clear/Delete, Copy, Paste, WAV Import/Export, MIDI
assignment, color, separators, and later actions without another menu type or a
large conditional event handler. Do not add abstractions for hypothetical menu
features until an action needs them.

## First implementation slice

The first slice provides one **Clear Pad** action using the engine's single-pad
clear behavior. It is enabled only when the clicked pad contains audio. The
first activation changes the item to **Confirm Clear** for two seconds; the
second queues the clear for the next audio block. The transient
`pad_clear_request` state is an atomic UI-to-DSP command, not saved project
content or a host parameter: its readable value is always neutral `0`. This
preserves released parameter identities and keeps sampler mutation on the audio
boundary.

Maintain coverage for Clear Pad and the menu lifecycle in both Play and Sample
Editor. Verify target selection without audition, pad layouts and banks,
disabled entries, hover transitions, dismissal by mapped MIDI playback and
other interactions, canvas-edge placement, resizing, and UI scaling. WAV
actions then follow the separate
[pad WAV contract](PAD-WAV-IO.md).
