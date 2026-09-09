# SudoMetalStudio Plugins

Native Linux audio plugins developed with AI and human testing.

## Plugins

- [SMS-Midichopper](SMS-Midichopper/README.md) — live stereo chopping sampler
  for LV2 and VST3.

![SMS-Midichopper interface](SMS-Midichopper/Docs/SMS-Midichopper-v0.0.3.png)

## Quick Start: AI autopilot in an isolated container

Requirements: Ubuntu Linux and ChatGPT Plus, or another plan that includes
GPT-5.6 Sol.

1. Install [Visual Studio Code](https://code.visualstudio.com/) and its
   [Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers).

2. Install Docker and add your user to its group:

   ```bash
   sudo apt update
   sudo apt install docker.io
   sudo systemctl enable --now docker
   sudo usermod -aG docker "$USER"
   sudo reboot
   ```

   **Reboot before opening VS Code.** Logging out may leave VS Code background
   processes without Docker permission.

3. Clone and open the repository:

   ```bash
   git clone --recurse-submodules https://github.com/sirsipe/SMS-Plugins.git
   code SMS-Plugins
   ```

4. In VS Code, run **Dev Containers: Reopen in Container**. The first image
   build downloads the complete audio, GUI, and AI toolchain and can take
   several minutes. Later starts are much faster.

5. Open the Codex panel on the right and sign in. Select **Full access**,
   **GPT-5.6 Sol**, and **High** reasoning for autopilot-style work.

6. Try this prompt:

   > Hello! Make the ARM button of SMS-Midichopper red, test it, and show me a
   > picture of how it looks.

7. (*Optional*) - To satisfy your curiosity, follow the agent's virtual screen at
   [http://localhost:6080/vnc.html](http://localhost:6080/vnc.html).

Inside the container, an AI agent can build and install the plugin, run its
tests, launch Carla, operate the real plugin UI, and capture window-only
screenshots. Docker provides the outer safety boundary for Full access: the
agent can change this checkout and use the network, but receives no host home,
display, audio, Docker socket, private keys, or unrelated repositories.

See [Contributing](CONTRIBUTING.md) for manual development and validation.
