# Development reference

Audience: AI agents. All commands run from the repository root.

## Build

Dependencies and default LV2 build/install commands live in the
[plugin guide](../../SMS-Midichopper/README.md). The
[CMake project](../../SMS-Midichopper/CMakeLists.txt) requires CMake 3.22+ and
C++20. Initialize the pinned [DPF submodule](../../.gitmodules) before configuring.
Do not update its revision as an incidental part of another task.

To match Ubuntu CI:

```bash
cmake -S SMS-Midichopper -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DMIDICHOPPER_BUILD_VST3=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Outputs: `build/bin/SMS-Midichopper.lv2` and, when enabled,
`build/bin/SMS-Midichopper.vst3`. `MIDICHOPPER_BUILD_CLAP=ON` adds CLAP;
CLAP is not currently covered by CI. Both optional format switches default OFF.
Use a fresh ignored `build-*` directory if an existing CMake cache uses a
different source path or generator. Avoid repeatedly rebuilding unchanged code.

## Change routing

- Engine/timing: `SMS-Midichopper/src/core`; `sampler-core` tests.
- Parameters/host transport: `SMS-Midichopper/src/plugin/Parameters.hpp`,
  `MidichopperPlugin.cpp`, and `src/ui/MidichopperUI.cpp`. Keep DSP/UI in sync.
- Project state: `src/plugin/StateCodec.*`, adapter state callbacks, and
  `Common-Src/State`; `state-codec` tests plus host save/restore.
- Product UI: `src/ui/MidichopperUI.cpp` handles interaction/host communication;
  `MidichopperView.*` draws it; `MidichopperLayout.hpp` defines product geometry.
- Shared code: consult the index's shared maps; check consuming plugin tests.
- Build/version/release: plugin CMake and `.github/workflows`. Read
  [releasing](../RELEASING.md) before preparing version changes.

Unless prefixed otherwise, `src/` above is under `SMS-Midichopper/`.

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
