#!/bin/sh
set -eu

version=1.0.1

usage()
{
    cat <<EOF
Usage: $0 [OPTIONS] METADATA.json [OUTPUT]

Convert a Camera2 RAW16/JSON pair to PNG or JPEG.

Options:
  -f, --format FORMAT       png, jpeg, or jpg (default: infer, then png)
  -o, --output FILE         Output filename
  -e, --exposure FACTOR     Linear exposure multiplier, >0 to 32
  -q, --quality N           JPEG quality, 1 to 100 (default: 92)
  -s, --sampling MODE       JPEG sampling: 4:4:4, 4:2:2, or 4:2:0
      --png-compression N   PNG compression level, 0 to 9 (default: 6)
      --progressive         Write a progressive JPEG
      --baseline            Write a baseline JPEG (default)
      --force               Replace an existing output file
      --converter FILE      Path to sfos-raw16-to-ppm
  -h, --help                Show this help
      --version             Show the script version

Environment defaults:
  RAW_EXPOSURE, JPEG_QUALITY, JPEG_SAMPLING, PNG_COMPRESSION,
  SFOS_RAW16_CONVERTER, and MAGICK
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

format=
output=
metadata=
exposure=${RAW_EXPOSURE:-1.0}
quality=${JPEG_QUALITY:-92}
sampling=${JPEG_SAMPLING:-4:2:0}
png_compression=${PNG_COMPRESSION:-6}
progressive=no
force=no
converter_override=${SFOS_RAW16_CONVERTER:-}

while [ "$#" -gt 0 ]; do
    case $1 in
        -f|--format)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            format=$2
            shift 2
            ;;
        -o|--output)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            output=$2
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
        -s|--sampling)
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
        --baseline)
            progressive=no
            shift
            ;;
        --force)
            force=yes
            shift
            ;;
        --converter)
            [ "$#" -ge 2 ] || die "$1 requires a value"
            converter_override=$2
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
        --)
            shift
            while [ "$#" -gt 0 ]; do
                if [ -z "$metadata" ]; then
                    metadata=$1
                elif [ -z "$output" ]; then
                    output=$1
                else
                    die "Too many arguments"
                fi
                shift
            done
            ;;
        -*) die "Unknown option: $1" ;;
        *)
            if [ -z "$metadata" ]; then
                metadata=$1
            elif [ -z "$output" ]; then
                output=$1
            else
                die "Too many arguments"
            fi
            shift
            ;;
    esac
done

[ -n "$metadata" ] || {
    usage >&2
    exit 2
}
[ -f "$metadata" ] || die "Metadata file not found: $metadata"

if [ -z "$format" ] && [ -n "$output" ]; then
    case $output in
        *.jpg|*.JPG|*.jpeg|*.JPEG) format=jpeg ;;
        *.png|*.PNG) format=png ;;
    esac
fi
format=${format:-png}
case $format in
    jpg) format=jpeg ;;
    png|jpeg) ;;
    *) die "Format must be png, jpeg, or jpg" ;;
esac

if [ -z "$output" ]; then
    case $metadata in
        *.json) output_base=${metadata%.json} ;;
        *) output_base=$metadata ;;
    esac
    if [ "$format" = jpeg ]; then
        output=$output_base.jpg
    else
        output=$output_base.png
    fi
fi

validate_integer "$quality" 1 100 "JPEG quality"
validate_integer "$png_compression" 0 9 "PNG compression"
case $sampling in
    4:4:4|4:2:2|4:2:0) ;;
    *) die "JPEG sampling must be 4:4:4, 4:2:2, or 4:2:0" ;;
esac

if [ -e "$output" ] && [ "$force" != yes ]; then
    die "Output exists; choose another name or pass --force: $output"
fi

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
converter=$converter_override
if [ -z "$converter" ]; then
    converter=$script_dir/sfos-raw16-to-ppm
fi
if [ ! -x "$converter" ]; then
    converter=/home/defaultuser/sfos-raw16-to-ppm
fi
[ -x "$converter" ] || die "RAW converter is not executable: $converter"

imagemagick=${MAGICK:-}
if [ -z "$imagemagick" ]; then
    if command -v magick >/dev/null 2>&1; then
        imagemagick=magick
    elif command -v convert >/dev/null 2>&1; then
        imagemagick=convert
    else
        die "ImageMagick (magick or convert) was not found"
    fi
fi

output_dir=$(dirname -- "$output")
[ -d "$output_dir" ] || die "Output directory does not exist: $output_dir"
temporary_ppm=$(mktemp "$output_dir/.raw16-to-image.XXXXXX") ||
    die "Could not create a temporary image in $output_dir"

cleanup()
{
    if [ -n "${temporary_ppm:-}" ] && [ -e "$temporary_ppm" ]; then
        unlink "$temporary_ppm"
    fi
}
trap cleanup 0
trap 'cleanup; exit 1' HUP INT TERM

"$converter" "$metadata" "$exposure" > "$temporary_ppm"

if [ "$format" = png ]; then
    "$imagemagick" "$temporary_ppm" -colorspace sRGB \
        -define png:color-type=2 \
        -define "png:compression-level=$png_compression" "$output"
else
    if [ "$progressive" = yes ]; then
        "$imagemagick" "$temporary_ppm" -colorspace sRGB \
            -sampling-factor "$sampling" -quality "$quality" \
            -interlace Plane "$output"
    else
        "$imagemagick" "$temporary_ppm" -colorspace sRGB \
            -sampling-factor "$sampling" -quality "$quality" "$output"
    fi
fi

[ -s "$output" ] || die "ImageMagick did not create a usable file: $output"
cleanup
temporary_ppm=

echo "Created $output"
