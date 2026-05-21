# Bare-Metal RV64 / RVA23U64-Subset Starter (RISC-V leg)

> This is the **RISC-V leg** of [`isa-portability-lab`](../README.md). For the
> cross-target (RISC-V ↔ Cortex-M7) comparison harness, the shared `portable/`
> probes, and the `compare/` dispatcher, see the **repo-root README**.

A working bare-metal RISC-V toolchain skeleton targeting the RVA23U64 userspace
ISA subset, running on QEMU's `virt` machine. Includes a stubbed accelerator
intrinsic header as the seam where your custom accelerator instructions will
later plug in, plus a 35-test self-checking regression suite that exercises the
integer/FP/vector/bit-manip paths and reports a real pass/fail exit code.

This is **deliverable #1 of the toolchain track** — a compileable, runnable
foundation. Compiler-backend work (TableGen, intrinsics, patterns) comes later
once your accelerator ISA is shareable.

## What you get

This leg lives in `riscv/`; the shared probe harness (`portable/`), the M7 leg
(`m7/`), and the dispatcher (`compare/`) live at the repo root.

```
riscv/
├── Dockerfile                       reproducible toolchain env (xPack GCC + QEMU)
├── Makefile                         build, run, test, test-negative, portable, hazards
├── link.ld                          memory map (256KB SRAM @ 0x80000000)
├── crt0.S                           startup: hart park, stack, BSS, mscratch, mtvec, FS/VS enable
├── trap.S                           trap handler: capture cause/epc/tval, skip faulting insn, mret
├── main.c                           bring-up sanity + 35-test self-test suite
├── portable_riscv.c                 RISC-V glue for the shared probe harness (UART + exit)
└── include/
    └── accelerator_intrinsics.h     stubbed intrinsic API - YOUR SEAM
```

## Architecture choice

`-march=rv64imafdcv_zicsr_zifencei_zba_zbb_zbs_zfh -mabi=lp64d`

This is the **bare-metal-relevant subset of RVA23U64**. RVA23 mandates a lot
more (hypervisor, supervisor, MMU, Zicond, Zcb, etc.) but those are either
meaningless on 256KB SRAM or unevenly supported in current toolchains. What's
here gives you:

- RV64 base + M/A/F/D/C (the GC profile)
- V@VLEN=128 vector
- Bit-manip (Zba/Zbb/Zbs)
- Half-precision FP (Zfh)
- Binary-compat with the RVA23U64 userspace ABI for future migration

Edit `ARCH`/`ABI` in `Makefile` to narrow or widen as your silicon dictates.

## Memory map

| Region | Base       | Size  |
|--------|------------|-------|
| SRAM   | 0x80000000 | 256KB |

`crt0.S` parks all harts except hart 0, points `sp` at the top of SRAM,
zeros BSS, enables `mstatus.FS` and `mstatus.VS` (otherwise FP and V
instructions trap), points `mscratch` at the trap handler's register save
area, installs the trap vector, and calls `main`. Stack is 8KB at the top of
SRAM; the rest of SRAM after `.bss` and stack is reserved as heap (no
allocator wired up).

Edit `MEMORY{}` in `link.ld` when you target real silicon. Add additional
regions (QSPI/eMMC code region, multiple SRAM banks, etc.) as needed.

## Building

### With Docker (recommended)

The image is built from `riscv/`, but the repo root is mounted at `/work` (so
the build can reach the shared `../portable/`), with the working dir set to
`/work/riscv`. Run these from the repo root:

```bash
docker build -t riscv-baremetal riscv
docker run --rm -it -v "$PWD:/work" -w /work/riscv riscv-baremetal make
docker run --rm -it -v "$PWD:/work" -w /work/riscv riscv-baremetal make run
```

### Directly with `riscv-none-elf-gcc` from xPack

```bash
# Install once:
#   https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases
export PATH=/path/to/xpack-riscv-none-elf-gcc-14.2.0-3/bin:$PATH
cd riscv
make            # build
make run        # run in QEMU (Ctrl-A X to exit)
make clean
```

### With the Linux-targeted GCC (Ubuntu's `gcc-riscv64-linux-gnu`)

Works fine because we link `-nostdlib -static -no-pie`:

```bash
make CROSS=riscv64-linux-gnu-
make run CROSS=riscv64-linux-gnu-
```

## Expected output

`make run` (interactive) and `make test` (scripted) print the same bring-up
banner followed by the self-test suite:

```
=== bare-metal RV64 + RVA23U64 subset bring-up ===
vlenb (bytes per V-reg): 16  (VLEN=128 bits)
accel_sqrtf(2.0) bits = 0x000000003fb504f3   (expect 0x3fb504f3)
accel_sinf(1.0) bits  = 0x000000003f576aa5   (software stub - replace with hw accel.sin)
vsetvli (e32,m1) max VL = 4 elements
intrinsics header: 0.1-stub

=== self-test suite ===
T1 u32 vec_add        (vle32/vadd.vv/vse32,   N=17) ... PASS
T2 f32 vec_add        (vle32/vfadd.vv/vse32,  N=17) ... PASS
T3 f32 dot (reduction)(vfmul/vfredusum/vfmv,  N=8) ... PASS
T4 u8  vec_memcpy     (vle8/vse8,             N=50) ... PASS
T5 Zbb cpop           (4 cases)                     ... PASS
T6 Zfh scalar f16 add (fadd.h, 3 cases)             ... PASS
T7 widening i16->i32  (vwadd.vv, m2 store, N=10)    ... PASS
T8 masked add (v0.t)  (vid/vmseq/vadd mu, N=4)      ... PASS
T9 strided gather     (vlse32.v stride=8, N=8)      ... PASS
T10 saxpy (FMA)       (vfmacc.vf alpha=2, N=17)     ... PASS
T11 accel seam        (sqrt/rsqrt/sin/cos/sincos)    ... PASS
T12 accel atan2 stub  (pinned: returns 0 by design) ... PASS
T13 trap capture+resume(ecall->11, illegal->2)       ... PASS
T14 segment load      (vlseg2e32 deinterleave, N=6)  ... PASS
T15 indexed gather    (vluxei32 base[idx], N=8)      ... PASS
T16 vrgather reverse  (vid/vrsub/vrgather m2, N=8)   ... PASS
T17 Zbb+Zbs bit-manip (clz/ctz/rev8/max/bset/bext)  ... PASS
T18 vcompress         (pack even lanes, N=8->4)      ... PASS
T19 mask logic        (vmsgt/vmand/vcpop/vfirst)     ... PASS
T20 max/min reduction (vredmax.vs/vredmin.vs, N=17)  ... PASS
T21 saturating add    (vsaddu/vsadd e8, N=8)         ... PASS
T22 segment store     (vsseg2e32 interleave, N=6)    ... PASS
T23 float max/min red (vfredmax.vs/vfredmin.vs, N=9) ... PASS
T24 frac-LMUL zext     (vzext.vf4 e8->e32, N=10)     ... PASS
T25 narrowing 32->16  (vnsrl.wi truncate, N=10)     ... PASS
T26 Zba shift-add     (sh1/2/3add, add.uw)          ... PASS
T27 FP<->int convert  (vfcvt.rtz.x.f / f.x, N=7)    ... PASS
T28 multiprec add64   (vmadc/vadc carry chain, N=8) ... PASS
T29 slides            (vslidedown.vi/vslide1up, N=8) ... PASS
T30 int div+rem       (vdivu.vv/vremu.vv, N=8)       ... PASS
T31 vmerge select     (vmerge.vvm even?t:f, N=8)     ... PASS
T32 ei16 gather       (vrgatherei16.vv, N=8)         ... PASS
T33 avg add (vxrm rnu)(vaaddu.vv, N=8)               ... PASS
T34 vector sqrt       (vfsqrt.v perfect sq, N=8)     ... PASS
T35 splat + vmv copy  (vmv.v.x/vmv.v.v, N=10)        ... PASS

=== summary: ALL PASS ===
```

For `make run`, QEMU then hangs in `wfi` (intentional) — exit with `Ctrl-A X`.
For `make test`, the guest writes QEMU's `sifive_test` device to exit with a
real status code (0 = all green).

## Testing

The same binary doubles as a self-checking regression suite. Each `Tn` runs a
small kernel and compares against a scalar golden; the result drives a real
process exit code via QEMU's `sifive_test` device at `0x100000`.

```bash
make test            # build + run; exits 0 iff all tests pass
make test-negative   # rebuild with an injected failure; asserts a non-zero
                     # exit propagates (proves the fail path actually works)
```

`make test` guards against hangs with a 10s wall-clock timeout. The suite
covers integer/FP elementwise ops, reductions (sum/max/min, integer + float),
masking, widening/narrowing, fractional LMUL, strided/indexed/segment memory,
FMA, gather/compress/permute (incl. 16-bit-indexed), slides, per-lane merge,
mask-domain logic, integer divide/remainder, saturating + averaging
fixed-point (with vxrm rounding control), FP<->int conversions, vector sqrt,
multi-precision carry-chain arithmetic, scalar splat / whole-vector moves, the
scalar Zba/Zbb/Zbs/Zfh extensions, the accelerator seam, and trap
capture-and-resume. When the accelerator ISA
lands and you replace a stub in `accelerator_intrinsics.h` with inline asm,
T11/T12 immediately tell you whether the hardware matches the software
contract everything else was built against.

Run it under QEMU's GDB stub if a test fails and you need to inspect state:
`trap.S` records `mcause`/`mepc`/`mtval` + a trap count in `_trap_info`.

## Cross-target portability comparison

This RISC-V leg is one half of a portability experiment: the same portable C
is compiled for both RISC-V and Cortex-M7 and the results compared. The shared
`portable/` probes, the `compare/` dispatcher, the verdict format, and the
codegen fingerprint live at the repo root — see the
[repo-root README](../README.md) and run `make compare` from there.

## The accelerator seam — `include/accelerator_intrinsics.h`

This is the only file you need to edit when the accelerator ISA is finalized.
Every function is currently a software fallback. To wire in real hardware
instructions, replace the body with inline assembly:

```c
/* Before: software stub */
static inline float accel_sinf(float x) {
    return __accel_soft_sinf(x);
}

/* After: maps to your accelerator instruction */
static inline float accel_sinf(float x) {
    float r;
    asm volatile("accel.sin %0, %1" : "=f"(r) : "f"(x));
    return r;
}
```

Keep the signatures stable — everything above (kernel C, generated code from
the visual tool, user higher-level code) calls through this header.

When you've got enough intrinsics that inline-asm performance limits matter,
the next step is real LLVM/GCC backend support (TableGen entries, `__builtin_*`
intrinsics, SelectionDAG patterns). That unlocks the optimizer for cross-call
register allocation and scheduling. Don't bother until inline asm is the
bottleneck.

## Footprint right now

With the full 35-test suite compiled in (`-Os`):

| Section | Size   |
|---------|--------|
| .text   | ~9 KB   |
| .data   | 0       |
| .bss    | 72 B    |
| **Total of SRAM used** | **~3.5%** |

Plenty of headroom for real kernels. The test suite is just `main.c`; strip it
back to the bring-up prints if you want a minimal firmware image.

## What's intentionally NOT here

- **No libc.** Add picolibc/newlib-nano if you need printf, malloc, etc.
  For control kernels you usually don't.
- **No peripheral drivers.** Only a single MMIO write to the QEMU 16550 UART
  for bring-up output. Per your "leave peripherals alone for now".
- **No interrupt dispatch.** `trap.S` captures `mcause`/`mepc`/`mtval`, skips
  the faulting instruction, and `mret`s back — enough to survive a synchronous
  fault and let a test read back what happened (it is *non-nested* and handles
  synchronous traps only). Add a vectored or table-driven dispatcher, plus full
  context save/restore, when you start servicing interrupts and peripherals.
- **No OpenSBI / no S-mode.** Pure M-mode bare-metal. The accelerator-aware
  kernels you described don't need an SBI layer.
- **No multi-hart.** Hart 0 only; others park. Wire up wakeup once you have
  a use case.

## Next steps in this toolchain track

1. Replace `riscv64-linux-gnu-` with bare-metal `riscv-none-elf-` for production.
   Linux toolchain is fine for sandbox testing but ships glibc/dynamic baggage.
2. Add picolibc *only* if you need libc symbols.
3. Validate on a cycle-accurate ISS or your FPGA bringup once available.
4. When the accelerator ISA opens up: fill in `accelerator_intrinsics.h` with
   inline asm. Toolchain otherwise unchanged.
5. When inline-asm perf isn't enough: real GCC/LLVM backend extensions.

## Known caveats

- The linker emits an "RWX segment" warning because SRAM is one unified region.
  Cosmetic — split into ROM+RAM regions when you have separate flash/SRAM.
- The software `sinf` stub is a 5-term Taylor series — accurate to ~6 decimals
  for small inputs, garbage near `±pi`. It exists only so the seam compiles
  and runs in QEMU before the accelerator is real.
- `accel_atan2f` returns 0; do not use until backed by hardware.
