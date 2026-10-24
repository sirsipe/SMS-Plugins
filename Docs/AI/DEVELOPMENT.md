# Development reference

Audience: AI agents. All commands run from the repository root.

## Build

Dependencies and build/install commands live in the
[plugin guide](../../SMS-AnvilleSampler/README.md). The
[CMake project](../../SMS-AnvilleSampler/CMakeLists.txt) requires CMake 3.22+ and
C++20. Initialize the pinned [DPF submodule](../../.gitmodules) before configuring.
Do not update its revision as an incidental part of another task.

To match Ubuntu CI:

```bash
cmake -S SMS-AnvilleSampler -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DANVILLESAMPLER_BUILD_VST3=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Outputs: `build/bin/SMS-AnvilleSampler.lv2` and, when enabled,
`build/bin/SMS-AnvilleSampler.vst3`. `ANVILLESAMPLER_BUILD_CLAP=ON` adds CLAP;
CLAP is not currently covered by CI. VST3/CLAP default OFF;
`ANVILLESAMPLER_BUILD_LV2` defaults ON and can be disabled for VST3-only builds.
At least one format must be enabled.
Python 3 runs DPF's resource converter to embed `res/anville.png` in each UI binary.
Each build copies the maintained Finnish Dream, Looks like Wu, and White Cadillac
Vinyl skins into LV2/VST3 `skins/` and CLAP sibling
`SMS-AnvilleSampler.skins`. The built-in appearance is Default; all three skins
are selectable from the menu.
The inactive `skin-template` remains available for authoring. Copies refresh
even when no binaries relink; builds remove legacy forced skins from build
output. The [skin guide](../../SMS-AnvilleSampler/Docs/SKINS.md) documents
cleanup of retired skin folders in reused output. Custom forced skins belong in
installed bundles.
Use a fresh ignored `build-*` directory if an existing CMake cache uses a
different source path or generator. Avoid repeatedly rebuilding unchanged code.

Windows and macOS formats are deferred and are not supported release targets.
Windows CI prerequisites, native build commands, remaining gaps and validation live in
[Platform builds](PLATFORM-BUILDS.md); do not present its unverified commands as
current release instructions.

## Native skin filesystem tests

These checks need CMake 3.22+ and C++20, without DPF or a display:

```bash
cmake -S SMS-AnvilleSampler/tests/skins -B build-skins
cmake --build build-skins --config Release --parallel 2
ctest --test-dir build-skins -C Release --output-on-failure
```

[Skin CI](../../.github/workflows/skin-ci.yml) runs them on Linux, Windows/MSVC
and macOS, including Unicode installation paths. Windows symlink cases report
skips when privileges are unavailable; other coverage still runs. The checks
cover manifests, discovery, native config folders, Unicode paths/artwork reads,
and concurrent atomic preferences, including held Windows readers. Windows
persistence requires Windows 10 version 1607 or newer. They do not validate a
native DAW/editor or establish Windows/macOS plugin release support.

## Docker development environment

The repository-owned [Dev Container](../../.devcontainer/devcontainer.json) is
the maintained full toolchain. Its Ubuntu 24.04 image supplies C++20 compilers,
CMake, Ninja, clangd, DPF's X11/OpenGL dependencies, LV2 tools, VST3 build
support, Carla, jalv, dummy JACK, and GUI diagnostics. It runs as non-root user
`vscode`; Dev Containers updates that user's UID/GID on Linux. The normal
workspace mount is `/workspaces/SMS-Plugins`.

The image starts its supervised desktop automatically. It drops all Linux
capabilities and enables `no-new-privileges`; it does not mount host service
sockets or directories beyond the current checkout. Its isolated D-Bus session
and GTK desktop portal exercise Linux Open and Save dialogs; no host D-Bus
socket is mounted. The Codex standalone CLI is installed using the official
Linux installer. Set `INSTALL_CODEX_CLI=0` for a CLI-free image. Authentication
is not baked in and persists only in the labeled
`sms-plugins-devcontainer-codex` volume.

After changing `.devcontainer`, use **Dev Containers: Rebuild and Reopen in
Container** from the VS Code Command Palette, then run `desktop-health
--dialogs`. From a host shell, the equivalent maintained workflow is:

```bash
make dev-build dev-run dev-ready
```

To exercise the Linux fallback without a session bus, give only the tested host
an invalid address, for example
`DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/sms-no-session-bus ...`; never stop or
reuse the container's bus. Unsetting it can rediscover the active session
through X11.

Dev Containers is authoritative. Root Make targets provide a matching manual
Docker workflow for diagnostics: `make dev-build dev-run dev-ready`. Ports are
published on host loopback only. `make dev-wipe` selects labeled project
resources and verifies the exact persistent volume labels; it never performs a
global prune. Close the VS Code remote window before wiping.

## Change routing

- Engine/timing: `SMS-AnvilleSampler/src/core`; `sampler-core` tests.
- Parameters/host transport: `SMS-AnvilleSampler/src/plugin/Parameters.hpp`,
  `MidichopperPlugin.cpp`, and `src/ui/MidichopperUI.cpp`. Keep DSP/UI in sync.
- Project state: `src/plugin/StateCodec.*`, adapter state callbacks, and
  `Common-Src/State`; `state-codec` tests plus host save/restore.
- Product UI: `src/ui/MidichopperUI.cpp` handles interaction/host communication;
  `MidichopperView.*` draws it; `MidichopperLayout.hpp` defines product geometry.
- Shared code: consult the index's shared maps; check consuming plugin tests.
- Build/version/release: plugin CMake and `.github/workflows`. Read
  [releasing](../RELEASING.md) before preparing version changes.

Unless prefixed otherwise, `src/` above is under `SMS-AnvilleSampler/`.

## Instruction adapters and documentation checks

Edit only root `AGENTS.md` for shared policy. `CLAUDE.md` is a relative symlink
to it. Copilot uses a short `.github/copilot-instructions.md` bootstrap; keep its
local-only boundary consistent with `AGENTS.md`. Other tools should load
`AGENTS.md` explicitly if they do not discover it. Avoid additional automatic
imports of the reference library.

Official entry-point references:
[Codex](https://developers.openai.com/codex/guides/agents-md),
[Claude](https://code.claude.com/docs/en/memory),
[Copilot](https://docs.github.com/en/copilot/how-tos/copilot-on-github/customize-copilot/add-custom-instructions/add-repository-instructions).
Client support varies; instructions are not a technical remote-write lock.

`python3 scripts/check_docs.py` checks owned Markdown, including new unignored
files: inline relative link targets, index coverage, word ceilings, and the
Claude symlink. Use inline Markdown links for local references. It does not
validate URL availability, heading fragments, or semantic accuracy. Exclude
vendored framework docs. [Docs CI](../../.github/workflows/docs.yml) runs this
without installing audio build dependencies.
