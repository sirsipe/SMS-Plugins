#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -eq 0 ] || [ "$1" = "desktop" ]; then
    runtime_dir="${XDG_RUNTIME_DIR:-/tmp/runtime-vscode}"
    mkdir -p "$runtime_dir"
    chmod 700 "$runtime_dir"
    exec dbus-run-session \
        --config-file=/usr/local/share/sms-plugins/dbus-session.conf \
        -- /usr/local/bin/start-desktop
fi

exec "$@"
