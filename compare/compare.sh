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

echo ">> [$MODE] building + running RISC-V (rv64) leg ..."
docker run --rm -u "$UG" -v "$ROOT:/work" -w /work/riscv "$RV_IMAGE" $RV_MK >/dev/null
RV_RAW="$(docker run --rm -u "$UG" -v "$ROOT:/work" -w /work/riscv "$RV_IMAGE" sh -c "$RV_QEMU")"

echo ">> [$MODE] building + running Cortex-M7 leg ..."
docker run --rm -u "$UG" -v "$ROOT:/work" -w /work/m7 "$M7_IMAGE" $M7_MK >/dev/null
M7_RAW="$(docker run --rm -u "$UG" -v "$ROOT:/work" -w /work/m7 "$M7_IMAGE" sh -c "$M7_QEMU")"

# Normalize: drop CR, blank lines, and QEMU's RVV version notice.
norm() { tr -d '\r' | sed '/^[[:space:]]*$/d; /vector version is not specified/d'; }
RV="$(printf '%s\n' "$RV_RAW" | norm)"
M7="$(printf '%s\n' "$M7_RAW" | norm)"

# Partition lines.
RV_INFO="$(printf '%s\n' "$RV" | grep -E '^info '            || true)"
M7_INFO="$(printf '%s\n' "$M7" | grep -E '^info '            || true)"
RV_COMP="$(printf '%s\n' "$RV" | grep -E '^([KH][0-9]|=== comb)' || true)"
M7_COMP="$(printf '%s\n' "$M7" | grep -E '^([KH][0-9]|=== comb)' || true)"

row() {   # $1=left $2=right $3=match-mark $4=diff-mark -> side-by-side table
    # paste defaults to a TAB separator; the probe lines contain no tabs.
    paste <(printf '%s\n' "$1") <(printf '%s\n' "$2") \
    | awk -F'\t' -v ok="$3" -v bad="$4" '{
        m = ($1==$2) ? ok : bad
        printf "  %-3s %-32s | %-32s\n", m, $1, $2
      }'
}

echo
echo "================ ABI / type model (informational) ================"
if [ -z "$RV_INFO$M7_INFO" ]; then
    echo "  (this probe set emits no ABI/type lines)"
else
    printf "      %-34s | %-34s\n" "RISC-V (rv64)" "Cortex-M7 (ARM)"
    row "$RV_INFO" "$M7_INFO" "=" "(!)"
fi

echo
echo "================ computation (must match) ========================"
printf "      %-34s | %-34s\n" "RISC-V (rv64)" "Cortex-M7 (ARM)"
row "$RV_COMP" "$M7_COMP" "ok" "XX"

echo
echo "=================================================================="
if diff -q <(printf '%s\n' "$RV_COMP") <(printf '%s\n' "$M7_COMP") >/dev/null; then
    echo "VERDICT: PASS - all kernel results identical across targets."
    echo "         (ABI/type differences above are expected, not failures.)"
    exit 0
else
    echo "VERDICT: FAIL - computation diverged. Mismatched lines:"
    diff <(printf '%s\n' "$RV_COMP") <(printf '%s\n' "$M7_COMP") | sed 's/^/    /'
    exit 1
fi
