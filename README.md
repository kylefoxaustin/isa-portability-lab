# isa-portability-lab

A lab for measuring **how hard it is to move bare-metal C across instruction
sets**. It compiles the *same* portable C with two independent toolchains —
RISC-V `rv64` (xPack `riscv-none-elf-gcc`) and ARM Cortex-M7
(`arm-none-eabi-gcc`) — runs each under QEMU, and compares the results two ways:

- **Behavior** — do the two targets compute the same outputs? (correctness portability)
- **Codegen** — did the compilers produce *similar* machine code, or did the ISA
  force different lowering? (performance portability)

Each target is a self-contained, swappable Docker "leg." You can drive a
**comparison** across both, or just use **one leg standalone** as a bare-metal
toolchain in a box.

## Layout

The two legs are symmetric peers; the probe sources and dispatcher are shared.

```
.
├── Makefile              orchestrator: compare / compare-hazards / compare-codegen / images
├── riscv/                RISC-V rv64 leg  - Dockerfile + Makefile + sources + its own README
├── m7/                   Cortex-M7 leg    - Dockerfile + Makefile + sources
├── portable/             shared probe sources, compiled identically by BOTH legs
│   ├── probe.h           REGISTER_PROBE registry + checksum helper
│   ├── probe_runner.c    shared driver that walks the registry
│   ├── probes/           portable probe set   (drop a .c here to add one)
│   └── hazards/          deliberately non-portable probes (the failure demo)
├── compare/              host-side dispatcher
│   ├── compare.sh        runs both legs and diffs results        (make compare)
│   ├── codegen.sh        dumps + compares generated code          (make compare-codegen)
│   └── codegen.py        ISA-neutral codegen fingerprint analyzer
└── .github/workflows/    CI: self-test + cross-target compare on every push
```

## Two ways to use it

### 1. Cross-target comparison (the dispatcher)

Drop the C you want to analyze into `portable/probes/` (see "Adding a probe"),
then from the repo root:

```bash
make compare           # compile on both legs, run both, diff results -> PASS/FAIL
make compare-hazards   # same, on deliberately non-portable probes (a demo)
make compare-codegen   # compare HOW it compiled: RVV vs scalar, FMA, libcalls, size
```

`make compare` builds the toolchain images on first use (or `make images` to
pre-build), runs the probe on both targets, and prints a report split into:

- **computation** — per-probe result checksums. Any mismatch is a real
  portability bug and fails the run (non-zero exit).
- **ABI / type model** — `sizeof(long)`, pointer size, char signedness, etc.
  Expected to differ (rv64 LP64 vs M7 ILP32); reported as *informational*.

Headline finding so far: well-typed portable C is **bit-identical** across both
targets (including floating-point) — the only differences are ABI facts. The
porting friction is the data model, not the computation. `compare-codegen`
additionally shows that at `-O2` RISC-V auto-vectorizes to **RVV** while
Cortex-M7 stays **scalar** (no SIMD unit): same output, very different code.

### 2. Standalone — one leg, no comparison

Each leg is a normal bare-metal toolchain you can use on its own. The repo root
is mounted at `/work` (so the build can reach `portable/`) and the working dir
is set to the leg:

```bash
# RISC-V: build, run the 35-test RVV self-test suite, run in QEMU
docker build -t riscv-baremetal riscv
docker run --rm -v "$PWD:/work" -w /work/riscv riscv-baremetal make        # build
docker run --rm -v "$PWD:/work" -w /work/riscv riscv-baremetal make test   # self-test
docker run --rm -it -v "$PWD:/work" -w /work/riscv riscv-baremetal make run

# Cortex-M7: build + run
docker build -t m7-baremetal m7
docker run --rm -v "$PWD:/work" -w /work/m7 m7-baremetal make
docker run --rm -v "$PWD:/work" -w /work/m7 m7-baremetal make run
```

The RISC-V leg ships a 35-test self-checking RVV/Zb*/Zfh regression suite and a
stubbed accelerator-intrinsic seam — see [`riscv/README.md`](riscv/README.md)
for the full details.

## Adding a probe (drop-in)

A probe is a function returning a `uint32_t` checksum of its result, registered
with `REGISTER_PROBE`. Drop a `.c` file into `portable/probes/` — **no Makefile
or table edits**. Both legs glob the directory and a linker-section registry
auto-discovers it (see `portable/probes/p_extra.c` for a template):

```c
#include "probe.h"
static uint32_t my_probe(void) { /* compute into buf ... */ return probe_fnv1a(buf, n); }
REGISTER_PROBE("K99 my_probe    ", my_probe);
```

Then `make compare` (and `make compare-codegen`) pick it up on both targets.
Use fixed-width types (`uint32_t`, …) for portable computation; lean on `long`,
pointer width, or `long double` and the harness will flag the divergence — that
is exactly what `portable/hazards/` demonstrates.

## Upgrading a leg

Each leg owns its `Dockerfile`, `Makefile`, and sources, so a toolchain bump
touches **one directory** and never the other leg or the shared probes:

- **RISC-V**: bump `ARG XPACK_VERSION` in `riscv/Dockerfile`, then
  `docker build -t riscv-baremetal riscv`.
- **Cortex-M7**: change the toolchain install in `m7/Dockerfile` (or pin a
  specific `gcc-arm-none-eabi`), then `docker build -t m7-baremetal m7`.

Adjust that leg's CPU/ISA flags in its own `Makefile` (e.g. RISC-V `ARCH`/`ABI`,
M7 `MCPU`) as needed. Re-run `make compare` to confirm the cross-target results
still hold. Adding a *third* leg is the same shape: a new peer directory with a
Dockerfile + Makefile that compiles the shared `portable/` set, plus an arm in
`compare/`.

## CI and run reports

`.github/workflows/ci.yml` runs on every push and PR (with Docker layer
caching): it builds both toolchain images, runs the RISC-V self-test suite +
negative-path check, runs `make compare` (must match), asserts the hazard
probes still diverge (so a broken detector also fails CI), and prints the
codegen fingerprint as an informational, non-gating step.

Each run reports its results natively on GitHub — no external dashboard needed:

- **Job Summary**: the comparison renders as Markdown tables (computation
  PASS/FAIL + the ABI diff) right on the run's page, with the codegen
  fingerprint folded into a collapsible block.
- **Artifact**: a machine-readable `report.json` is uploaded on every run
  (`compute` rows, `abi` rows, `combined` hash, `verdict`).

The same outputs are available locally — set `REPORT_MD=summary.md` and/or
`REPORT_JSON=report.json` when running `make compare`. `report.json` is the
stable contract a future dashboard (e.g. a Streamlit cross-run trend view)
would consume.

## Requirements

Docker, GNU `make`, `bash`, and `python3` on the host. The toolchains and QEMU
live inside the images — nothing else to install.
