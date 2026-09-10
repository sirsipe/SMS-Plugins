# Deferred Windows and macOS builds

Audience: maintainers or agents resuming cross-platform work. This page records
research, not current platform support. SMS-Midichopper releases remain Linux
x86-64 LV2 and VST3; do not add other release artifacts until their native-host
checks below pass.

## Findings

The plugin, core, state codec, and Common-UI sources have no known direct Linux
API dependency. DPF/DGL owns format and window-system integration. The pinned
DPF CMake support generates Windows and macOS VST3 bundles and an Apple-only
AUv2 `.component`. Its [feature table](../../third_party/DPF/FEATURES.md) reports
MIDI input, parameter outputs, an embedded UI, and full internal state for VST3
and AU. Therefore the existing per-pad sample state should be reusable, but
large-state restoration must be tested in native hosts rather than inferred
from codec tests.

[Plugin metadata](../../SMS-Midichopper/src/plugin/DistrhoPluginInfo.h) already
defines four-character brand `SMSM` and plugin `MdCh` identifiers. With stereo
input/output and MIDI input, DPF automatically classifies the AU as `aumf`, a
music effect. DPF's AU wrapper does not support host-driven UI resizing, so the
default 1040x680 editor and host behavior need explicit testing.

## Current gaps

- The format list always starts with LV2. VST3 and CLAP have options, but LV2
  cannot be disabled and AU has no project option.
- The CMake install rule installs only LV2. Windows VST3, macOS VST3, and AU
  need bundle-aware installation and packaging.
- Project CI, artifacts, and releases are Ubuntu-only. Neither MSVC nor Apple
  SDK compilation is currently verified.
- There is no Windows/macOS DAW validation, universal-macOS check, signing, or
  notarization workflow.

## Proposed enablement

First add `MIDICHOPPER_BUILD_LV2` (default `ON`) and
`MIDICHOPPER_BUILD_AU` (default `OFF`, rejected unless `APPLE`), build the format
list conditionally, and preserve all existing Linux defaults. Add install rules
only for enabled bundles.

A prospective native Windows x64 build is:

```powershell
cmake -S SMS-Midichopper -B build-win `
  -G "Visual Studio 17 2022" -A x64 `
  -DMIDICHOPPER_BUILD_LV2=OFF -DMIDICHOPPER_BUILD_VST3=ON `
  -DBUILD_TESTING=ON
cmake --build build-win --config Release --parallel
ctest --test-dir build-win -C Release --output-on-failure
```

Expected output is `build-win/bin/SMS-Midichopper.vst3`, including
`Contents/x86_64-win/SMS-Midichopper.vst3`.

A prospective native universal macOS build is:

```bash
cmake -S SMS-Midichopper -B build-mac -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DMIDICHOPPER_BUILD_LV2=OFF -DMIDICHOPPER_BUILD_VST3=ON \
  -DMIDICHOPPER_BUILD_AU=ON \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=<chosen-minimum>
cmake --build build-mac --parallel
ctest --test-dir build-mac --output-on-failure
```

Expected outputs are `SMS-Midichopper.vst3` and
`SMS-Midichopper.component` under `build-mac/bin`. Choose and document the
minimum macOS version from actual host coverage; do not guess it from the SDK.

## Acceptance before claiming support

- Run CTest natively on every shipped architecture and validate bundle layout.
- Exercise audio input, MIDI capture/playback, UI, parameter changes and
  triggers in at least REAPER or Bitwig for Windows, and representative VST3
  and AU hosts on macOS.
- Save, close, and restore projects containing sparse and near-capacity pad
  audio plus editor settings. Include a different-session-sample-rate restore
  and watch project size/save latency.
- Run a VST3 validator. For AU, run `auval -v aumf MdCh SMSM` and test the
  fixed-size editor in Logic.
- Verify both slices of a universal macOS bundle, then establish appropriate
  Windows signing and Apple code-signing/notarization before distribution.
- Add native CI, packaging, checksums, installation documentation, and release
  procedure changes only when maintainers decide to support the platforms.
