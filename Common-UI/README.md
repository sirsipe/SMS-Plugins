# SMS shared user-interface source

Audience: AI agents. Read when changing shared UI behavior or rendering.

This directory contains source-level UI components shared by SMS plug-ins.
Unlike `Common-Src`, code below `DPF` deliberately depends on DPF/DGL and is
compiled as part of each plug-in UI target so that the plug-in's DPF feature
defines remain authoritative.

Consumers link `sms_common_ui` to inherit shared include paths and the compiled
`sms_file_io` platform library. Other shared UI components remain headers.

- `DPF/NanoUI.hpp`: logical-canvas scaling and repaint lifecycle.
- `Skin.hpp`: bounded versioned INI snapshots, UTF-8 text/checked-format lookup, and confined
  image paths; framework-neutral and used only outside audio processing.
  Decimal metrics use classic-locale streams, supporting Xcode 15 libc++ without
  floating-point `from_chars`, while keeping the host locale unchanged.
- `SkinCatalog.hpp`: named collection discovery, forced-folder precedence, and
  per-user skin choice through shared `DefaultsFile` atomic merging; IDs stay
  separate from translated display text.
- `SkinFileIO.hpp`: image-read limits and a compatibility adapter to the shared
  [filesystem helpers](../Common-Src/IO/FileIO.hpp). Skin/startup preferences use
  [DefaultsFile](../Common-Src/IO/DefaultsFile.hpp), linked through `sms_file_io`.
  Platform paths are documented in the
  [skin guide](../SMS-AnvilleSampler/Docs/SKINS.md).
- `DPF/Theme.hpp`: semantic colors, fixed pad tint identities, drawing metrics,
  hover opacity, and per-editor drawing scopes with thread-local restoration.
- `DPF/SkinTheme.hpp`: supported color/metric fields and validated DPF palette
  overrides. See the [skin guide](../SMS-AnvilleSampler/Docs/SKINS.md).
- `LogoGlow.hpp`: fixed-size alpha resampling, padded Gaussian halo generation,
  and bounded pulse opacity; UI-only cached work, independent of DPF.
- `DPF/LogoGlow.hpp`: logo decoding through the pinned DPF NanoVG stb decoder
  symbols, capped at 4096 pixels per axis; no new decoder or audio dependency.
- `DPF/AnalogMaterials.hpp`: scalable chassis, recessed-panel, rubber-pad,
  physical-control, LED, screw, and powder-coat rendering primitives; pad
  surface opacity leaves selection outlines and external labels intact.
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
- `WaveformViewport.hpp`: frame-range zoom, pan, source-to-screen mapping, and
  cached-detail reprojection with a whole-source summary fallback.
