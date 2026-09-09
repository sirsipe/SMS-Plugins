#!/usr/bin/env bash
set -euo pipefail

display="${DISPLAY:-:1}"
vnc_port="${VNC_PORT:-5901}"
novnc_port="${NOVNC_PORT:-6080}"

if [ "$(cat /proc/1/comm)" != tini ]; then
    echo "PID 1 is not tini." >&2
    exit 1
fi

DISPLAY="$display" xdpyinfo >/dev/null

pointer_state="$(DISPLAY="$display" xdotool getmouselocation --shell)"
pointer_x="$(printf '%s\n' "$pointer_state" | sed -n 's/^X=//p')"
pointer_y="$(printf '%s\n' "$pointer_state" | sed -n 's/^Y=//p')"
if ! [[ "$pointer_x" =~ ^[0-9]+$ && "$pointer_y" =~ ^[0-9]+$ ]]; then
    echo "Could not read the X11 pointer location." >&2
    exit 1
fi
for movement_attempt in 1 2 3; do
    DISPLAY="$display" xdotool mousemove_relative --sync -- 1 0
done
DISPLAY="$display" xdotool mousemove --sync "$pointer_x" "$pointer_y"
DISPLAY="$display" xdpyinfo >/dev/null

jack_lsp >/dev/null
if ! pgrep -a -x jackd | grep -Eq ' -d dummy( |$)'; then
    echo "JACK is not using the dummy backend." >&2
    exit 1
fi
jack_rate="$(jack_samplerate)"
jack_buffer="$(jack_bufsize)"
nc -z -w 2 127.0.0.1 "$vnc_port"
curl -fsS "http://127.0.0.1:${novnc_port}/vnc.html" >/dev/null

renderer="$(DISPLAY="$display" LIBGL_ALWAYS_SOFTWARE=1 \
    glxinfo -B | sed -n 's/^OpenGL renderer string: //p')"
if [ -z "$renderer" ]; then
    echo "Software OpenGL renderer was not detected." >&2
    exit 1
fi
case "$renderer" in
    *llvmpipe*|*softpipe*|*swrast*|*Software\ Rasterizer*) ;;
    *)
        echo "Renderer does not identify as software: $renderer" >&2
        exit 1
        ;;
esac

if ps -eo stat= | awk '$1 ~ /^Z/ { found=1 } END { exit found ? 0 : 1 }'; then
    echo "Zombie process detected." >&2
    exit 1
fi

workspace='/workspaces/SMS-Plugins'
if [ ! -d "$workspace" ] || [ ! -w "$workspace" ]; then
    echo "Workspace is missing or not writable: $workspace" >&2
    exit 1
fi

printf 'desktop=healthy display=%s jack=%sHz/%sframes novnc=%s vnc=%s renderer=%s\n' \
    "$display" "$jack_rate" "$jack_buffer" "$novnc_port" "$vnc_port" \
    "$renderer"
