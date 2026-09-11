# SMS plug-in shared source

Audience: AI agents. Read when changing shared DSP, codecs, or geometry.

This directory contains dependency-light C++20 components that are reusable by
multiple SMS audio plug-ins. It deliberately has no dependency on DPF, LV2, a
host, or a specific plug-in's state model.

- `DSP/SamplePlaybackSettings.hpp`: sanitized, non-destructive sample region
  and ADSR value type.
- `DSP/AdsrEnvelope.hpp`: allocation-free per-voice linear ADSR processor.
- `DSP/PeakMeter.hpp`: allocation-free stereo sample-peak follower with hold
  and release ballistics for live meters.
- `Audio/WaveformSummary.hpp`: fixed-size stereo min/max reduction and compact
  DSP-to-UI transport.
- `Audio/WavCodec.*`: bounded RIFF/WAVE decoding, stereo PCM16 encoding, and
  offline region/ADSR rendering without third-party audio-file dependencies.
- `Audio/RealtimeAccessGate.hpp`: lock-free audio-side ownership and handoff
  that lets a control thread access callback-owned state immediately while the
  callback is idle, or at a block boundary while it is running.
- `State/SamplePlaybackSettingsCodec.hpp`: versioned settings serialization.
- `UI/Geometry.hpp`: renderer-independent rectangles and reusable pad-grid
  layout/hit testing.

Plug-ins include this directory directly from CMake. Components should remain
generic; workflow-specific behavior belongs in the consuming plug-in.
