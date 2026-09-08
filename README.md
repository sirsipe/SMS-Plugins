# SudoMetalStudio Plugins

[![Ubuntu CI](https://github.com/sirsipe/SMS-Plugins/actions/workflows/ubuntu-ci.yml/badge.svg)](https://github.com/sirsipe/SMS-Plugins/actions/workflows/ubuntu-ci.yml)

Native Linux audio plugins from **SudoMetalStudio (SMS)**, a YouTube channel.
The project is developed using AI with human direction and testing.

## Plugins

- [SMS-Midichopper](SMS-Midichopper/README.md) — a live stereo chopping
  sampler under development. LV2 is the primary format; VST3 is also built
  for releases.

![SMS-Midichopper interface](SMS-Midichopper/Docs/SMS-Midichopper-v0.0.3.png)

_Screenshot from version v0.0.3._

Clone with submodules before building:

```bash
git clone --recurse-submodules https://github.com/sirsipe/SMS-Plugins.git
```

The repository-level `third_party/DPF` submodule and `Common-UI` sources are
shared by all plug-ins. Each plug-in contains its own build and usage
instructions.

Release builds are produced by GitHub Actions. See
[Docs/RELEASING.md](Docs/RELEASING.md) for the release procedure.

Report bugs and propose features in
[GitHub Issues](https://github.com/sirsipe/SMS-Plugins/issues).
See [Contributing](CONTRIBUTING.md) for development and documentation upkeep.
AI agents start at [AGENTS.md](AGENTS.md) and use its task index.
