# The Cadence/Tensilica DSP arm

## What this arm is (and isn't)

The `rv64` and `cortex-m7` legs answer *"how does the same portable C lower
across ISAs?"* This arm answers a **different question**, because that one is
a category error for a DSP: a DSP's value is in intrinsics and hand-tuned
kernels, not in auto-vectorized portable C.

> **DSP arm question:** how do hand-written *intrinsic* kernels compare across
> datapaths (HiFi vs. RVV) on identical fixed-point workloads?

Results from this arm are **intrinsic-kernel** results and are reported
separately from the portable-C legs' **auto-lowering** results.

## What's reachable open-source (honesty box)

| Target | Reachable with open tools? | Notes |
|---|---|---|
| Tensilica **dc233c** controller core | **Yes** | QEMU `-M sim -cpu dc233c` + open GCC. Scalar core — plumbing baseline, *not* a DSP data point. |
| RISC-V **RVV 1.0** | **Yes** | The fair open-source counterpart to HiFi. |
| ESP32-S3 **LX7** (limited SIMD) | Yes-ish | Espressif open GCC + QEMU forks. Closest DSP-flavored open target; optional future leg. |
| Cadence **HiFi / Vision / Fusion** | **No** | ISA extensions are NDA'd in the config overlay. Needs Xtensa Xplorer + XCC + proprietary ISS — done behind the firewall. |

## Kernel set

All fixed-point (unambiguous, bit-exact golden vectors), all with real DSP
datapath support: FIR (Q15), biquad IIR (Q14), real dot product, complex dot
product, 64-pt radix-2 FFT (Q15), NN requantize (i32→i8), int8 GEMM.

Ground truth lives in `kernels/dsp_kernels_scalar.c` (the oracle). Golden
outputs are in `kernels/golden/*.txt`. The on-target harness
(`harness/run_kernels.c`) self-checks and returns nonzero on any divergence.

## Leg parameterization

The `xtensa` leg is one vessel; HiFi is the same vessel with three args
changed:

| arg | open default | inside firewall |
|---|---|---|
| `XT_CC` | `xtensa-dc233c-elf-gcc` | `xt-clang` / `xt-xcc` |
| `XT_CORE` | `dc233c` | licensed core config |
| `XT_RUN` | `qemu-system-xtensa -M sim -cpu dc233c` | `xt-run` |
| impl file | `dsp_kernels_baseline.c` | `dsp_kernels_hifi.c` + `-DHAVE_HIFI` |

Equivalence bar for every accelerated kernel: **bit-exact against
`kernels/golden/*.txt`**, not "close enough."
