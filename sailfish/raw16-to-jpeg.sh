#!/bin/sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "Usage: $0 METADATA.json [OUTPUT.jpg]" >&2
    exit 2
fi

metadata=$1
case $metadata in
    *.json) default_output=${metadata%.json}.jpg ;;
    *) default_output=$metadata.jpg ;;
esac
output=${2:-$default_output}
exposure=${RAW_EXPOSURE:-1.0}
quality=${JPEG_QUALITY:-92}
sampling=${JPEG_SAMPLING:-4:2:0}

case $quality in
    ''|*[!0-9]*)
        echo "JPEG_QUALITY must be an integer from 1 to 100" >&2
        exit 3
        ;;
esac
if [ "$quality" -lt 1 ] || [ "$quality" -gt 100 ]; then
    echo "JPEG_QUALITY must be an integer from 1 to 100" >&2
    exit 3
fi

case $sampling in
    4:4:4|4:2:2|4:2:0) ;;
    *)
        echo "JPEG_SAMPLING must be 4:4:4, 4:2:2, or 4:2:0" >&2
        exit 3
        ;;
esac

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
converter=${SFOS_RAW16_CONVERTER:-$script_dir/sfos-raw16-to-ppm}
if [ ! -x "$converter" ]; then
    converter=/home/defaultuser/sfos-raw16-to-ppm
fi
if [ ! -x "$converter" ]; then
    echo "RAW converter is not executable: $converter" >&2
    exit 4
fi

imagemagick=${MAGICK:-}
if [ -z "$imagemagick" ]; then
    if command -v magick >/dev/null 2>&1; then
        imagemagick=magick
    elif command -v convert >/dev/null 2>&1; then
        imagemagick=convert
    else
        echo "ImageMagick (magick or convert) was not found" >&2
        exit 5
    fi
fi

"$converter" "$metadata" "$exposure" |
    "$imagemagick" ppm:- -colorspace sRGB \
        -sampling-factor "$sampling" -quality "$quality" "$output"

echo "Created $output (quality $quality, sampling $sampling)"
