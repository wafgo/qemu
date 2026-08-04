#!/usr/bin/env bash
# Boot the P4 FluxOS WIC hands-off and capture the console until login:.
# Usage: scripts/p4boot.sh [outlog]
# Exit status: 0 if the login prompt was reached, 1 if not (timeout or QEMU
# exited early), 2 on a setup failure (missing WIC, qemu-img create failed).
#
# The WIC is attached as the read-only backing file of a throwaway,
# size-rounded qcow2 overlay (same trick as
# tests/functional/aarch64/test_am64_bootrom.py::test_fluxos_full_linux_boot)
# so QEMU's sd-card model -- which requires the image size to be a multiple
# of 512 KiB -- accepts it, and guest writes never touch the operator's WIC.
set -uo pipefail
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

OVERLAY="$(mktemp /tmp/p4boot-overlay.XXXXXX.qcow2)"
cleanup() {
  rm -f "$OVERLAY"
}
trap cleanup EXIT

size=$(stat -c%s "$WIC") || { echo "[p4boot] cannot stat WIC: $WIC" >&2; exit 2; }
padded=$(( (size + 0x7ffff) & ~0x7ffff ))
"$QEMU_IMG" create -f qcow2 -b "$WIC" -F raw "$OVERLAY" "$padded" >/dev/null \
    || { echo "[p4boot] qemu-img create failed" >&2; exit 2; }

: > "$OUT"
# Run QEMU in the background writing straight to the log, then poll the log
# for the login prompt. The guest goes idle right after printing it (no more
# output, so a tee|grep pipe would never see SIGPIPE) -- explicitly kill QEMU
# as soon as we see the prompt, or when it exits on its own, or at
# HARD_TIMEOUT, whichever comes first.
"$QEMU" -machine am64-virt -display none \
    -bios "$TIBOOT3" \
    -drive if=sd,format=qcow2,file="$OVERLAY" \
    -serial stdio > "$OUT" 2>&1 &
qpid=$!

reached=1
for ((i = 0; i < HARD_TIMEOUT; i++)); do
  if grep -q ' login:' "$OUT"; then
    reached=0
    break
  fi
  kill -0 "$qpid" 2>/dev/null || break # QEMU exited on its own
  sleep 1
done

kill "$qpid" 2>/dev/null
wait "$qpid" 2>/dev/null

if [ "$reached" -eq 0 ]; then
  echo "[p4boot] login reached"
else
  echo "[p4boot] login NOT reached" >&2
fi
echo "[p4boot] captured $(wc -l < "$OUT") lines -> $OUT"
exit "$reached"
