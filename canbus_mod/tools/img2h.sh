#!/bin/bash
# Embed an image as native-size ARGB8888 pixels in a C header.

usage() {
    cat >&2 <<EOF
Usage: ${0##*/} [-o output.h] [-n name] input_image

  -o FILE   output header (default: <input dir>/<name>_argb.h)
  -n NAME   symbol prefix (default: input basename without extension)

Generates:  NAME_W, NAME_H, and  static const uint32_t name_argb[]
EOF
    exit 1
}

out= name=
while getopts ':o:n:h' opt; do
    case $opt in
        o) out=$OPTARG ;;
        n) name=$OPTARG ;;
        *) usage ;;
    esac
done
shift $((OPTIND - 1))
[[ $# -eq 1 ]] || usage

src=$1
[[ -r $src ]] || { echo "error: cannot read '$src'" >&2; exit 1; }

# Default name from the filename, sanitized into a valid C identifier.
if [[ -z $name ]]; then
    base=${src##*/}
    name=${base%.*}
fi
name=$(printf '%s' "$name" | tr -c 'A-Za-z0-9_' '_')
[[ $name =~ ^[0-9] ]] && name=_$name
NAME=$(printf '%s' "$name" | tr '[:lower:]' '[:upper:]')
name=$(printf '%s' "$name" | tr '[:upper:]' '[:lower:]')

[[ -n $out ]] || out=$(dirname "$src")/${name}_argb.h

# ImageMagick 7 uses "magick"; IM6 uses "convert"/"identify".
if command -v magick >/dev/null; then
    im_convert=(magick)
    im_identify=(magick identify)
elif command -v convert >/dev/null; then
    im_convert=(convert)
    im_identify=(identify)
else
    echo "error: ImageMagick not found (apt install imagemagick)" >&2
    exit 1
fi

read -r width height < <("${im_identify[@]}" -format '%w %h' "${src}[0]")

{
    echo "/* Generated from ${src##*/}. Keep the source image for edits. */"
    echo "#ifndef ${NAME}_ARGB_H"
    echo "#define ${NAME}_ARGB_H"
    echo "#define ${NAME}_W $width"
    echo "#define ${NAME}_H $height"
    echo "static const uint32_t ${name}_argb[${NAME}_W * ${NAME}_H] = {"

    # Raw 8-bit RGBA -> one "R G B A" line per pixel -> 8 ARGB words per row.
    "${im_convert[@]}" "${src}[0]" -depth 8 rgba:- |
        od -An -v -tu1 -w4 |
        awk '{
            printf "%s0x%02x%02x%02x%02x", (n % 8 ? ", " : "\t"), $4, $1, $2, $3
            n++
            if (n % 8 == 0) printf ",\n"
        }
        END { if (n % 8) printf ",\n" }'

    echo '};'
    echo '#endif'
} > "$out"

echo "wrote $out (${width}x${height})" >&2