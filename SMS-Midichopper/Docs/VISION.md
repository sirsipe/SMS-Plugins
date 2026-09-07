# SMS-Midichopper vision

SMS-Midichopper is a hands-on chopping sampler shaped around real sampling and
beatmaking workflows.

It makes live capture and performance immediate:

**Capture or load → chop → shape → perform**

Live, gesture-driven chopping is the starting point and most distinctive
workflow. A performer can capture an incoming stereo source and create playable
slices by tapping their boundaries on a MIDI controller. Recorded audio and
loaded files then follow the same focused chopping workflow.

SMS-Midichopper can grow into a flexible production instrument through global
and per-pad editing, configurable controller layouts, sound-shaping controls,
and optional multi-output routing. Basic chopping must always remain
approachable. Detailed production features belong in focused editor views or
optional plug-in configurations where they do not obstruct capturing and
playing.

Development is guided by practical user workflows and experimentation. A
feature belongs in SMS-Midichopper when it meaningfully improves sampling,
chopping, shaping, beatmaking, or performance while preserving a coherent user
experience. Features should not be added merely because another sampler has
them.

## Product principles

- Keep live capture, chopping, and pad performance fast and immediately
  understandable.
- Support both whole-recording slice adjustment and detailed, non-destructive
  per-pad editing.
- Keep audio sources, slices, pad assignments, visual layouts, and MIDI mappings
  as separate concepts so each can evolve without restricting the others.
- Put advanced controls in dedicated views instead of crowding the primary
  workflow.
- Prefer reliable project restoration, predictable MIDI behaviour, and
  real-time-safe audio processing over feature count.
- Let demonstrated user workflows guide priorities, even when they expand the
  instrument beyond its original scope.

## Scope

The intended scope includes live capture, audio-file loading, manual and
editable chopping, multiple pad banks, flexible MIDI mapping and controller
layouts, global and per-pad editing, essential playback and sound-shaping
controls, voice and choke-group management, and optional multi-output routing.

Independent time-stretching, additional sound-shaping tools, and other advanced
features may be added when they serve a concrete workflow and can be integrated
without weakening the basic experience.

SMS-Midichopper is not intended to become a DAW, a full mixer, an internal
sequencer or groove workstation, a synthesizer, or a sample-library database.
Those boundaries may be reconsidered only when real user workflows provide a
compelling reason.

## Feature requests

Every feature request must fit this vision. Requests that do not meaningfully
support the defined workflows and principles will be rejected. Features that do
fit may still be deferred when they depend on unfinished foundations, require
further design, or would compromise reliability.
