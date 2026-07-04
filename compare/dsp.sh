#!/usr/bin/env bash
#
# dsp.sh - dispatcher for the DSP arm (intrinsic-kernel equivalence).
#
# This is a DIFFERENT experiment from compare.sh. compare.sh asks "does the
# SAME portable C auto-lower to the same behavior across ISAs?". The DSP arm
# asks "do hand-written INTRINSIC kernels reproduce the scalar oracle
# bit-exactly on each datapath?" - because that is the fair question for a DSP
# (its value is in intrinsics, not auto-vectorized C). So there is no
# cross-target diff here: each leg's on-target harness (harness/run_kernels.c)
# self-checks its k_* kernels against kernels/golden/<name>.txt and exits 0
# iff every kernel is bit-exact. This script runs each accelerated leg and
# gates on those exit codes.
#
#   RVV     (riscv leg,  -DHAVE_RVV)   : RISC-V Vector 1.0 intrinsics
#   dc233c  (xtensa leg, scalar)       : plumbing baseline / portability point
#   HiFi    (xtensa leg, -DHAVE_HIFI)  : behind the firewall - not run here
#
# Usage:  ./compare/dsp.sh        (or: make compare-dsp)
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
UG="$(id -u):$(id -g)"

ensure_image() {                      # $1=image  $2=build-context
    if ! docker image inspect "$1" >/dev/null 2>&1; then
        echo ">> image '$1' not found - building from '$2' ..."
        docker build -t "$1" "$2"
    fi
}
ensure_image riscv-baremetal  riscv
ensure_image xtensa-baremetal xtensa

# Run one leg's DSP harness; echo its output, return its exit code.
run_leg() {                           # $1=image $2=legdir $3=make-target [extra make args...]
    local img="$1" leg="$2" tgt="$3"; shift 3
    docker run --rm -u "$UG" -v "$ROOT:/work" -w "/work/$leg" "$img" make "$tgt" "$@" 2>&1
}

echo ">> [dsp] RVV intrinsic kernels (riscv rv64, -DHAVE_RVV) ..."
RVV_OUT="$(run_leg riscv-baremetal riscv dsp-run)";       RVV_RC=$?
printf '%s\n' "$RVV_OUT"

echo ">> [dsp] dc233c scalar baseline (xtensa -M sim) ..."
DC_OUT="$(run_leg xtensa-baremetal xtensa run)";          DC_RC=$?
printf '%s\n' "$DC_OUT"

# Pull the per-run kernel pass count for the report (best-effort).
kcount() { printf '%s\n' "$1" | grep -c '^PASS '; }
verdict() { [ "$1" -eq 0 ] && echo PASS || echo FAIL; }

echo
echo "=== DSP arm: intrinsic-kernel equivalence vs golden ==="
printf '%-26s %-22s %-8s %s\n' "leg" "datapath" "kernels" "verdict"
printf '%-26s %-22s %-8s %s\n' "riscv (rv64)"  "RVV 1.0 intrinsics"  "$(kcount "$RVV_OUT")/7" "$(verdict $RVV_RC)"
printf '%-26s %-22s %-8s %s\n' "xtensa (dc233c)" "scalar (baseline)" "$(kcount "$DC_OUT")/7"  "$(verdict $DC_RC)"
echo "note: HiFi intrinsics run behind the firewall (same harness, IMPL=dsp_kernels_hifi)."

# Optional GitHub Job-Summary Markdown (set REPORT_MD, as CI does).
if [ -n "${REPORT_MD:-}" ]; then
    {
        echo '## DSP arm - intrinsic-kernel equivalence (vs golden)'
        echo
        echo '| leg | datapath | kernels | verdict |'
        echo '|---|---|---|---|'
        echo "| riscv (rv64) | RVV 1.0 intrinsics | $(kcount "$RVV_OUT")/7 | $(verdict $RVV_RC) |"
        echo "| xtensa (dc233c) | scalar (baseline) | $(kcount "$DC_OUT")/7 | $(verdict $DC_RC) |"
        echo
        echo '_Intrinsic-kernel results - distinct from the portable-C legs auto-lowering results. HiFi runs behind the firewall._'
    } >> "$REPORT_MD"
fi

if [ "$RVV_RC" -ne 0 ] || [ "$DC_RC" -ne 0 ]; then
    echo "=== DSP arm FAIL (a kernel diverged from golden) ==="
    exit 1
fi
echo "=== DSP arm PASS (every kernel bit-exact vs golden) ==="
