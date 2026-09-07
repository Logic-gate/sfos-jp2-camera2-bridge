#!/bin/sh
set -eu

project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$project_dir"

if ! command -v ~/SailfishOS/bin/sfdk >/dev/null 2>&1; then
    echo "sfdk is not in PATH." >&2
    exit 2
fi

if [ ! -d .~/SailfishOS/bin/sfdk ]; then
    echo "Initializing Sailfish build tree in $project_dir"
    ~/SailfishOS/bin/sfdk build-init
fi

~/SailfishOS/bin/sfdk build-shell make -C sailfish clean all

echo "Built $project_dir/sailfish/sfos-camera2-probe"
echo "Built $project_dir/sailfish/sfos-raw16-to-ppm"
