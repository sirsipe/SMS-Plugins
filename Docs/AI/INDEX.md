# Task index

AI-facing navigation. Start with [AGENTS.md](../../AGENTS.md), then read only
the row needed for the task. Paths in commands are relative to the repo root.

| Task | Read | Source of truth / starting point |
| --- | --- | --- |
| Build, dependencies, formats | [Development](DEVELOPMENT.md) | [CMake](../../SMS-Midichopper/CMakeLists.txt) |
| Deferred Windows/macOS builds | [Platform builds](PLATFORM-BUILDS.md) | Research and acceptance checks; not current support |
| Dev Container setup | [Development](DEVELOPMENT.md), [Testing](TESTING.md) | [Configuration](../../.devcontainer/devcontainer.json) |
| Tests, LV2 discovery, host/UI checks | [Testing](TESTING.md) | [Tests](../../SMS-Midichopper/tests/) |
| Engine, parameters, state, UI | [Midichopper architecture](../../SMS-Midichopper/Docs/ARCHITECTURE.md) | [Plugin source](../../SMS-Midichopper/src/) |
| Shared DSP, codecs, geometry | [Common-Src map](../../Common-Src/README.md) | [Common-Src](../../Common-Src/) |
| Shared DPF UI, pads, waveform | [Common-UI map](../../Common-UI/README.md) | [Common-UI](../../Common-UI/) |
| Feature scope and direction | [Vision](../../SMS-Midichopper/Docs/VISION.md) | Intended scope; not an implementation checklist |
| Pad context menu and actions | [Context-menu design](PAD-CONTEXT-MENU.md) | Implemented Clear Pad and future action contract |
| Pad WAV import/export | [WAV I/O design](PAD-WAV-IO.md) | Formats, real-time boundary, dialogs, support notes |
| User-visible behavior | [Plugin guide](../../SMS-Midichopper/README.md) | Verify against affected source/tests |
| Project overview | [Project README](../../README.md) | Human entry point |
| Contributions, doc maintenance | [Contributing](../../CONTRIBUTING.md) | [Doc checker](../../scripts/check_docs.py) |
| CI, release preparation | [Releasing](../RELEASING.md) | [Workflows](../../.github/workflows/) |
| Agent integration | [Development](DEVELOPMENT.md) | [Claude alias](../../CLAUDE.md), [Copilot adapter](../../.github/copilot-instructions.md) |

Every repository-owned Markdown page belongs here, except this index itself.
Shared source maps and architecture are AI references despite their historical
paths. Do not read external framework docs until a task requires them.
