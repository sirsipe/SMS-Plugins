# SMS shared user-interface source

Audience: AI agents. Read when changing shared UI behavior or rendering.

This directory contains source-level UI components shared by SMS plug-ins.
Unlike `Common-Src`, code below `DPF` deliberately depends on DPF/DGL and is
compiled as part of each plug-in UI target so that the plug-in's DPF feature
defines remain authoritative.

Consumers can link the header-only `sms_common_ui` CMake target to inherit the
shared UI and framework-neutral source include paths.

- `DPF/NanoUI.hpp`: logical-canvas scaling and repaint lifecycle.
- `DPF/Theme.hpp`: semantic colors, drawing metrics, and hover opacity.
- `DPF/AnalogMaterials.hpp`: scalable chassis, recessed-panel, rubber-pad,
  physical-control, LED, screw, and powder-coat rendering primitives.
- `DPF/Controls.hpp`: reusable NanoVG panel, segment, slider, and action
  primitives.
- `Interaction.hpp`, `ContextMenu.hpp`, and `DPF/ContextMenu.hpp`: reusable
  single-target hover identity/transitions, bounded wheel adjustments, menu
  geometry/hit testing, and NanoVG menu rendering.
- `LevelMeter.hpp` and `DPF/LevelMeter.hpp`: renderer-independent variable-size
  stereo LED geometry/level mapping and its NanoVG renderer.
- `DPF/WaveformRenderer.hpp`: waveform, region, and ADSR drawing.
- `PadLayout.hpp`: visible-pad counts, bottom-up index mapping, and grid geometry.
- `WaveformEditor.hpp`: renderer-independent waveform geometry, hit testing,
  value mapping, and drag behavior.
