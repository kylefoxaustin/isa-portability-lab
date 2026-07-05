# isa-portability-lab

A lab for measuring **how hard it is to move bare-metal C across instruction
sets**. It compiles the *same* portable C with two independent toolchains —
RISC-V `rv64` (xPack `riscv-none-elf-gcc`) and ARM Cortex-M7
(`arm-none-eabi-gcc`) — runs each under QEMU, and compares the results two ways:

- **Behavior** — do the two targets compute the same outputs? (correctness portability)
- **Codegen** — did the compilers produce *similar* machine code, or did the ISA
  force different lowering? (performance portability)

Each target is a self-contained, swappable Docker "leg." You can drive a
**comparison** across a pair of legs, or just use **one leg standalone** as a
bare-metal toolchain in a box.

A second experiment — the **[DSP arm](#the-dsp-arm-intrinsic-kernels)** — adds a
third leg (Tensilica `dc233c`) and asks a different question: how do hand-written
*intrinsic* kernels (RVV, and HiFi behind a firewall) compare on identical
fixed-point workloads? Intrinsic-kernel results are reported separately from the
portable-C auto-lowering results above.

## Layout

The legs are symmetric peers; the shared sources and host-side dispatchers are
split by experiment — `portable/` + `compare/compare.sh` for the auto-lowering
comparison (riscv + m7), `kernels/` + `harness/` + `compare/dsp.sh` for the DSP
arm (riscv + xtensa).

```
.
├── Makefile              orchestrator: compare / compare-hazards / compare-codegen / compare-dsp / images
├── riscv/                RISC-V rv64 leg  - Dockerfile + Makefile + sources + its own README
├── m7/                   Cortex-M7 leg    - Dockerfile + Makefile + sources
├── xtensa/               Xtensa dc233c leg (DSP arm) - Dockerfile + Makefile + reset/vector glue
├── portable/             shared probe sources for the AUTO-LOWERING experiment (riscv + m7)
│   ├── probe.h           REGISTER_PROBE registry + checksum helper
│   ├── probe_runner.c    shared driver that walks the registry
│   ├── probes/           portable probe set   (drop a .c here to add one)
│   └── hazards/          deliberately non-portable probes (the failure demo)
├── kernels/              shared fixed-point DSP kernels for the INTRINSIC experiment
│   ├── dsp_kernels.h     the kernel contract (Q-format signatures + round/sat helpers)
│   ├── dsp_kernels_scalar.c   the scalar oracle (ground truth) + golden/
│   ├── dsp_kernels_{rvv,hifi,baseline}.c   per-datapath intrinsic impls
│   └── golden/           committed known-good output vectors (source of truth)
├── harness/              on-target DSP equivalence harness + golden pipeline
│   └── run_kernels.c     runs k_* vs golden, PASS/FAIL, exit code = gate
├── compare/              host-side dispatchers
│   ├── compare.sh        runs both legs and diffs results        (make compare)
│   ├── codegen.sh        dumps + compares generated code          (make compare-codegen)
│   ├── codegen.py        ISA-neutral codegen fingerprint analyzer
│   └── dsp.sh            DSP arm: intrinsic kernels vs golden      (make compare-dsp)
└── .github/workflows/    CI: self-test + cross-target compare + DSP arm on every push
```

## Two ways to use it

### 1. Cross-target comparison (the dispatcher)

Drop the C you want to analyze into `portable/probes/` (see "Adding a probe"),
then from the repo root:

```bash
make compare           # compile on both legs, run both, diff results -> PASS/FAIL
make compare-hazards   # same, on deliberately non-portable probes (a demo)
make compare-codegen   # compare HOW it compiled: RVV vs scalar, FMA, libcalls, size
make compare-dsp       # DSP arm: hand-written intrinsic kernels vs golden (see below)
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

# Xtensa dc233c (DSP arm): build + run the fixed-point kernel harness vs golden
docker build -t xtensa-baremetal xtensa
docker run --rm -v "$PWD:/work" -w /work/xtensa xtensa-baremetal make run
```

The RISC-V leg ships a 35-test self-checking RVV/Zb*/Zfh regression suite and a
stubbed accelerator-intrinsic seam — see [`riscv/README.md`](riscv/README.md)
for the full details. The Xtensa leg runs the shared DSP harness on the free
`dc233c` core — see [the DSP arm](#the-dsp-arm-intrinsic-kernels) below.

## The DSP arm (intrinsic kernels)

The `riscv` and `m7` legs above answer *"how does the same portable C
**auto-lower** across ISAs?"* The DSP arm answers a **different** question,
because that one is a category error for a DSP: a DSP's value is in intrinsics
and hand-tuned kernels, not in auto-vectorized portable C.

> **DSP-arm question:** how do hand-written *intrinsic* kernels compare across
> datapaths (**HiFi vs. RVV**) on identical fixed-point workloads?

Results from this arm are **intrinsic-kernel** results and are reported
separately from the portable-C legs' **auto-lowering** results. Every kernel is
fixed-point (Q15/Q14, 64-bit accumulate) on purpose: the golden vectors are
then unambiguous and **bit-exact**, and it is how real HiFi/Vision kernels
actually run. (Float bit-exactness is the thing the portable-C legs already
probe, and where FMA contraction/rounding legitimately diverges — we don't
reopen it here.)

```bash
make compare-dsp     # RVV (rv64) + dc233c (xtensa) run the kernels vs golden
```

The scalar reference in `kernels/dsp_kernels_scalar.c` is the **oracle**; its
outputs are frozen in `kernels/golden/`. Each accelerated build's public `k_*`
kernels must reproduce those **bit-exactly** — that is what makes an eventual
HiFi-vs-RVV comparison fair. On the `riscv` leg every kernel below is a real
**RVV 1.0** intrinsic, verified bit-exact vs golden under QEMU.

<!-- BEGIN capability-table -->

### What runs today

_Generated by `tools/gen-capabilities.py` (`make capabilities`) from the golden roster - do not edit by hand._

The DSP arm has **7** fixed-point kernels, each checked **bit-exact** against the scalar golden oracle:

| kernel | workload | RVV 1.0 (rv64) | HiFi (firewall) |
|---|---|---|---|
| `biquad_q14` | biquad IIR, DF-I (Q14) | &#10003; bit-exact | seam |
| `cdotprod_q15` | complex dot product (Q15) | &#10003; bit-exact | seam |
| `dotprod_q15` | real dot product (Q15 -> i64) | &#10003; bit-exact | seam |
| `fft64_q15` | 64-pt radix-2 FFT (Q15) | &#10003; bit-exact | seam |
| `fir_q15` | FIR filter (Q15) | &#10003; bit-exact | seam |
| `matmul_s8` | int8 GEMM (i32 accumulate) | &#10003; bit-exact | seam |
| `requantize_s8` | NN requantize (i32 -> i8) | &#10003; bit-exact | seam |

Legs: `riscv` (rv64, xPack GCC) &middot; `m7` (Cortex-M7, arm-none-eabi) &middot; `xtensa` (dc233c scalar baseline, Zephyr SDK). RVV status is enforced live by `make compare-dsp`; HiFi kernels are filled behind the firewall.

<!-- END capability-table -->

The table above is generated from the golden roster by
`tools/gen-capabilities.py` (`make capabilities`); CI fails the build if it
drifts from the actual kernels.

### What's reachable with open tools (honesty box)

| Target | Open tools? | Notes |
|---|---|---|
| RISC-V **RVV 1.0** | **Yes** | The fair open-source counterpart to HiFi — real ratified `__riscv_*` intrinsics. |
| Tensilica **dc233c** controller core | **Yes** | Zephyr-SDK GCC + `qemu-system-xtensa -M sim`. Scalar core — the plumbing baseline + portability point, *not* a DSP data point. |
| Cadence **HiFi / Vision / Fusion** | **No** | The ISA extensions ship under NDA in the config overlay (needs Xtensa Xplorer + XCC + the proprietary ISS) — done behind the firewall. |

### One vessel, three swappable args

The `xtensa` leg is one vessel; the licensed HiFi build is the *same* vessel
with three build args changed — no structural change:

| arg | open default (dc233c) | inside the firewall (HiFi) |
|---|---|---|
| `XT_CC`  | `xtensa-dc233c_zephyr-elf-gcc` | `xt-clang` / `xt-xcc` |
| `XT_CORE`| `dc233c` | licensed core config |
| `XT_RUN` | `qemu-system-xtensa -M sim -cpu dc233c` | `xt-run` (ISS) |
| impl     | `IMPL=dsp_kernels_baseline` | `IMPL=dsp_kernels_hifi HAVE=HAVE_HIFI` |

`kernels/dsp_kernels_hifi.c` is left green-via-`_ref`-fallback with its
`TODO(ACC)` seams intact, ready for the firewall session to fill with real HiFi
intrinsics against the same golden bar.

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
- **Xtensa**: bump `ARG ZSDK_VERSION` in `xtensa/Dockerfile`, then
  `docker build -t xtensa-baremetal xtensa`.

Adjust that leg's CPU/ISA flags in its own `Makefile` (e.g. RISC-V `ARCH`/`ABI`,
M7 `MCPU`, Xtensa `XT_CORE`) as needed. Re-run the relevant comparison to
confirm results still hold. Adding another leg is the same shape: a new peer
directory with a Dockerfile + Makefile that compiles a shared source set —
`portable/` for the auto-lowering experiment, or `kernels/` + `harness/` for the
DSP arm — plus an arm in `compare/`. The `xtensa/` leg is exactly that for the
DSP arm (and shows how a leg can bring a different toolchain model — reset-vector
placement + semihosting — without disturbing the others).

## CI and run reports

`.github/workflows/ci.yml` runs on every push and PR (with Docker layer
caching): it builds all three toolchain images, runs the RISC-V self-test suite
+ negative-path check, runs `make compare` (must match), asserts the hazard
probes still diverge (so a broken detector also fails CI), runs the **DSP arm**
(`make compare-dsp` — the RVV and dc233c intrinsic kernels must be bit-exact vs
golden), and prints the codegen fingerprint as an informational, non-gating
step.

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

## Author

**Kyle Fox** - [GitHub](https://github.com/kylefoxaustin)

## License

MIT License - Use freely for personal and commercial projects.
