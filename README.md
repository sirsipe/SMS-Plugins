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

## Docker Dev Container

The repository includes a Docker-based [Dev Container](.devcontainer/devcontainer.json)
with the build toolchain, LV2/VST3 dependencies, Carla and jalv, dummy JACK,
and a TigerVNC/Openbox desktop. Install Docker Engine, Visual Studio Code, and
the recommended **Dev Containers** extension
(`ms-vscode-remote.remote-containers`) on the host. Then:

1. Open this repository in a second VS Code window.
2. Run **Dev Containers: Reopen in Container**.
3. Wait for `desktop-health` to succeed in the container terminal.
4. Open VS Code's forwarded port 6080 for noVNC, or connect a VNC viewer to
   forwarded port 5901.

The workspace opens directly at `/workspaces/SMS-Plugins` as user `vscode`.
The container-side Codex, CMake Tools, and clangd extensions install
automatically. Codex CLI is also available; run `codex` and sign in when needed.
Codex state and an optional VNC password persist in two narrowly scoped Docker
volumes. No host home, display/audio socket, Docker socket, private key, or
unrelated repository is mounted.

The passwordless VNC default is intended only for VS Code's local tunnel or the
Make workflow's loopback-bound ports. In a Dev Container terminal, set a
persistent password with
`tigervncpasswd "$HOME/.config/sms-plugins-devcontainer/tigervnc.passwd"`; the
manual workflow provides `make dev-vnc-password`. Restart the container after
either command. Never expose ports 5901 or 6080 publicly without authentication
and transport security.

Dev Containers is the primary workflow. The equivalent manual commands are:

```bash
make dev-build dev-run dev-ready
make dev-shell             # or: make dev-codex
```

Close any VS Code remote window before `make dev-wipe`. That target removes
only Docker resources carrying this repository's label and its two exact named
volumes; it never runs a global prune. Removing the Codex volume requires
signing in again. Host ports may be changed with `NOVNC_HOST_PORT=6081` and
`VNC_HOST_PORT=5902` on `make dev-run`.

Release builds are produced by GitHub Actions. See
[Docs/RELEASING.md](Docs/RELEASING.md) for the release procedure.

Report bugs and propose features in
[GitHub Issues](https://github.com/sirsipe/SMS-Plugins/issues).
See [Contributing](CONTRIBUTING.md) for development and documentation upkeep.
AI agents start at [AGENTS.md](AGENTS.md) and use its task index.
