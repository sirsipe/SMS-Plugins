# SMS plug-in shared source

This directory contains dependency-light C++20 components that are reusable by
multiple SMS audio plug-ins. It deliberately has no dependency on DPF, LV2, a
host, or a specific plug-in's state model.

- `DSP/SamplePlaybackSettings.hpp`: sanitized, non-destructive sample region
  and ADSR value type.
- `DSP/AdsrEnvelope.hpp`: allocation-free per-voice linear ADSR processor.
- `Audio/WaveformSummary.hpp`: fixed-size stereo min/max reduction and compact
  DSP-to-UI transport.
- `State/SamplePlaybackSettingsCodec.hpp`: versioned settings serialization.
- `UI/Geometry.hpp`: renderer-independent rectangles and reusable pad-grid
  layout/hit testing.

Plug-ins include this directory directly from CMake. Components should remain
generic; workflow-specific behavior belongs in the consuming plug-in.
