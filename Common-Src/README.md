# SMS plug-in shared source

Audience: AI agents. Read when changing shared DSP, codecs, filesystem preferences, or geometry.

This directory contains dependency-light C++20 components that are reusable by
multiple SMS audio plug-ins. It deliberately has no dependency on DPF, LV2, a
host, or a specific plug-in's state model.

- `DSP/SamplePlaybackSettings.hpp`: sanitized sample region/ADSR and
  non-destructive region mirroring.
- `DSP/SampleMixerSettings.hpp`: sanitized per-sample gain, stereo balance, and
  semitone tune, filter, dirty-mode, playback direction, and global mixer routing.
- `DSP/ColorEffects.hpp`: allocation-free per-voice stereo filters and 26.04 kHz,
  12-bit mono source conversion.
- `DSP/AdsrEnvelope.hpp`: allocation-free per-voice linear ADSR processor with
  phase-preserving live setting updates and explicit automatic-to-note-off release handoff.
- `DSP/PeakMeter.hpp`: allocation-free stereo sample-peak follower with hold
  and release ballistics for live meters.
- `Audio/WaveformSummary.hpp`: fixed-size stereo min/max reduction and compact
  DSP-to-UI transport.
- `Audio/WavCodec.*`: bounded RIFF/WAVE decoding, stereo PCM16 encoding, and
  offline editor-processing renders without third-party audio-file dependencies.
- `Audio/RealtimeCommandDispatcher.hpp`: serialized control callers hand bounded,
  non-throwing operations to the callback without pausing audio; inactive host
  lifecycle executes commands inline.
- `Audio/RealtimeLatestMailbox.hpp`: three-slot latest-value control-to-audio
  publication; serialized writers, one reader, bounded lock-free consumption.
- `Audio/RealtimeAccessGate.hpp`: lock-free audio-side ownership and handoff
  that lets a control thread access callback-owned state immediately while the
  callback is idle, or at a block boundary while it is running.
- `State/SamplePlaybackSettingsCodec.hpp`: versioned settings serialization.
- `State/SampleMixerSettingsCodec.hpp`: versioned mixer-settings serialization;
  see the [mixing contract](../Docs/AI/MIXING.md) for fields and compatibility.
- `IO/FileIO.hpp/.cpp`: native user-config folders, UTF-8/native paths, bounded
  reads and atomic file replacement. Control-thread only. Windows uses wide
  APIs and Windows 10+ rename semantics;
  Linux/macOS use POSIX files. The `sms_file_io` CMake target is defined alongside
  its consumer in [Common-UI](../Common-UI/CMakeLists.txt).
- `IO/DefaultsFile.hpp/.cpp`: bounded flat key/value defaults with native config
  paths, writer serialization across threads/processes, latest-file merging and
  atomic replacement. Unknown valid keys survive updates. Compiled into
  `sms_file_io`; control-thread only. Plugin-specific schemas/validators belong
  in consumers; see Anville's [preference contract](../Docs/AI/LAYOUT-PREFERENCE.md).
- `UI/Geometry.hpp`: renderer-independent rectangles and reusable pad-grid
  layout/hit testing.

Plug-ins include this directory directly from CMake. Components should remain
generic; workflow-specific behavior belongs in the consuming plug-in.
