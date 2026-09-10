# syntax=docker/dockerfile:1

FROM ubuntu:24.04

LABEL io.sudometalstudio.sms-plugins-devcontainer="true"

SHELL ["/bin/bash", "-euxo", "pipefail", "-c"]

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    alsa-utils \
    build-essential \
    ca-certificates \
    carla \
    clang \
    clang-format \
    clang-tidy \
    clangd \
    cmake \
    curl \
    dbus-x11 \
    file \
    fonts-dejavu-core \
    gdb \
    git \
    git-lfs \
    imagemagick \
    jack-tools \
    jackd2 \
    jalv \
    jq \
    libdbus-1-dev \
    libegl1-mesa-dev \
    libgl-dev \
    libgl1-mesa-dev \
    libgl1-mesa-dri \
    libglx-dev \
    libopengl-dev \
    libx11-dev \
    libxcursor-dev \
    libxext-dev \
    libxinerama-dev \
    libxrandr-dev \
    lilv-utils \
    lldb \
    ltrace \
    make \
    mesa-utils \
    netcat-openbsd \
    ninja-build \
    novnc \
    openssh-client \
    openbox \
    pkg-config \
    procps \
    python3 \
    python3-dbus \
    python3-gi \
    python3-pip \
    ripgrep \
    strace \
    tigervnc-standalone-server \
    tigervnc-tools \
    tini \
    unzip \
    valgrind \
    websockify \
    wmctrl \
    x11-utils \
    x11-xserver-utils \
    xdotool \
    xterm \
    xdg-desktop-portal \
    xdg-desktop-portal-gtk \
    && rm -rf /var/lib/apt/lists/*

ARG DEV_USER=vscode
ARG DEV_UID=1000
ARG DEV_GID=1000

ENV HOME=/home/${DEV_USER}

RUN if getent passwd ubuntu >/dev/null; then userdel -r ubuntu; fi \
    && if getent group ubuntu >/dev/null; then groupdel ubuntu; fi \
    && groupadd --gid "${DEV_GID}" "${DEV_USER}" \
    && useradd --uid "${DEV_UID}" --gid "${DEV_GID}" --create-home \
        --shell /bin/bash "${DEV_USER}" \
    && install -d -m 1777 /tmp/.X11-unix \
    && install -d -o "${DEV_USER}" -g "${DEV_USER}" \
        "/home/${DEV_USER}/.codex" \
        "/home/${DEV_USER}/.config/sms-plugins-devcontainer"

ARG INSTALL_CODEX_CLI=1

USER ${DEV_USER}
RUN if [ "${INSTALL_CODEX_CLI}" = "1" ]; then \
        curl -fsSL https://chatgpt.com/codex/install.sh | sh; \
    fi

USER root
RUN if [ "${INSTALL_CODEX_CLI}" = "1" ]; then \
        codex_binary="$(readlink -f "/home/${DEV_USER}/.local/bin/codex")"; \
        test -x "${codex_binary}"; \
        install -m 0755 "${codex_binary}" /usr/local/bin/codex; \
        code_mode_host="$(find "/home/${DEV_USER}/.codex" \
            -type f -name codex-code-mode-host -print -quit)"; \
        if [ -n "${code_mode_host}" ]; then \
            install -m 0755 "${code_mode_host}" \
                /usr/local/bin/codex-code-mode-host; \
        fi; \
        rm -rf "/home/${DEV_USER}/.codex" "/home/${DEV_USER}/.local/bin/codex"; \
        install -d -o "${DEV_USER}" -g "${DEV_USER}" \
            "/home/${DEV_USER}/.codex"; \
    fi

COPY .devcontainer/scripts/container-entrypoint.sh /usr/local/bin/container-entrypoint
COPY .devcontainer/scripts/start-desktop.sh /usr/local/bin/start-desktop
COPY .devcontainer/scripts/desktop-health.sh /usr/local/bin/desktop-health
COPY .devcontainer/scripts/portal-dialog-health.sh /usr/local/bin/portal-dialog-health
COPY .devcontainer/scripts/portal-dialog-health.py /usr/local/libexec/portal-dialog-health
COPY .devcontainer/scripts/screenshot-window.sh /usr/local/bin/screenshot-window
COPY .devcontainer/scripts/test-plugin.sh /usr/local/bin/test-plugin
COPY .devcontainer/dbus-session.conf /usr/local/share/sms-plugins/dbus-session.conf
COPY .devcontainer/xdg-desktop-portal/portals.conf /etc/xdg/xdg-desktop-portal/portals.conf

RUN chmod 0755 \
    /usr/local/bin/container-entrypoint \
    /usr/local/bin/start-desktop \
    /usr/local/bin/desktop-health \
    /usr/local/bin/portal-dialog-health \
    /usr/local/libexec/portal-dialog-health \
    /usr/local/bin/screenshot-window \
    /usr/local/bin/test-plugin

USER ${DEV_USER}
WORKDIR /workspaces/SMS-Plugins

ENV DISPLAY=:1 \
    DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/runtime-vscode/bus \
    QT_QPA_PLATFORM=xcb \
    LIBGL_ALWAYS_SOFTWARE=1 \
    XDG_CURRENT_DESKTOP=Openbox \
    XDG_RUNTIME_DIR=/tmp/runtime-vscode \
    NOVNC_PORT=6080 \
    VNC_PORT=5901

EXPOSE 5901 6080

ENTRYPOINT ["/usr/bin/tini", "-g", "--", "/usr/local/bin/container-entrypoint"]
CMD ["desktop"]
