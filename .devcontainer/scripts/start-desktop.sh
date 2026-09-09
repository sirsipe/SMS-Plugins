#!/usr/bin/env bash
set -Eeuo pipefail

display="${DISPLAY:-:1}"
display_number="${display#:}"
desktop_geometry="${DESKTOP_GEOMETRY:-1280x900}"
desktop_depth="${DESKTOP_DEPTH:-24}"
vnc_port="${VNC_PORT:-5901}"
novnc_port="${NOVNC_PORT:-6080}"
runtime_dir="${XDG_RUNTIME_DIR:-/tmp/runtime-vscode}"
state_dir="${XDG_STATE_HOME:-$HOME/.local/state}/sms-plugins-devcontainer"
password_file="$HOME/.config/sms-plugins-devcontainer/tigervnc.passwd"

if ! [[ "$display_number" =~ ^[0-9]+$ ]]; then
    echo "DISPLAY must be a simple numeric X11 display, got: $display" >&2
    exit 2
fi
if ! [[ "$vnc_port" =~ ^[0-9]+$ && "$novnc_port" =~ ^[0-9]+$ ]]; then
    echo "VNC_PORT and NOVNC_PORT must be numeric." >&2
    exit 2
fi

export DISPLAY="$display"
export XDG_RUNTIME_DIR="$runtime_dir"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"

mkdir -p "$runtime_dir" "$state_dir" /tmp/.X11-unix
chmod 700 "$runtime_dir"

lock_file="/tmp/.X${display_number}-lock"
socket_file="/tmp/.X11-unix/X${display_number}"

if [ -e "$lock_file" ] || [ -S "$socket_file" ]; then
    if DISPLAY="$display" xdpyinfo >/dev/null 2>&1; then
        echo "An X server is already running on $display." >&2
        exit 1
    fi
    rm -f -- "$lock_file" "$socket_file"
fi

critical_pids=()
critical_names=()

cleanup() {
    trap - EXIT INT TERM
    for service_pid in "${critical_pids[@]}"; do
        kill "$service_pid" 2>/dev/null || true
    done
    wait "${critical_pids[@]}" 2>/dev/null || true
}

trap cleanup EXIT
trap 'exit 0' INT TERM

vnc_security=(-SecurityTypes None)
if [ -s "$password_file" ]; then
    vnc_security=(-SecurityTypes VncAuth -PasswordFile "$password_file")
fi

Xtigervnc "$display" \
    -geometry "$desktop_geometry" \
    -depth "$desktop_depth" \
    -rfbport "$vnc_port" \
    -localhost no \
    -AlwaysShared \
    "${vnc_security[@]}" \
    >"$state_dir/tigervnc.log" 2>&1 &
critical_pids+=("$!")
critical_names+=("TigerVNC")

for readiness_attempt in $(seq 1 100); do
    if DISPLAY="$display" xdpyinfo >/dev/null 2>&1; then
        break
    fi
    if ! kill -0 "${critical_pids[0]}" 2>/dev/null; then
        echo "TigerVNC exited during startup. See $state_dir/tigervnc.log" >&2
        exit 1
    fi
    if [ "$readiness_attempt" -eq 100 ]; then
        echo "Timed out waiting for X11 display $display." >&2
        exit 1
    fi
    sleep 0.1
done

xsetroot -solid '#30343b'

openbox-session >"$state_dir/openbox.log" 2>&1 &
critical_pids+=("$!")
critical_names+=("Openbox")

jackd --no-realtime --no-mlock -d dummy -r 48000 -p 2048 \
    >"$state_dir/jack.log" 2>&1 &
critical_pids+=("$!")
critical_names+=("JACK")

websockify --web=/usr/share/novnc "0.0.0.0:${novnc_port}" \
    "127.0.0.1:${vnc_port}" >"$state_dir/novnc.log" 2>&1 &
critical_pids+=("$!")
critical_names+=("noVNC")

xterm -title 'SMS container terminal' -geometry 100x28+20+20 \
    >"$state_dir/xterm.log" 2>&1 &

for readiness_attempt in $(seq 1 100); do
    if jack_lsp >/dev/null 2>&1 \
        && curl -fsS "http://127.0.0.1:${novnc_port}/vnc.html" >/dev/null; then
        break
    fi
    if [ "$readiness_attempt" -eq 100 ]; then
        echo "Timed out waiting for desktop services. See $state_dir/*.log" >&2
        exit 1
    fi
    sleep 0.1
done

echo "Container desktop ready: DISPLAY=$display, VNC=$vnc_port, noVNC=$novnc_port"
echo "Service logs: $state_dir"

set +e
wait -n "${critical_pids[@]}"
desktop_status=$?
set -e

for desktop_index in "${!critical_pids[@]}"; do
    if ! kill -0 "${critical_pids[$desktop_index]}" 2>/dev/null; then
        echo "Critical desktop service exited: ${critical_names[$desktop_index]}" >&2
    fi
done

if [ "$desktop_status" -eq 0 ]; then
    desktop_status=1
fi
exit "$desktop_status"
