# Pad context menu

Audience: agents changing pad interactions. The shared menu and actions below
are implemented. [Issue #10](https://github.com/sirsipe/SMS-Plugins/issues/10)
also requests MIDI assignment and optional color. This is a general pad action
surface, not a WAV-specific menu.

## Interaction contract

Right-clicking a visible pad in Play or Sample Editor opens a custom SMS menu
and selects that pad without auditioning it. Actions are grouped with compact,
non-interactive separators; unavailable actions remain visible and disabled.
Arm mode has no context menu because pad clicks choose capture destinations. A
click outside, Escape, another pad menu, a mode/bank/layout change, or a mapped
MIDI note-on closes it; note-off does not. Clamp it to the logical canvas under
UI scaling. A dismissing pointer event must not activate the control underneath.
If target state changes externally, close the menu or recompute enablement.

Enabled actions highlight on hover. Repaint only when the hovered target
changes; disabled entries stay non-interactive.

Keep reusable hover identity, transitions, geometry, and themed drawing in
`Common-UI`, separate from product actions, so other SMS controls can reuse it.

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

## Implemented actions

**Edit Sample** is the first action. In Play it is enabled only for an occupied
target and opens the Sample Editor focused on that pad. It remains visible but
disabled when the menu is opened inside the Sample Editor.

**Clear Pad** uses the engine's single-pad
clear behavior. It is enabled only when the clicked pad contains audio. The
first activation changes the item to **Confirm Clear** for two seconds; the
second queues the clear for the next audio block. The transient
`pad_clear_request` state is an atomic UI-to-DSP command, not saved project
content or a host parameter: its readable value is always neutral `0`. This
preserves released parameter identities and keeps sampler mutation on the audio
boundary.

WAV Import and Export follow the separate [pad WAV contract](PAD-WAV-IO.md).
Maintain coverage for every action and the menu lifecycle in both Play and Sample
Editor. Verify target selection without audition, pad layouts and banks,
disabled entries, hover transitions, dismissal by mapped MIDI playback and
other interactions, canvas-edge placement, resizing, and UI scaling.

**Copy Pad** is enabled only for an occupied target. It takes a private snapshot
of the pad's stereo sample, source sample rate, and all current non-destructive
region and ADSR settings. Later edits or clearing the source do not change that
snapshot. A failed Copy, including an empty source, leaves the previous snapshot
available.

**Paste Pad** is enabled whenever that snapshot exists, including on an empty
target. It immediately replaces the target's audio and region/ADSR settings with
the snapshot; pasting onto an occupied pad has no confirmation step. Copy and
Paste use an internal clipboard owned by one plug-in instance. It is not the OS
clipboard, cannot cross plug-in instances, is not part of saved DAW state, and
is lost when the instance is destroyed.

The snapshot and replacement run outside the audio callback under the existing
real-time access gate. The callback emits silence while access is granted; it
never allocates, blocks, locks, or copies sample data. Keep clipboard storage and
mutation framework-neutral in `src/core`; DPF state and output parameters carry
only bounded commands and completion/availability signals. Paste publishes fresh
editor and waveform state through the same path as WAV Import. The
[VST3 UI message bus](DPF-VST3-CONSTRAINTS.md) carries these replies where DPF's
`updateStateValue()` callback is absent.

**Collapse Gap** is enabled on an empty visible pad when an occupied pad follows
in the current bank/page. **Confirm Collapse** finds the complete containing gap
and shifts the next contiguous occupied run left into it, stopping at the next
empty slot. All audio metadata and settings move; vacated slots are empty. It
never crosses the visible boundary or consumes hidden slots.

**Split Sample...** is enabled on an occupied pad when a later empty visible
slot exists. The DSP prepares a non-mutating, generation-stamped plan through
the nearest empty slot. The editor shows two virtual halves and one midpoint
split. Apply revalidates the plan, shifts intervening pads right, and writes
both halves atomically. The halves reset Start/End and ADSR and inherit mixer
settings; shifted pads retain everything. Exit, Escape, or failure changes no
pads. Navigation cancels the plan before opening the neighboring cut window.

`pad_structure_request/status` carries transient commands outside the audio
callback under the real-time access gate. Generations cover audio and settings
edits. The commands are neither saved state nor host parameters.
