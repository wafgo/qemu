#!/usr/bin/env bash
# Boot the P4 FluxOS WIC hands-off and capture the console until login:.
# Usage: scripts/p4boot.sh [outlog]
#
# The WIC is attached as the read-only backing file of a throwaway,
# size-rounded qcow2 overlay (same trick as
# tests/functional/aarch64/test_am64_bootrom.py::test_fluxos_full_linux_boot)
# so QEMU's sd-card model -- which requires the image size to be a multiple
# of 512 KiB -- accepts it, and guest writes never touch the operator's WIC.
set -u
QEMU_DIR="$(cd "$(dirname "$0")/.." && pwd)"
QEMU="${QEMU_BIN:-$QEMU_DIR/build/qemu-system-aarch64}"
TIBOOT3="${TIBOOT3:-$QEMU_DIR/build/tiboot3-p4.bin}"
WIC="${WIC:-$QEMU_DIR/build/fluxos-p4.wic}"
OUT="${1:-/tmp/p4boot.log}"
HARD_TIMEOUT="${HARD_TIMEOUT:-180}"

if [ -x "$QEMU_DIR/build/qemu-img" ]; then
  QEMU_IMG="$QEMU_DIR/build/qemu-img"
else
  QEMU_IMG="qemu-img"
fi

OVERLAY="$(mktemp -u /tmp/p4boot-overlay.XXXXXX.qcow2)"
cleanup() {
  rm -f "$OVERLAY"
}
trap cleanup EXIT

size=$(stat -c%s "$WIC")
padded=$(( (size + 0x7ffff) & ~0x7ffff ))
"$QEMU_IMG" create -f qcow2 -b "$WIC" -F raw "$OVERLAY" "$padded" >/dev/null

: > "$OUT"
# -serial into a pipe we tee; power off the guest once we see the login prompt.
timeout "$HARD_TIMEOUT" stdbuf -oL "$QEMU" \
    -machine am64-virt -display none \
    -bios "$TIBOOT3" \
    -drive if=sd,format=qcow2,file="$OVERLAY" \
    -serial stdio 2>&1 \
  | stdbuf -oL tee "$OUT" \
  | { grep -q -m1 ' login:' && echo "[p4boot] login reached"; }
# QEMU is killed by SIGPIPE when the grep above closes the pipe, or by timeout.
echo "[p4boot] captured $(wc -l < "$OUT") lines -> $OUT"
