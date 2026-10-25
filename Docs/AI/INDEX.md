# Task index

Read [AGENTS.md](../../AGENTS.md), then the relevant row.

| Task | Read | Starting point |
| --- | --- | --- |
| Build, dependencies, formats | [Development](DEVELOPMENT.md) | [CMake](../../SMS-AnvilleSampler/CMakeLists.txt) |
| Windows/macOS builds | [Platform builds](PLATFORM-BUILDS.md) | CI and native-host acceptance |
| Dev Container setup | [Development](DEVELOPMENT.md), [Testing](TESTING.md) | [Configuration](../../.devcontainer/devcontainer.json) |
| Tests, LV2 discovery, host/UI checks | [Testing](TESTING.md) | [Tests](../../SMS-AnvilleSampler/tests/) |
| Screenshots | [Images](DOC-IMAGES.md) | [Captures](../../SMS-AnvilleSampler/Docs/) |
| Engine, state, UI | [Anville Sampler architecture](../../SMS-AnvilleSampler/Docs/ARCHITECTURE.md) | [Plugin source](../../SMS-AnvilleSampler/src/) |
| Layout/bank-mode defaults | [Preferences](LAYOUT-PREFERENCE.md) | Startup and host precedence |
| Filter and Dirty implementation | [Mixing contract](MIXING.md) | [DSP](../../Common-Src/DSP/ColorEffects.hpp) |
| Shared DSP, codecs, geometry | [Common-Src map](../../Common-Src/README.md) | [Common-Src](../../Common-Src/) |
| Shared DPF UI, pads, waveform | [Common-UI map](../../Common-UI/README.md) | [Common-UI](../../Common-UI/) |
| Feature scope and direction | [Vision](../../SMS-AnvilleSampler/Docs/VISION.md) | Intended scope |
| Pad context menu and actions | [Context-menu design](PAD-CONTEXT-MENU.md) | Action contract |
| Cut Point Editor design and tests | [Cut Point Editor](CHOP-EDITOR.md) | Three-pad cuts, raw preview, state and limitations |
| Pad storage and transfer concurrency | [Storage handoff](PAD-STORAGE.md) | [Engine](../../SMS-AnvilleSampler/src/core/SamplerEngine.hpp) |
| Pad WAV import/export | [WAV I/O design](PAD-WAV-IO.md) | Formats, real-time boundary, dialogs, support notes |
| VST3 state and outputs | [DPF VST3 constraints](DPF-VST3-CONSTRAINTS.md) | UI synchronization and fixed/dynamic bus boundaries |
| Skins and artwork | [Skin guide](../../SMS-AnvilleSampler/Docs/SKINS.md), [Authoring](SKINS.md) | [Catalog](../../Common-UI/SkinCatalog.hpp), [Theme adapter](../../Common-UI/DPF/SkinTheme.hpp) |
| User-visible behavior | [Plugin guide](../../SMS-AnvilleSampler/README.md) | Verify against affected source/tests |
| User mixing controls | [Mixing guide](../../SMS-AnvilleSampler/Docs/MIXING.md) | Pad and global effects |
| User WAV workflow | [WAV file guide](../../SMS-AnvilleSampler/Docs/WAV-FILES.md) | Current formats and Linux requirements |
| User cut-point workflow | [Adjust cut points](../../SMS-AnvilleSampler/Docs/CHOP-EDITOR.md) | Context action, raw preview, Apply/Exit behavior |
| Human overview/review | [README](../../README.md), [Architecture](../ARCHITECTURE.md) | Components and review |
| Contributions, doc maintenance | [Contributing](../../CONTRIBUTING.md) | [Doc checker](../../scripts/check_docs.py) |
| CI, release preparation | [Releasing](../RELEASING.md) | [Workflows](../../.github/workflows/) |
| Agent integration | [Development](DEVELOPMENT.md) | [Claude alias](../../CLAUDE.md), [Copilot adapter](../../.github/copilot-instructions.md) |

Index every repository-owned Markdown page except this index.
Source maps and plugin architecture are AI references.
