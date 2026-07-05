# =============================================================================
# isa-portability-lab - top-level orchestrator.
#
# This repo has two peer bare-metal legs (riscv/ and m7/), each with its own
# Dockerfile + Makefile, plus a shared portable/ probe set and a host-side
# compare/ dispatcher. This Makefile drives the cross-target workflows; for
# single-leg work, build inside that leg's container directly, e.g.:
#
#   docker run --rm -v "$PWD:/work" -w /work/riscv riscv-baremetal make
#   docker run --rm -v "$PWD:/work" -w /work/m7    m7-baremetal    make
# =============================================================================
.PHONY: images compare compare-hazards compare-codegen compare-dsp capabilities check-capabilities clean help

help:
	@echo "isa-portability-lab targets:"
	@echo "  make images          build both toolchain images (riscv-baremetal, m7-baremetal)"
	@echo "  make compare         compile the portable probes on both legs and diff results"
	@echo "  make compare-hazards same, on the deliberately non-portable probes (demo)"
	@echo "  make compare-codegen compare HOW the code compiled (RVV vs scalar, libcalls, size)"
	@echo "  make compare-dsp     DSP arm: intrinsic kernels (RVV + dc233c) vs golden, bit-exact"
	@echo "  make capabilities    regenerate the README 'What runs today' table from the golden roster"
	@echo "  make clean           remove all legs' build/ dirs"
	@echo ""
	@echo "Single-leg builds run inside each container; see riscv/ and m7/."

# Build both toolchain images (the compare scripts also build-on-demand).
images:
	docker build -t riscv-baremetal riscv
	docker build -t m7-baremetal    m7

# Behavioral comparison: same inputs -> same outputs. Fails if any diverge.
compare:
	@./compare/compare.sh clean

# Same dispatcher on the hazard probes; divergence is expected (the demo), so
# a non-zero result is not a build error here.
compare-hazards:
	@./compare/compare.sh hazards || true

# Codegen fingerprint: compares HOW the same C compiled on each target
# (instruction mix, FMA, libcalls, size). Diagnostic, not a gate.
compare-codegen:
	@./compare/codegen.sh clean

# DSP arm: hand-written intrinsic kernels (RVV on rv64, scalar on dc233c) must
# reproduce the scalar oracle bit-exactly (vs kernels/golden/). Gates on the
# per-leg exit code. Distinct from compare (auto-lowering behavioral diff).
compare-dsp:
	@./compare/dsp.sh

# Regenerate the README "What runs today" table from the golden kernel roster.
# The generator hard-errors if golden/*.txt, dsp_kernels.h, and its label map
# disagree - so the table (and the roster) cannot silently drift.
capabilities:
	@python3 tools/gen-capabilities.py

# CI gate: fail if the committed table is stale vs the roster (no write).
check-capabilities:
	@python3 tools/gen-capabilities.py --check

clean:
	rm -rf riscv/build m7/build xtensa/build
