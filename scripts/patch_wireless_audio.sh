#!/usr/bin/env bash
set -euo pipefail

fail() {
	printf 'patch_wireless_audio: %s\n' "$*" >&2
	exit 1
}

usage='usage: patch_wireless_audio.sh INPUT_WIRELESS_MOD OUTPUT_WIRELESS_MOD [GAIN]
       patch_wireless_audio.sh --overwrite INPUT_WIRELESS_MOD [GAIN]
GAIN is an integer from 10 to 100 (default: 100).'
[[ $# -ge 2 && $# -le 3 ]] || fail "$usage"
gain=${3-100}
[[ $gain =~ ^(100|[1-9][0-9])$ ]] || fail 'GAIN must be an integer from 10 to 100'
overwrite=0
if [[ $1 == --overwrite ]]; then
	overwrite=1
	input=$2
	output=$input
else
	[[ $1 != --* ]] || fail "$usage"
	input=$1
	output=$2
fi
[[ -f "$input" ]] || fail "input not found: $input"
if ((overwrite)); then
	[[ ! -L "$input" ]] || fail "refusing to overwrite symlink: $input"
	source_digest=$(sha256sum -- "$input" | cut -d' ' -f1)
else
	[[ ! -e "$output" && ! -L "$output" ]] || fail "output already exists: $output"
fi

readelf=${READELF:-readelf}
objdump=${OBJDUMP:-riscv64-unknown-elf-objdump}
header=$("$readelf" -W -h "$input") || fail 'readelf could not read input'
grep -Eq 'Machine:[[:space:]]+RISC-V' <<< "$header" || fail 'input must be RISC-V ELF'
grep -Eq 'Data:.*little endian' <<< "$header" || fail 'input must be little endian'

sections=$("$readelf" -W -S "$input") || fail 'readelf could not list sections'
text_section=$(sed -nE 's/^[[:space:]]*\[[[:space:]]*[0-9]+\][[:space:]]+\.text[[:space:]]+PROGBITS[[:space:]]+([[:xdigit:]]+)[[:space:]]+([[:xdigit:]]+)[[:space:]]+([[:xdigit:]]+).*/\1 \2 \3/p' <<< "$sections")
[[ -n "$text_section" && "$text_section" != *$'\n'* ]] || fail 'expected exactly one .text section'
read -r text_vma text_offset text_size <<< "$text_section"
text_start=$((16#$text_vma))
text_file_start=$((16#$text_offset))
text_end=$((text_start + 16#$text_size))

dump=$(mktemp "${TMPDIR:-/tmp}/wireless-audio.XXXXXX")
temp_output=
cleanup() {
	rm -f -- "$dump"
	if [[ -n "$temp_output" ]]; then
		rm -f -- "$temp_output"
	fi
}
trap cleanup EXIT
"$objdump" -d -w -j .text "$input" > "$dump" || fail 'objdump could not disassemble .text'

# The paired immediates identify the scaling routine; the window check below
# distinguishes it from unrelated constants in the large wireless module.
candidates=$(awk '
	/^[[:space:]]*[[:xdigit:]]+:/ {
		if (previous != "" && $2 == "06400f93" && $3 == "li" && $4 == "t6,100")
			print previous
		previous = ""
		if ($2 == "03c00f13" && $3 == "li" && $4 == "t5,60") {
			previous = $1
			sub(/:$/, "", previous)
		}
	}
' "$dump")
[[ "$candidates" =~ ^[[:xdigit:]]+$ ]] || fail 'expected exactly one 60/100 scaling candidate'
candidate=${candidates,,}
address=$((16#$candidate))
((address >= text_start + 20 && address + 80 <= text_end)) || fail 'candidate window outside .text'

window=$("$objdump" -d -w -j .text --start-address="$((address - 20))" --stop-address="$((address + 80))" "$input") || fail 'objdump could not inspect candidate'
awk '
	/^[[:space:]]*[[:xdigit:]]+:/ {
		n++
		code[n] = $2
		op[n] = $3
		args[n] = $4
	}
	END {
		for (i = 6; i <= n; i++) {
			if (code[i] != "03c00f13" || code[i+1] != "06400f93")
				continue
			if (op[i-5] != "lui" || args[i-5] != "a5,0xc" ||
			    op[i-4] != "addiw" || args[i-4] != "a5,a5,-1152" ||
			    op[i-3] != "divuw" || args[i-3] != "a2,a5,a2" ||
			    op[i-1] != "li" || args[i-1] != "t1,0" ||
			    op[i+2] != "slliw" || args[i+2] != "a2,a2,0x1" ||
			    op[i+3] != "divuw" || args[i+3] != "t4,a2,a3")
				continue
			for (j = i+4; j < n && j <= i+28; j++)
				if (op[j] == "mulw" && args[j] == "a5,a5,t5" &&
				    op[j+1] == "divw" && args[j+1] == "a5,a5,t6")
					exit 0
		}
		exit 1
	}
' <<< "$window" || fail 'candidate does not match the Android Auto sample-scaling routine'

file_offset=$((text_file_start + address - text_start))
original=$(od -An -tx1 -N4 -j "$file_offset" "$input" | tr -d '[:space:]')
[[ "$original" == 130fc003 ]] || fail 'candidate bytes differ from li t5,60'

printf -v gain_low_hex '%02x' "$(((gain & 15) << 4))"
printf -v gain_high_hex '%02x' "$((gain >> 4))"
expected_bytes="130f${gain_low_hex}${gain_high_hex}"
printf -v gain_low_escape '\\x%02x' "$(((gain & 15) << 4))"
printf -v gain_high_escape '\\x%02x' "$((gain >> 4))"
printf -v instruction_hex '%08x' "$(((gain << 20) | 0x0f13))"

output_dir=$(dirname -- "$output")
[[ -d "$output_dir" ]] || fail "output directory not found: $output_dir"
temp_output=$(mktemp "$output_dir/.wireless-audio.XXXXXX")
cp -p -- "$input" "$temp_output"
printf '\x13\x0f%b%b' "$gain_low_escape" "$gain_high_escape" | dd of="$temp_output" bs=1 seek="$file_offset" conv=notrunc status=none

[[ $(stat -c %s -- "$input") == "$(stat -c %s -- "$temp_output")" ]] || fail 'output size changed'
[[ $(od -An -tx1 -N4 -j "$file_offset" "$temp_output" | tr -d '[:space:]') == "$expected_bytes" ]] || fail "patched bytes differ from li t5,$gain"
cmp -s -n "$file_offset" -- "$input" "$temp_output" || fail 'bytes before instruction changed'
cmp -s -i "$((file_offset + 4)):$((file_offset + 4))" -- "$input" "$temp_output" || fail 'bytes after instruction changed'
patched=$("$objdump" -d -w -j .text --start-address="$address" --stop-address="$((address + 8))" "$temp_output") || fail 'objdump could not verify output'
grep -Eq "${instruction_hex}[[:space:]]+li[[:space:]]+t5,$gain" <<< "$patched" || fail "patched instruction did not disassemble as li t5,$gain"

if ((overwrite)); then
	[[ ! -L "$input" ]] || fail "input became a symlink during patching: $input"
	[[ $(sha256sum -- "$input" | cut -d' ' -f1) == "$source_digest" ]] || fail 'input changed during patching'
else
	[[ ! -e "$output" && ! -L "$output" ]] || fail "output appeared during patching: $output"
fi
mv -- "$temp_output" "$output"
temp_output=
printf 'Patched %s -> %s: VMA 0x%x, file offset 0x%x, gain 60/100 -> %s/100\n' "$input" "$output" "$address" "$file_offset" "$gain"
