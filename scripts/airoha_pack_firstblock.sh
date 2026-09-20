#!/bin/sh

set -eu

if [ "$#" -ne 2 ]; then
	echo "usage: airoha_pack_firstblock.sh <preloader> <output>" >&2
	exit 2
fi

preloader="$1"
output="$2"
offset=$((0x800))
block_size=$((0x20000))
length="$(stat -c %s "$preloader")"

# BootROM reads the preloader FIP at byte 0x800 of the first eraseblock.
if [ "$length" -eq 0 ] || [ "$length" -gt "$((block_size - offset))" ]; then
	echo "preloader size $length exceeds the first-block payload range" >&2
	exit 1
fi

# The NAND driver generates OOB ECC when writing this main-area image.
dd if=/dev/zero bs="$block_size" count=1 status=none |
	tr '\000' '\377' > "$output"
dd if="$preloader" of="$output" bs=1 seek="$offset" conv=notrunc status=none
