#!/usr/bin/env bash
set -euo pipefail

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "Usage: screenshot-window <window-id> [output.png]" >&2
    exit 2
fi

display="${DISPLAY:-:1}"
window_id="$1"
default_name="window-$(date -u +%Y%m%dT%H%M%SZ).png"
output_path="${2:-$PWD/$default_name}"

if ! [[ "$window_id" =~ ^(0x[0-9A-Fa-f]+|[0-9]+)$ ]]; then
    echo "Window ID must be decimal or hexadecimal: $window_id" >&2
    exit 2
fi

DISPLAY="$display" xwininfo -id "$window_id" >/dev/null
mkdir -p "$(dirname "$output_path")"
DISPLAY="$display" import -window "$window_id" "$output_path"
identify "$output_path" >/dev/null
realpath "$output_path"
