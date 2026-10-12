# SMS shared user-interface source

Audience: AI agents. Read when changing shared UI behavior or rendering.

This directory contains source-level UI components shared by SMS plug-ins.
Unlike `Common-Src`, code below `DPF` deliberately depends on DPF/DGL and is
compiled as part of each plug-in UI target so that the plug-in's DPF feature
defines remain authoritative.

Consumers can link the header-only `sms_common_ui` CMake target to inherit the
shared UI and framework-neutral source include paths.

- `DPF/NanoUI.hpp`: logical-canvas scaling and repaint lifecycle.
- `Skin.hpp`: bounded versioned INI snapshots, UTF-8 text/checked-format lookup, and confined
  image paths; framework-neutral and used only outside audio processing.
- `SkinCatalog.hpp`: named collection discovery, forced-folder precedence, and
  atomic per-user preferences; IDs stay separate from translated display text.
- `DPF/Theme.hpp`: semantic colors, fixed pad tint identities, drawing metrics,
  hover opacity, and per-editor drawing scopes with thread-local restoration.
- `DPF/SkinTheme.hpp`: supported color/metric fields and validated DPF palette
  overrides. See the [skin guide](../SMS-AnvilSampler/Docs/SKINS.md).
- `DPF/AnalogMaterials.hpp`: scalable chassis, recessed-panel, rubber-pad,
  physical-control, LED, screw, and powder-coat rendering primitives.
- `DPF/Controls.hpp`: reusable NanoVG panel, segment, slider, and action
  primitives, symbol buttons, and ivory console fader caps with index lines.
- `Interaction.hpp`, `ContextMenu.hpp`, and `DPF/ContextMenu.hpp`: reusable
  single-target hover identity/transitions, bounded wheel adjustments, menu
  geometry/hit testing, and NanoVG menu rendering.
- `LevelMeter.hpp` and `DPF/LevelMeter.hpp`: renderer-independent variable-size
  stereo LED geometry/level mapping and its NanoVG renderer; optional channel
  spacing leaves room for an overlaid fader.
- `DPF/WaveformRenderer.hpp`: waveform, region, and ADSR drawing.
- `PadLayout.hpp`: visible-pad counts, bottom-up index mapping, and grid geometry.
- `WaveformEditor.hpp`: renderer-independent waveform geometry, hit testing,
  value mapping, and drag behavior.
- `WaveformViewport.hpp`: frame-range zoom, pan, and source-to-screen mapping.
