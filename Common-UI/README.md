# SMS shared user-interface source

This directory contains source-level UI components shared by SMS plug-ins.
Unlike `Common-Src`, code below `DPF` deliberately depends on DPF/DGL and is
compiled as part of each plug-in UI target so that the plug-in's DPF feature
defines remain authoritative.

Consumers can link the header-only `sms_common_ui` CMake target to inherit the
shared UI and framework-neutral source include paths.

- `DPF/NanoUI.hpp`: logical-canvas scaling and repaint lifecycle.
- `DPF/Theme.hpp`: semantic color and drawing metrics.
- `DPF/Controls.hpp`: reusable NanoVG panel, segment, slider, and action
  primitives.
- `WaveformEditor.hpp`: renderer-independent waveform geometry, hit testing,
  value mapping, and drag behavior.
