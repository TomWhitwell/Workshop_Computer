#!/usr/bin/env bash
set -euo pipefail

UF2_PATH="${1:-build/breaky.uf2}"
PORT="${2:-/dev/ttyACM0}"

if [ ! -f "$UF2_PATH" ]; then
    echo "UF2 not found: $UF2_PATH" >&2
    exit 1
fi

find_rpi_mount() {
    findmnt -rn -o TARGET,LABEL | awk '$2 == "RPI-RP2" { print $1; exit }'
}

MOUNT_POINT="$(find_rpi_mount || true)"

if [ -z "$MOUNT_POINT" ]; then
    if [ -e "$PORT" ]; then
        echo "requesting BOOTSEL via 1200 baud on $PORT"
        stty -F "$PORT" 1200 hupcl 2>/dev/null || true
        timeout 1 bash -c "cat < '$PORT' > /dev/null" 2>/dev/null || true
    else
        echo "warning: $PORT not found; waiting for an already-mounted RPI-RP2 volume" >&2
    fi

    echo "waiting for RPI-RP2"
    for _ in $(seq 1 100); do
        MOUNT_POINT="$(find_rpi_mount || true)"
        if [ -n "$MOUNT_POINT" ]; then
            break
        fi
        sleep 0.1
    done
fi

if [ -z "$MOUNT_POINT" ]; then
    echo "RPI-RP2 mount not found" >&2
    exit 1
fi

echo "copying $UF2_PATH to $MOUNT_POINT"
cp "$UF2_PATH" "$MOUNT_POINT/"
sync "$MOUNT_POINT" || sync
echo "upload complete"
