#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -eq 0 ] || [ "$1" = "desktop" ]; then
    exec /usr/local/bin/start-desktop
fi

exec "$@"
