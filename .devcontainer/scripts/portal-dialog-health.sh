#!/usr/bin/env bash
set -Eeuo pipefail

display="${DISPLAY:-:1}"
state_dir="${XDG_STATE_HOME:-$HOME/.local/state}/sms-plugins-devcontainer"
mkdir -p "$state_dir"

if [ -z "${DBUS_SESSION_BUS_ADDRESS:-}" ]; then
    echo "DBUS_SESSION_BUS_ADDRESS is not set." >&2
    exit 1
fi

dialog_pid=''
cleanup() {
    trap - EXIT INT TERM
    if [ -n "$dialog_pid" ] && kill -0 "$dialog_pid" 2>/dev/null; then
        kill "$dialog_pid" 2>/dev/null || true
        wait "$dialog_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT INT TERM

run_dialog() {
    local kind="$1"
    local title="$2"
    local log_file="$state_dir/portal-${kind}-health.log"
    local window_id=''

    DISPLAY="$display" /usr/local/libexec/portal-dialog-health \
        "$kind" "$title" >"$log_file" 2>&1 &
    dialog_pid="$!"

    for readiness_attempt in $(seq 1 100); do
        window_id="$(DISPLAY="$display" xdotool search --onlyvisible \
            --name "$title" 2>/dev/null | head -n 1 || true)"
        if [ -n "$window_id" ]; then
            break
        fi
        if ! kill -0 "$dialog_pid" 2>/dev/null; then
            wait "$dialog_pid" || true
            echo "Portal $kind request exited before showing a dialog. See $log_file" >&2
            exit 1
        fi
        if [ "$readiness_attempt" -eq 100 ]; then
            echo "Timed out waiting for the portal $kind dialog. See $log_file" >&2
            exit 1
        fi
        sleep 0.1
    done

    DISPLAY="$display" xdotool windowactivate --sync "$window_id"
    DISPLAY="$display" xdotool key --window "$window_id" Escape
    if ! wait "$dialog_pid"; then
        dialog_pid=''
        echo "Portal $kind dialog did not complete cleanly. See $log_file" >&2
        exit 1
    fi
    dialog_pid=''
}

run_dialog open 'SMS portal Open health check'
run_dialog save 'SMS portal Save health check'
echo "portal-dialogs=healthy display=$display open=cancelled save=cancelled"
