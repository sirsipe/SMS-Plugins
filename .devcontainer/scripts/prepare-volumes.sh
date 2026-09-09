#!/usr/bin/env bash
set -euo pipefail

resource_label='io.sudometalstudio.sms-plugins-devcontainer=true'
volumes=(
    sms-plugins-devcontainer-codex
    sms-plugins-devcontainer-vnc
)

for volume in "${volumes[@]}"; do
    if docker volume inspect "$volume" >/dev/null 2>&1; then
        label_value="$(docker volume inspect \
            --format '{{index .Labels "io.sudometalstudio.sms-plugins-devcontainer"}}' \
            "$volume")"
        if [ "$label_value" != true ]; then
            echo "Refusing to use existing unlabeled volume: $volume" >&2
            exit 1
        fi
        continue
    fi

    docker volume create --label "$resource_label" "$volume" >/dev/null
done
