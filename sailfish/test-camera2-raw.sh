#!/bin/sh
set -eu

probe=${SFOS_CAMERA2_PROBE:-/home/defaultuser/sfos-camera2-probe}
output_dir=${1:-/home/defaultuser/Pictures}
camera_id=${CAMERA_ID:-0}
raw_size=${RAW_SIZE:-4096x3072}
timeout=${CAPTURE_TIMEOUT:-30}
focus=${CAMERA_FOCUS:-auto}
focus_distance=${CAMERA_FOCUS_DISTANCE:-0}
focus_timeout=${CAMERA_FOCUS_TIMEOUT:-3}
focus_failure=${CAMERA_FOCUS_FAILURE:-capture}

if [ ! -x "$probe" ]; then
    echo "Camera2 probe is not executable: $probe" >&2
    exit 2
fi

mkdir -p "$output_dir"
timestamp=$(date +%Y%m%d-%H%M%S)
prefix="$output_dir/camera2-raw-$timestamp"

echo "Capturing camera $camera_id RAW16 at $raw_size (focus: $focus)..."
"$probe" --capture \
    --camera "$camera_id" \
    --size "$raw_size" \
    --timeout "$timeout" \
    --focus "$focus" \
    --focus-distance "$focus_distance" \
    --focus-timeout "$focus_timeout" \
    --focus-failure "$focus_failure" \
    --output "$prefix"

echo "Capture files:"
ls -lh "$prefix.raw16" "$prefix.json"
echo "Metadata:"
cat "$prefix.json"
