#!/bin/sh

set -eu

if [ "$#" -ne 5 ]; then
	echo "usage: airoha_pack_bl2.sh <bl21> <bl22.lzma> <bl23.lzma> <flash-table.lzma> <output>" >&2
	exit 2
fi

bl21="$1"
bl22="$2"
bl23="$3"
flash_table="$4"
output="$5"

# BL21 reads this fixed little-endian header before expanding BL22 and BL23.
# The source address matches the EN7581 SRAM layout used by the closed DRAM
# calibration stage.
bl22_length="$(stat -c %s "$bl22")"
bl23_length="$(stat -c %s "$bl23")"
flash_table_length="$(stat -c %s "$flash_table")"
lzma_src=$((0x1e843c00 + 36))
lzma_des=0x08004000
lzma_length="$bl22_length"
lzma_cmd=0
fw_ver=0
reserved=0

write_le32()
{
	value="$1"
	printf "\\$(printf '%03o' $(( value         & 0xff )))"
	printf "\\$(printf '%03o' $(((value >> 8)  & 0xff )))"
	printf "\\$(printf '%03o' $(((value >> 16) & 0xff )))"
	printf "\\$(printf '%03o' $(((value >> 24) & 0xff )))"
}

cp "$bl21" "$output"

{
	write_le32 "$bl22_length"
	write_le32 "$bl23_length"
	write_le32 "$flash_table_length"
	write_le32 "$lzma_src"
	write_le32 "$lzma_des"
	write_le32 "$lzma_length"
	write_le32 "$lzma_cmd"
	write_le32 "$fw_ver"
	write_le32 "$reserved"
} >> "$output"

cat "$bl22" "$bl23" "$flash_table" >> "$output"

# BL21 validates an inverted CRC32 over the packed payload.
crc="$(python3 -c 'import binascii, sys; print(binascii.crc32(sys.stdin.buffer.read()))' < "$output")"
write_le32 "$((0xffffffff ^ crc))" >> "$output"
