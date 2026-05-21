#!/usr/bin/env bash
#
# compare.sh - dispatcher for the M7 <-> RISC-V portability comparison.
#
# Builds + runs the shared portable probe (portable/kernels.c) on both the
# RISC-V (rv64) and Cortex-M7 toolchains, then compares their output. The
# report separates two classes of difference:
#
#   * Computation  - per-kernel result checksums. ANY mismatch is a real
#                    portability bug and FAILS the run (non-zero exit).
#   * ABI / types  - sizeof(long)/sizeof(ptr)/char-signedness etc. These are
#                    expected to differ (rv64 LP64 vs M7 ILP32) and are
#                    reported as informational only.
#
# Usage:  ./compare/compare.sh        (or: make compare)
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

RV_IMAGE=riscv-baremetal
M7_IMAGE=m7-baremetal
UG="$(id -u):$(id -g)"

# Mode selects which probe set to compare.
MODE="${1:-clean}"
case "$MODE" in
    clean)
        RV_MK="make portable"; RV_BIN="build/portable.elf"
        M7_MK="make";          M7_BIN="build/m7.elf" ;;
    hazards)
        RV_MK="make hazards";  RV_BIN="build/hazards.elf"
        M7_MK="make hazards";  M7_BIN="build/m7_hazards.elf" ;;
    *)
        echo "usage: compare.sh [clean|hazards]" >&2; exit 2 ;;
esac

ensure_image() {                      # $1=image  $2=build-context
    if ! docker image inspect "$1" >/dev/null 2>&1; then
        echo ">> image '$1' not found - building from '$2' ..."
        docker build -t "$1" "$2"
    fi
}

RV_QEMU="qemu-system-riscv64 -M virt \
    -cpu rv64,v=true,vlen=128,zba=true,zbb=true,zbs=true,zfh=true \
    -smp 1 -m 8M -bios none -kernel $RV_BIN -nographic 2>&1"
M7_QEMU="qemu-system-arm -M mps2-an500 -semihosting -nographic \
    -kernel $M7_BIN 2>&1"

ensure_image "$RV_IMAGE" "riscv"
ensure_image "$M7_IMAGE" "m7"

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

echo ">> [$MODE] building + running RISC-V (rv64) leg ..."
docker run --rm -u "$UG" -v "$ROOT:/work" -w /work/riscv "$RV_IMAGE" $RV_MK >/dev/null
RV_RAW="$(docker run --rm -u "$UG" -v "$ROOT:/work" -w /work/riscv "$RV_IMAGE" sh -c "$RV_QEMU")"

echo ">> [$MODE] building + running Cortex-M7 leg ..."
docker run --rm -u "$UG" -v "$ROOT:/work" -w /work/m7 "$M7_IMAGE" $M7_MK >/dev/null
M7_RAW="$(docker run --rm -u "$UG" -v "$ROOT:/work" -w /work/m7 "$M7_IMAGE" sh -c "$M7_QEMU")"

# Normalize: drop CR, blank lines, and QEMU's RVV version notice.
norm() { tr -d '\r' | sed '/^[[:space:]]*$/d; /vector version is not specified/d'; }
printf '%s\n' "$RV_RAW" | norm > "$tmp/rv.txt"
printf '%s\n' "$M7_RAW" | norm > "$tmp/m7.txt"

# Render terminal table + verdict; optionally also emit machine-readable JSON
# (REPORT_JSON) and GitHub job-summary Markdown (REPORT_MD), set by CI. The
# exit code (0=match, 1=diverged) is the gate compare / CI key on.
REPORT_ARGS=(--rv "$tmp/rv.txt" --m7 "$tmp/m7.txt" --mode "$MODE")
[ -n "${REPORT_JSON:-}" ] && REPORT_ARGS+=(--json "$REPORT_JSON")
[ -n "${REPORT_MD:-}" ]   && REPORT_ARGS+=(--md "$REPORT_MD")
python3 "$ROOT/compare/report.py" "${REPORT_ARGS[@]}"
