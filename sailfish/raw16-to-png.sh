#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "Usage: $0 METADATA.json [OUTPUT.png]" >&2
    exit 2
fi

metadata=$1
case $metadata in
    *.json) default_output=${metadata%.json}.png ;;
    *) default_output=$metadata.png ;;
esac
output=${2:-$default_output}
exposure=${RAW_EXPOSURE:-1.0}

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
converter=${SFOS_RAW16_CONVERTER:-$script_dir/sfos-raw16-to-ppm}
if [ ! -x "$converter" ]; then
    converter=/home/defaultuser/sfos-raw16-to-ppm
fi
if [ ! -x "$converter" ]; then
    echo "RAW converter is not executable: $converter" >&2
    exit 3
fi

imagemagick=${MAGICK:-}
if [ -z "$imagemagick" ]; then
    if command -v magick >/dev/null 2>&1; then
        imagemagick=magick
    elif command -v convert >/dev/null 2>&1; then
        imagemagick=convert
    else
        echo "ImageMagick (magick or convert) was not found" >&2
        exit 4
    fi
fi

"$converter" "$metadata" "$exposure" |
    "$imagemagick" ppm:- -define png:color-type=2 "$output"

echo "Created $output"
