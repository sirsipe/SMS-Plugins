# Windows CI and deferred platform releases

Audience: maintainers or agents resuming cross-platform work. This page records
development build support, not native DAW validation. Releases remain Linux
x86-64 LV2 and VST3; do not add other release artifacts until their native-host
checks below pass.

## Findings

The engine and state codec use portable C++.
[Skin filesystem operations](../../Common-UI/SkinFileIO.cpp) implement Linux,
Windows 10+, and macOS paths and preference persistence; Windows resolves the
loaded module through the wide API because pinned DPF exposes ANSI filenames.
The [focused skin CI](../../.github/workflows/skin-ci.yml) builds filesystem tests
on all three platforms independently of DPF. This is not full plugin coverage.
Online Help in
[the UI](../../SMS-AnvilleSampler/src/ui/MidichopperUI.cpp) launches a browser
only on Linux; other platforms return failure. DPF/DGL owns format and
window-system integration. The pinned
DPF CMake support generates Windows and macOS VST3 bundles and an Apple-only
AUv2 `.component`. Its [feature table](../../third_party/DPF/FEATURES.md) reports
MIDI input, parameter outputs, an embedded UI, and full internal state for VST3
and AU. Therefore the existing per-pad sample state should be reusable, but
large-state restoration must be tested in native hosts rather than inferred
from codec tests.

The pinned VST3 wrapper has two relevant limits: its `updateStateValue()`
callback is absent, and DPF audio-port topology is compile-time fixed even
though native VST3 can notify hosts of bus-count changes. Anville Sampler's VST3
build uses a local UI message bus for transient replies. See
[DPF VST3 constraints](DPF-VST3-CONSTRAINTS.md) before changing VST3 state or
planning multi-output routing.

[Plugin metadata](../../SMS-AnvilleSampler/src/plugin/DistrhoPluginInfo.h) already
defines four-character brand `SMSM` and plugin `AnSm` identifiers. With stereo
input/output and MIDI input, DPF automatically classifies the AU as `aumf`, a
music effect. DPF's AU wrapper does not support host-driven UI resizing, so the
default 1344x756 editor and host behavior need explicit testing.

## Windows runtime requirements

Target Windows 10 (1607+) or Windows 11, with a 64-bit VST3 host and graphics
driver providing OpenGL 2.x compatibility. Native client/DAW validation remains
pending. Windows 7 is not a supported target; skin preference replacement uses
the Windows 10 rename path without a legacy fallback. This does not establish
that the plugin cannot load on Windows 7.

Keep the complete VST3 bundle. WebView2, Python, CMake and SDKs are not runtime
dependencies. The configured MSVC CI build uses a static C++ runtime. Audio
drivers are managed by the host; the plugin requires no ASIO component itself.

## Current gaps

- Online Help needs a native browser launcher for Windows/macOS.
- The project removes unused DPF WebView definitions and Windows source files
  from its targets. This avoids generator-expression feature detection and
  CHOC/Windows loader conflicts without changing the pinned submodule.
- LV2 can be disabled for VST3/CLAP-only builds; AU has no project option.
- The CMake install rule installs only LV2. Windows VST3, macOS VST3, and AU
  need bundle-aware installation and packaging.
- Full plugin CI is configured for Ubuntu and Windows x64/MSVC. Windows
  cross-compilation succeeds with LLVM-MinGW; the native Windows workflow must
  run on GitHub to verify MSVC and CTest. Apple SDK compilation remains unverified.
- There is no Windows/macOS DAW validation, universal-macOS check, signing, or
  notarization workflow.

## Proposed enablement

[Windows CI](../../.github/workflows/windows-ci.yml) runs on push to `main` or `win-vst3`, pull
requests, or manual dispatch. It uses GitHub's
[Windows 2022 image](https://github.com/actions/runner-images/blob/main/images/windows/Windows2022-Readme.md)
with Visual Studio 2022 C++ tools, Windows SDK and CMake 3.22+. It selects
Python 3.12 for embedded artwork and initializes the pinned DPF submodule.
Before configuring, it downloads pinned Khronos `glext.h` and `khrplatform.h`
into DPF's expected directories and checks their SHA-256 hashes. There is no
external VST3 SDK, Linux package, private dependency or secret to install.

The workflow builds VST3 only, runs all CTest tests, verifies bundle binary,
default skin and template paths, and checks LV2 was not produced. The static
MSVC runtime avoids a separate Visual C++ redistributable for these artifacts.
Push/PR runs retain no bundles; manual runs upload a ZIP and SHA-256 checksum
for 14 days. See [testing branch builds](../RELEASING.md).

With the same prerequisites, the native Windows x64 commands are:

```powershell
cmake -S SMS-AnvilleSampler -B build-win `
  -G "Visual Studio 17 2022" -A x64 `
  -DANVILLESAMPLER_BUILD_LV2=OFF -DANVILLESAMPLER_BUILD_VST3=ON `
  -DBUILD_TESTING=ON -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
cmake --build build-win --config Release --parallel
ctest --test-dir build-win -C Release --output-on-failure
```

Expected output is `build-win/bin/SMS-AnvilleSampler.vst3`, including
`Contents/x86_64-win/SMS-AnvilleSampler.vst3`.

For macOS, first add `ANVILLESAMPLER_BUILD_AU` (default OFF, rejected unless
`APPLE`) and install rules for enabled bundles, preserving Linux defaults.
A prospective native universal macOS build is:

```bash
cmake -S SMS-AnvilleSampler -B build-mac -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DANVILLESAMPLER_BUILD_LV2=OFF -DANVILLESAMPLER_BUILD_VST3=ON \
  -DANVILLESAMPLER_BUILD_AU=ON \
  '-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64' \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=<chosen-minimum>
cmake --build build-mac --parallel
ctest --test-dir build-mac --output-on-failure
```

Expected outputs are `SMS-AnvilleSampler.vst3` and
`SMS-AnvilleSampler.component` under `build-mac/bin`. Choose and document the
minimum macOS version from actual host coverage; do not guess it from the SDK.

## Acceptance before claiming support

- Run CTest natively on every shipped architecture and validate bundle layout.
- Exercise audio input, MIDI capture/playback, UI, parameter changes and
  triggers in at least REAPER or Bitwig for Windows, and representative VST3
  and AU hosts on macOS.
- Save, close, and restore projects containing sparse and near-capacity pad
  audio plus editor settings. Include a different-session-sample-rate restore
  and watch project size/save latency.
- Run a VST3 validator. For AU, run `auval -v aumf AnSm SMSM` and test the
  fixed-size editor in Logic.
- Verify both slices of a universal macOS bundle, then establish appropriate
  Windows signing and Apple code-signing/notarization before distribution.
- Add remaining macOS CI, signing, installation documentation and platform
  release publishing only after native-host checks pass. Windows CI artifacts
  remain unsigned development builds.
