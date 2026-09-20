#!/bin/sh

set -eu

[ "$#" -eq 3 ] || { echo "usage: $0 <tf-a-tree> <preloader-fip> <bl31-uboot-fip>" >&2; exit 2; }

if [ -n "${AIROHA_SIGN_KEY_PATH:-}" ] && [ -f "$AIROHA_SIGN_KEY_PATH" ]; then
	key_path="$(readlink -f -- "$AIROHA_SIGN_KEY_PATH")"
elif [ -n "${AIROHA_SIGN_KEY:-}" ]; then
	key_path=
elif [ -n "${AIROHA_SIGN_KEY_PATH:-}" ]; then
	echo "Signing key path does not name a file." >&2
	exit 1
else
	echo "FIP signing skipped (key environment unset)."
	exit 0
fi

tfa="$(readlink -f -- "$1")"
preloader="$(readlink -f -- "$2")"
stage2="$(readlink -f -- "$3")"
umask 077
work="$(mktemp -d "$tfa/signing.XXXXXX")"
trap 'rm -rf -- "$work"' EXIT
trap 'exit 1' HUP INT TERM
if [ -z "$key_path" ]; then
	key_path="$work/root.pem"
	printf '%s\n' "$AIROHA_SIGN_KEY" > "$key_path"
fi
unset AIROHA_SIGN_KEY

openssl pkey -in "$key_path" -passin pass: -pubout -out "$work/root.pub"
openssl pkey -pubin -in "$work/root.pub" -text -noout > "$work/root.txt"
grep -q 'Public-Key: (4096 bit)' "$work/root.txt" || {
	echo "Signing requires RSA-4096." >&2
	exit 1
}

make -C "$tfa/tools/cert_create" HOSTCC=cc
fiptool="$tfa/tools/fiptool/fiptool"
cert_create="$tfa/tools/cert_create/cert_create"
"$fiptool" unpack --tb-fw "$work/bl2.bin" "$preloader"
"$fiptool" unpack --soc-fw "$work/bl31.bin" --nt-fw "$work/u-boot.bin" "$stage2"
(
	cd "$work"
	"$cert_create" -n --key-alg rsa --key-size 4096 --hash-alg sha512 \
		--tfw-nvctr 0 --ntfw-nvctr 0 --rot-key "$key_path" \
		--tb-fw bl2.bin --tb-fw-cert tb-fw.crt
	"$cert_create" -n --key-alg rsa --key-size 4096 --hash-alg sha512 \
		--tfw-nvctr 0 --ntfw-nvctr 0 --rot-key "$key_path" \
		--soc-fw bl31.bin --nt-fw u-boot.bin \
		--trusted-key-cert trusted-key.crt \
		--soc-fw-key-cert soc-fw-key.crt --soc-fw-cert soc-fw.crt \
		--nt-fw-key-cert nt-fw-key.crt --nt-fw-cert nt-fw.crt
	"$fiptool" create --align 1024 --tb-fw bl2.bin --tb-fw-cert tb-fw.crt preloader.fip
	"$fiptool" create --align 1024 --soc-fw bl31.bin --nt-fw u-boot.bin \
		--trusted-key-cert trusted-key.crt \
		--soc-fw-key-cert soc-fw-key.crt --soc-fw-cert soc-fw.crt \
		--nt-fw-key-cert nt-fw-key.crt --nt-fw-cert nt-fw.crt stage2.fip
)

# These windows include certificates and padding: BL2 follows the 0x800-byte
# boot prefix; the next-stage FIP occupies the platform's 0x7f800-byte window.
[ "$(wc -c < "$work/preloader.fip")" -le "$((0x1f800))" ] &&
[ "$(wc -c < "$work/stage2.fip")" -le "$((0x7f800))" ] || {
	echo "Signed FIP exceeds the boot image window." >&2
	exit 1
}
cp "$work/preloader.fip" "$preloader"
cp "$work/stage2.fip" "$stage2"
echo "Preloader and BL31/U-Boot FIPs signed."
