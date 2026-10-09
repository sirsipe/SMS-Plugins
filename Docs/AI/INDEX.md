# Task index

Read [AGENTS.md](../../AGENTS.md), then a relevant row. Commands use the repo root.

| Task | Read | Source of truth / starting point |
| --- | --- | --- |
| Build, dependencies, formats | [Development](DEVELOPMENT.md) | [CMake](../../SMS-AnvilSampler/CMakeLists.txt) |
| Deferred Windows/macOS builds | [Platform builds](PLATFORM-BUILDS.md) | Research and acceptance checks; not current support |
| Dev Container setup | [Development](DEVELOPMENT.md), [Testing](TESTING.md) | [Configuration](../../.devcontainer/devcontainer.json) |
| Tests, LV2 discovery, host/UI checks | [Testing](TESTING.md) | [Tests](../../SMS-AnvilSampler/tests/) |
| Engine, state, UI | [Anvil Sampler architecture](../../SMS-AnvilSampler/Docs/ARCHITECTURE.md) | [Plugin source](../../SMS-AnvilSampler/src/) |
| Filter and Dirty implementation | [Mixing contract](MIXING.md) | [DSP](../../Common-Src/DSP/ColorEffects.hpp) |
| Shared DSP, codecs, geometry | [Common-Src map](../../Common-Src/README.md) | [Common-Src](../../Common-Src/) |
| Shared DPF UI, pads, waveform | [Common-UI map](../../Common-UI/README.md) | [Common-UI](../../Common-UI/) |
| Feature scope and direction | [Vision](../../SMS-AnvilSampler/Docs/VISION.md) | Intended scope; not an implementation checklist |
| Pad context menu and actions | [Context-menu design](PAD-CONTEXT-MENU.md) | Implemented actions and future action contract |
| Cut Point Editor design and tests | [Cut Point Editor](CHOP-EDITOR.md) | Three-pad cuts, raw preview, state and limitations |
| Pad storage and transfer concurrency | [Storage handoff](PAD-STORAGE.md) | [Engine](../../SMS-AnvilSampler/src/core/SamplerEngine.hpp) |
| Pad WAV import/export | [WAV I/O design](PAD-WAV-IO.md) | Formats, real-time boundary, dialogs, support notes |
| VST3 state and outputs | [DPF VST3 constraints](DPF-VST3-CONSTRAINTS.md) | UI synchronization and fixed/dynamic bus boundaries |
| User-visible behavior | [Plugin guide](../../SMS-AnvilSampler/README.md) | Verify against affected source/tests |
| User mixing controls | [Mixing guide](../../SMS-AnvilSampler/Docs/MIXING.md) | Pad and global effects |
| User WAV workflow | [WAV file guide](../../SMS-AnvilSampler/Docs/WAV-FILES.md) | Current formats and Linux requirements |
| User cut-point workflow | [Adjust cut points](../../SMS-AnvilSampler/Docs/CHOP-EDITOR.md) | Context action, raw preview, Apply/Exit behavior |
| Human overview/review | [README](../../README.md), [Architecture](../ARCHITECTURE.md) | Components; review prompt in README |
| Contributions, doc maintenance | [Contributing](../../CONTRIBUTING.md) | [Doc checker](../../scripts/check_docs.py) |
| CI, release preparation | [Releasing](../RELEASING.md) | [Workflows](../../.github/workflows/) |
| Agent integration | [Development](DEVELOPMENT.md) | [Claude alias](../../CLAUDE.md), [Copilot adapter](../../.github/copilot-instructions.md) |

Index every repository-owned Markdown page except this index.
Source maps and plugin architecture are AI references. Read framework docs only
when needed.
