# SudoMetalStudio Plugins

[![Ubuntu CI](https://github.com/sirsipe/SMS-Plugins/actions/workflows/ubuntu-ci.yml/badge.svg)](https://github.com/sirsipe/SMS-Plugins/actions/workflows/ubuntu-ci.yml)

Open-source audio plugins from SudoMetalStudio.

## Plugins

- [SMS-Midichopper](SMS-Midichopper/README.md) — a live stereo chopping
  sampler for Linux, available as LV2 and optionally VST3.

![SMS-Midichopper interface](SMS-Midichopper/Docs/SMS-Midichopper-v0.0.1.png)

_Screenshot from version v0.0.1._

Clone with submodules before building:

```bash
git clone --recurse-submodules https://github.com/sirsipe/SMS-Plugins.git
```

Each plugin contains its own build and usage instructions.

Release builds are produced by GitHub Actions. See
[Docs/RELEASING.md](Docs/RELEASING.md) for the release procedure.
