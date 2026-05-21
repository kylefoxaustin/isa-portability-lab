#!/usr/bin/env bash
#
# codegen.sh - codegen fingerprint comparison (M7 vs RISC-V).
#
# Builds the portable probe on both toolchains, pulls each ELF's disassembly,
# and runs the host-side analyzer (compare/codegen.py) to compare codegen
# *shape* per kernel - instruction mix, FMA use, libcalls, size. This is a
# diagnostic, not a pass/fail gate (you can't diff incomparable ISAs).
#
# Usage:  ./compare/codegen.sh [clean|hazards]
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

RV_IMAGE=riscv-baremetal
M7_IMAGE=m7-baremetal
UG="$(id -u):$(id -g)"

MODE="${1:-clean}"
case "$MODE" in
    clean)
        RV_MK="make portable"; RV_BIN="build/portable.elf"
        M7_MK="make";          M7_BIN="build/m7.elf";        PREFIX="k_" ;;
    hazards)
        RV_MK="make hazards";  RV_BIN="build/hazards.elf"
        M7_MK="make hazards";  M7_BIN="build/m7_hazards.elf"; PREFIX="h_" ;;
    *)
        echo "usage: codegen.sh [clean|hazards]" >&2; exit 2 ;;
esac

ensure_image() {
    if ! docker image inspect "$1" >/dev/null 2>&1; then
        echo ">> image '$1' not found - building from '$2' ..."
        docker build -t "$1" "$2"
    fi
}
ensure_image "$RV_IMAGE" "riscv"
ensure_image "$M7_IMAGE" "m7"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

echo ">> [$MODE] building both legs + dumping disassembly ..."
docker run --rm -u "$UG" -v "$ROOT:/work" -w /work/riscv "$RV_IMAGE" $RV_MK >/dev/null
docker run --rm -u "$UG" -v "$ROOT:/work" -w /work/riscv "$RV_IMAGE" \
    sh -c "riscv-none-elf-objdump -d $RV_BIN" > "$tmp/rv.dis"
docker run --rm -u "$UG" -v "$ROOT:/work" -w /work/m7 "$M7_IMAGE" $M7_MK >/dev/null
docker run --rm -u "$UG" -v "$ROOT:/work" -w /work/m7 "$M7_IMAGE" \
    sh -c "arm-none-eabi-objdump -d $M7_BIN" > "$tmp/m7.dis"

echo
python3 compare/codegen.py "$tmp/rv.dis" "$tmp/m7.dis" "$PREFIX"
