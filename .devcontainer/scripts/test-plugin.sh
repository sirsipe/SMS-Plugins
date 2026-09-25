#!/usr/bin/env bash
set -Eeuo pipefail

keep_open=false
if [ "${1:-}" = "--keep-open" ]; then
    keep_open=true
    shift
fi
if [ "$#" -gt 1 ]; then
    echo "Usage: test-plugin [--keep-open] [repository-root]" >&2
    exit 2
fi

repository_root="$(realpath "${1:-$PWD}")"
build_dir="$repository_root/build"
artifact_dir="$build_dir/gui-test"
lv2_uri='https://github.com/sirsipe/SMS-Plugins/SMS-Midichopper'

if [ ! -f "$repository_root/SMS-Midichopper/CMakeLists.txt" ]; then
    echo "Not an SMS-Plugins checkout: $repository_root" >&2
    exit 2
fi

export DISPLAY="${DISPLAY:-:1}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-xcb}"
export LIBGL_ALWAYS_SOFTWARE="${LIBGL_ALWAYS_SOFTWARE:-1}"

desktop-health
# The 16:9 default window is wider than the VNC desktop's initial 1280 pixels.
xrandr --output VNC-0 --mode 1920x1080

git -C "$repository_root" submodule update --init --recursive
cmake -S "$repository_root/SMS-Midichopper" -B "$build_dir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
    -DMIDICHOPPER_BUILD_VST3=ON
cmake --build "$build_dir" --parallel 2
ctest --test-dir "$build_dir" --output-on-failure
LV2_PATH="$build_dir/bin" lv2info "$lv2_uri" >/dev/null

mkdir -p "$artifact_dir"
carla_log="$artifact_dir/carla-single.log"

LV2_PATH="$build_dir/bin" carla-single native lv2 "$lv2_uri" \
    >"$carla_log" 2>&1 &
carla_pid=$!

cleanup() {
    trap - EXIT INT TERM
    if kill -0 "$carla_pid" 2>/dev/null; then
        kill "$carla_pid" 2>/dev/null || true
    fi
    wait "$carla_pid" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 130' INT TERM

plugin_window=''
best_score=1000000
for launch_attempt in $(seq 1 80); do
    if ! kill -0 "$carla_pid" 2>/dev/null; then
        set +e
        wait "$carla_pid"
        carla_status=$?
        set -e
        echo "Carla exited during launch (status $carla_status). See $carla_log" >&2
        exit "$carla_status"
    fi

    while IFS= read -r candidate; do
        [ -n "$candidate" ] || continue
        geometry="$(DISPLAY="$DISPLAY" xdotool getwindowgeometry --shell \
            "$candidate" 2>/dev/null || true)"
        width="$(printf '%s\n' "$geometry" | sed -n 's/^WIDTH=//p')"
        height="$(printf '%s\n' "$geometry" | sed -n 's/^HEIGHT=//p')"
        if ! [[ "$width" =~ ^[0-9]+$ && "$height" =~ ^[0-9]+$ ]]; then
            continue
        fi
        if [ "$width" -lt 700 ] || [ "$height" -lt 500 ]; then
            continue
        fi
        score=$(( (${width} - 1344) * (${width} - 1344) + \
            (${height} - 756) * (${height} - 756) ))
        if [ "$score" -lt "$best_score" ]; then
            plugin_window="$candidate"
            best_score="$score"
        fi
    done < <(DISPLAY="$DISPLAY" xdotool search --onlyvisible \
        --name '^SMS-Midichopper' 2>/dev/null || true)

    [ -z "$plugin_window" ] || break
    sleep 0.25
done

if [ -z "$plugin_window" ]; then
    echo "No visible SMS-Midichopper plug-in window. See $carla_log" >&2
    exit 1
fi

window_name="$(DISPLAY="$DISPLAY" xdotool getwindowname "$plugin_window")"
DISPLAY="$DISPLAY" xdotool windowactivate --sync "$plugin_window"
# The original controls are translated by contentOffsetX in MidichopperLayout.hpp.
# ARM is centered at logical coordinate (1202, 166).
DISPLAY="$DISPLAY" xdotool mousemove --sync --window "$plugin_window" 1202 166
DISPLAY="$DISPLAY" xdotool click 1
sleep 0.5

screenshot="$artifact_dir/sms-midichopper-$(date -u +%Y%m%dT%H%M%SZ).png"
screenshot-window "$plugin_window" "$screenshot" >/dev/null

printf 'Carla PID: %s\nPlug-in window: %s (%s)\nWindow-only screenshot: %s\n' \
    "$carla_pid" "$plugin_window" "$window_name" "$screenshot"
echo "Inspect the PNG before retaining or sharing it."

if [ "$keep_open" = true ]; then
    echo "Carla remains open; interrupt this command to close it."
    set +e
    wait "$carla_pid"
    carla_status=$?
    set -e
    exit "$carla_status"
fi
