#!/bin/sh
set -eu

version=1.1.0

usage()
{
    cat <<EOF
Usage: $0 [OPTIONS]

Capture one Camera2 RAW16 frame and convert it to PNG, JPEG, or both.

Capture options:
  -f, --format FORMAT       png, jpeg, jpg, or both (default: png)
  -c, --camera ID           Camera ID (default: 0)
  -s, --size WIDTHxHEIGHT   RAW16 size (default: 4096x3072)
  -t, --timeout SECONDS     Capture timeout (default: 30)
      --focus MODE          auto, continuous, manual, infinity, or none
                            (default: auto)
      --focus-distance D    Manual focus distance in diopters (default: 0)
      --focus-timeout SEC   Autofocus timeout (default: 3)
      --focus-failure MODE  capture or abort (default: capture)
  -d, --output-dir DIR      Output directory
  -o, --output-prefix PATH  Exact output prefix without an extension
      --force               Replace files with the selected prefix

Rendering options:
  -e, --exposure FACTOR     Linear exposure multiplier, >0 to 32
  -q, --quality N           JPEG quality, 1 to 100 (default: 92)
      --sampling MODE       JPEG sampling: 4:4:4, 4:2:2, or 4:2:0
      --png-compression N   PNG compression level, 0 to 9 (default: 6)
      --progressive         Write a progressive JPEG

Cleanup and output:
      --delete-raw          Delete RAW16 only after conversion succeeds
      --delete-metadata     Delete JSON only after conversion succeeds
      --print-metadata      Print the complete JSON metadata
      --probe FILE          Path to sfos-camera2-probe
      --converter FILE      Path to raw16-to-image.sh
  -h, --help                Show this help
      --version             Show the script version

Environment defaults:
  CAMERA_ID, RAW_SIZE, CAPTURE_TIMEOUT, CAMERA_FOCUS,
  CAMERA_FOCUS_DISTANCE, CAMERA_FOCUS_TIMEOUT, CAMERA_FOCUS_FAILURE,
  OUTPUT_FORMAT, RAW_EXPOSURE, JPEG_QUALITY, JPEG_SAMPLING,
  PNG_COMPRESSION, SFOS_CAMERA2_PROBE, and SFOS_RAW16_IMAGE_CONVERTER
EOF
}

die()
{
    echo "$*" >&2
    exit 2
}

validate_integer()
{
    value=$1
    minimum=$2
    maximum=$3
    label=$4
    case $value in
        ''|*[!0-9]*) die "$label must be an integer from $minimum to $maximum" ;;
    esac
    if [ "$value" -lt "$minimum" ] || [ "$value" -gt "$maximum" ]; then
        die "$label must be an integer from $minimum to $maximum"
    fi
}

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
probe=${SFOS_CAMERA2_PROBE:-/home/defaultuser/sfos-camera2-probe}
converter=${SFOS_RAW16_IMAGE_CONVERTER:-$script_dir/raw16-to-image.sh}
format=${OUTPUT_FORMAT:-png}
camera_id=${CAMERA_ID:-0}
raw_size=${RAW_SIZE:-4096x3072}
timeout=${CAPTURE_TIMEOUT:-30}
focus=${CAMERA_FOCUS:-auto}
focus_distance=${CAMERA_FOCUS_DISTANCE:-0}
focus_timeout=${CAMERA_FOCUS_TIMEOUT:-3}
focus_failure=${CAMERA_FOCUS_FAILURE:-capture}
output_dir=/home/defaultuser/Pictures
prefix=
exposure=${RAW_EXPOSURE:-1.0}
quality=${JPEG_QUALITY:-92}
sampling=${JPEG_SAMPLING:-4:2:0}
png_compression=${PNG_COMPRESSION:-6}
progressive=no
delete_raw=no
delete_metadata=no
print_metadata=no
force=no

while [ "$#" -gt 0 ]; do
    case $1 in
        -f|--format)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            format=$2
            shift 2
            ;;
        -c|--camera)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            camera_id=$2
            shift 2
            ;;
        -s|--size)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            raw_size=$2
            shift 2
            ;;
        -t|--timeout)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            timeout=$2
            shift 2
            ;;
        --focus)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            focus=$2
            shift 2
            ;;
        --focus-distance)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            focus_distance=$2
            shift 2
            ;;
        --focus-timeout)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            focus_timeout=$2
            shift 2
            ;;
        --focus-failure)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            focus_failure=$2
            shift 2
            ;;
        -d|--output-dir)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            output_dir=$2
            shift 2
            ;;
        -o|--output-prefix)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            prefix=$2
            shift 2
            ;;
        -e|--exposure)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            exposure=$2
            shift 2
            ;;
        -q|--quality)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            quality=$2
            shift 2
            ;;
        --sampling)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            sampling=$2
            shift 2
            ;;
        --png-compression)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            png_compression=$2
            shift 2
            ;;
        --progressive)
            progressive=yes
            shift
            ;;
        --delete-raw)
            delete_raw=yes
            shift
            ;;
        --delete-metadata)
            delete_metadata=yes
            shift
            ;;
        --print-metadata)
            print_metadata=yes
            shift
            ;;
        --force)
            force=yes
            shift
            ;;
        --probe)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            probe=$2
            shift 2
            ;;
        --converter)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            converter=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --version)
            echo "$version"
            exit 0
            ;;
        *) die "Unknown option: $1" ;;
    esac
done

case $format in
    jpg) format=jpeg ;;
    png|jpeg|both) ;;
    *) die "Format must be png, jpeg, jpg, or both" ;;
esac
case $raw_size in
    *x*) ;;
    *) die "Size must be written as WIDTHxHEIGHT" ;;
esac
validate_integer "$timeout" 1 3600 "Capture timeout"
validate_integer "$focus_timeout" 1 3600 "Focus timeout"
validate_integer "$quality" 1 100 "JPEG quality"
validate_integer "$png_compression" 0 9 "PNG compression"
case $sampling in
    4:4:4|4:2:2|4:2:0) ;;
    *) die "JPEG sampling must be 4:4:4, 4:2:2, or 4:2:0" ;;
esac
case $focus in
    auto|continuous|manual|infinity|none) ;;
    *) die "Focus must be auto, continuous, manual, infinity, or none" ;;
esac
case $focus_failure in
    capture|abort) ;;
    *) die "Focus failure policy must be capture or abort" ;;
esac
case $focus_distance in
    ''|*[!0-9.]*) die "Focus distance must be a non-negative number" ;;
esac

[ -x "$probe" ] || die "Camera2 probe is not executable: $probe"
if [ ! -x "$converter" ]; then
    converter=/home/defaultuser/raw16-to-image.sh
fi
[ -x "$converter" ] || die "Image converter is not executable: $converter"

mkdir -p "$output_dir"
if [ -z "$prefix" ]; then
    timestamp=$(date +%Y%m%d-%H%M%S)
    prefix=$output_dir/camera2-raw-$timestamp
fi
prefix_dir=$(dirname -- "$prefix")
mkdir -p "$prefix_dir"

if [ "$raw_size" != 4096x3072 ]; then
    echo "Warning: only 4096x3072 is verified on the tested Jolla Phone 2." >&2
    echo "The advertised 1600x1200 RAW16 mode crashed camerahalserver." >&2
fi

echo "Capturing camera $camera_id RAW16 at $raw_size (focus: $focus)..."
if [ "$force" = yes ]; then
    "$probe" --capture --camera "$camera_id" --size "$raw_size" \
        --timeout "$timeout" --focus "$focus" \
        --focus-distance "$focus_distance" \
        --focus-timeout "$focus_timeout" \
        --focus-failure "$focus_failure" --output "$prefix" --force
else
    "$probe" --capture --camera "$camera_id" --size "$raw_size" \
        --timeout "$timeout" --focus "$focus" \
        --focus-distance "$focus_distance" \
        --focus-timeout "$focus_timeout" \
        --focus-failure "$focus_failure" --output "$prefix"
fi

raw_path=$prefix.raw16
metadata_path=$prefix.json
[ -s "$raw_path" ] || die "Capture did not create RAW16 data: $raw_path"
[ -s "$metadata_path" ] || die "Capture did not create metadata: $metadata_path"

convert_png()
{
    echo "Rendering $prefix.png..."
    if [ "$force" = yes ]; then
        "$converter" --format png --exposure "$exposure" \
            --png-compression "$png_compression" --force \
            --output "$prefix.png" "$metadata_path"
    else
        "$converter" --format png --exposure "$exposure" \
            --png-compression "$png_compression" \
            --output "$prefix.png" "$metadata_path"
    fi
    [ -s "$prefix.png" ] || die "PNG conversion failed"
}

convert_jpeg()
{
    echo "Rendering $prefix.jpg..."
    if [ "$progressive" = yes ] && [ "$force" = yes ]; then
        "$converter" --format jpeg --exposure "$exposure" \
            --quality "$quality" --sampling "$sampling" \
            --progressive --force --output "$prefix.jpg" "$metadata_path"
    elif [ "$progressive" = yes ]; then
        "$converter" --format jpeg --exposure "$exposure" \
            --quality "$quality" --sampling "$sampling" \
            --progressive --output "$prefix.jpg" "$metadata_path"
    elif [ "$force" = yes ]; then
        "$converter" --format jpeg --exposure "$exposure" \
            --quality "$quality" --sampling "$sampling" \
            --force --output "$prefix.jpg" "$metadata_path"
    else
        "$converter" --format jpeg --exposure "$exposure" \
            --quality "$quality" --sampling "$sampling" \
            --output "$prefix.jpg" "$metadata_path"
    fi
    [ -s "$prefix.jpg" ] || die "JPEG conversion failed"
}

case $format in
    png) convert_png ;;
    jpeg) convert_jpeg ;;
    both)
        convert_png
        convert_jpeg
        ;;
esac

if [ "$delete_raw" = yes ]; then
    unlink "$raw_path"
    echo "Deleted $raw_path"
fi
if [ "$print_metadata" = yes ]; then
    echo "Metadata:"
    cat "$metadata_path"
fi
if [ "$delete_metadata" = yes ]; then
    unlink "$metadata_path"
    echo "Deleted $metadata_path"
fi

echo "Finished:"
case $format in
    png) ls -lh "$prefix.png" ;;
    jpeg) ls -lh "$prefix.jpg" ;;
    both) ls -lh "$prefix.png" "$prefix.jpg" ;;
esac
if [ "$delete_raw" != yes ]; then
    ls -lh "$raw_path"
fi
if [ "$delete_metadata" != yes ]; then
    ls -lh "$metadata_path"
fi
