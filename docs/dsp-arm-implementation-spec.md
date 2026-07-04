# IMPLEMENTATION_SPEC.md — Cadence/Tensilica DSP arm for isa-portability-lab

Handoff for the local Claude Code session (home PC). Reviewer-implementer
discipline: this document is the design intent; you implement against it,
mirroring the repo's existing conventions rather than inventing new ones.

---

## 0. The reframe (read first — it changes what we're building)

The existing legs (rv64, cortex-m7) answer: *"compile the same portable C,
how does it lower across ISAs?"* That question is a **category error for a
DSP.** A DSP's value lives in intrinsics and hand-tuned kernels, not in
auto-vectorized portable C — point even a licensed HiFi compiler at generic
C and the HiFi datapath sits idle. So this arm answers a **different**
question and the repo must say so plainly:

> **DSP arm:** how do hand-written *intrinsic* kernels compare across
> datapaths — HiFi vs. RVV — on identical fixed-point workloads?

Both experiments live in one repo; they are simply not the same experiment.

Two more honest constraints baked into this design:

- **No real Cadence DSP (HiFi/Vision/Fusion) is reachable via open source.**
  Its ISA extensions ship under NDA in the config overlay; you need Cadence
  Xtensa Xplorer + XCC + the proprietary ISS. That work happens **inside the
  firewall** with ACC Claude. Out here we build everything that does *not*
  need the licensed ISA and leave shaped seams.
- **Fixed-point, not float, on purpose.** Every kernel is integer/fixed-point
  so the golden vectors are unambiguous and bit-exact across targets. Float
  bit-exactness is the thing the portable-C legs already probe and where FMA
  contraction/rounding legitimately diverges — we deliberately don't reopen
  that here.

The open `dc233c` leg is **not** a DSP data point (it's a scalar controller
core). Its jobs: (a) prove the entire Xtensa vessel — Docker, Makefile,
semihosting I/O, harness — on a free core, and (b) be the portability
baseline. The HiFi leg is the *same vessel* with three args changed:
compiler prefix, core overlay, runner binary.

---

## 1. What's in this package (drop into the repo)

```
kernels/
  dsp_kernels.h            # API contract: k_*_ref (ground truth) + k_* (under test)
  dsp_kernels_scalar.c     # ground-truth scalar reference — THE ORACLE, never target-specific
  dsp_kernels_baseline.c   # public k_* -> forwards to _ref (scalar/dc233c/m7 legs link this)
  dsp_kernels_hifi.c       # HiFi intrinsic SKELETON — ACC Claude fills this inside the firewall
  dsp_kernels_rvv.c        # RVV intrinsic skeleton + ONE worked exemplar (dot product)
  test_vectors.h           # shared deterministic LCG input generator
  golden/*.txt             # known-good outputs, human-diffable (generated & verified out here)
harness/
  gen_golden.c             # regenerates golden/*.txt from the reference
  golden_to_header.py      # golden/*.txt -> golden_data.h (compiled-in, no target FS needed)
  golden_data.h            # generated
  run_kernels.c            # on-target self-check: runs k_*, diffs golden, PASS/FAIL, nonzero on fail
```

Everything here **builds green today** (verified with host gcc: baseline
passes all 7 kernels; hifi/rvv fall back to `_ref` and also pass). The
determinism of the oracle is verified bit-identical across `-O0`/`-O2`.

Kernel set (all fixed-point, all with real HiFi/Vision datapath support):
FIR (Q15), biquad IIR (Q14), real dot product (Q15→i64), complex dot
product, 64-pt radix-2 FFT (Q15, >>1/stage), NN requantize (i32→i8),
int8 GEMM (i32 accumulate).

---

## 2. Your tasks, out here (all open-source-reachable)

### 2.1 Add the `xtensa/` leg — parameterized

Mirror the existing `rv64` / `cortex-m7` leg layout exactly (same Makefile
variable names, same `portable/` wiring style, same `compare/` arm shape —
**read those legs first and match them**). The leg must be parameterized on
three build args so HiFi drops in later with zero structural change:

| arg        | open default (dc233c)          | inside firewall (HiFi)              |
|------------|--------------------------------|-------------------------------------|
| `XT_CC`    | `xtensa-dc233c-elf-gcc`        | `xt-clang` / `xt-xcc`               |
| `XT_CORE`  | `dc233c`                        | licensed core config name           |
| `XT_RUN`   | `qemu-system-xtensa -M sim -cpu dc233c` | `xt-run` (ISS)             |
| impl file  | `dsp_kernels_baseline.c`        | `dsp_kernels_hifi.c` + `-DHAVE_HIFI`|

Runner semantics: QEMU's `-M sim` speaks SIMCALL semihosting; `run_kernels.c`
only needs stdout + exit code, which both `-M sim` and `xt-run` provide. Wire
the leg so `make run` builds `run_kernels.c` + `dsp_kernels_scalar.c` +
`$(IMPL)` for `$(XT_CORE)`, runs under `$(XT_RUN)`, and propagates the exit
code (0 = all kernels bit-exact vs golden).

**Toolchain acquisition for dc233c — the one empirical unknown.** Pick in
this order and pin whichever works in the Dockerfile:
1. **Zephyr SDK** prebuilt `xtensa-dc233c` toolchain — single tarball, no
   multi-hour build; fastest to green. *Verify it drives a raw bare-metal ELF
   with SIMCALL cleanly outside Zephyr's build system — that's the thing to
   confirm empirically.*
2. **crosstool-NG** from source + a dc233c overlay — canonical, matches the
   repo's "toolchain in a box" ethos, but slow build; needs the dc233c
   overlay tar (exists in the buildroot/QEMU lineage).
3. Do **not** use Espressif's `xtensa-esp-elf` for the dc233c leg — it's an
   esp32 (LX6) config, wrong core for `-cpu dc233c`. (It's only for a future
   esp32s3 leg; see §4.)

Acceptance: `make -C xtensa run` prints `== RESULT: PASS ==`, exit 0, under
QEMU. Add it to the `compare/` dispatcher as a third arm and to CI.

### 2.2 Fill in the RVV intrinsic kernels

`dsp_kernels_rvv.c` has the **dot product worked as the exemplar** (RVV 1.0,
`__riscv_*` ratified intrinsics, VLEN-agnostic strip-mining). **Verify it on
your actual rv64 leg's toolchain and `-march`** (I couldn't cross-compile it
here — treat the exemplar as a pattern to confirm, not gospel), then
implement the remaining six following the same pattern. Build the RVV leg
with `-DHAVE_RVV`. Acceptance: every RVV kernel is **bit-exact vs golden** —
same Q formats, same 64-bit accumulate, same round-half-up + saturation as
`_ref`. This is what makes the eventual HiFi-vs-RVV comparison fair.

### 2.3 README delta

Add `README_LEG.md`'s content into the repo README: the DSP-arm reframe
(§0), the "what's reachable open-source" honesty box, and the leg's
parameterization table. Label the DSP-arm results section as *intrinsic
kernels*, distinct from the portable-C legs' *auto-lowering* results.

---

## 3. ACC Claude's tasks, inside the firewall (do NOT attempt out here)

Hand the firewall session this same repo. Its scope is exactly:

1. Build the `xtensa` leg with `XT_CC`/`XT_CORE`/`XT_RUN` set to the licensed
   HiFi toolchain + `-DHAVE_HIFI`.
2. Fill each `TODO(ACC)` in `dsp_kernels_hifi.c` with real HiFi intrinsics
   (include the correct `xt_hifi<N>.h` for the licensed core). Each kernel is
   annotated with the HiFi datapath to target (MAC families, complex MAC,
   FFT butterfly, vnclip-equivalent, etc.).
3. **Bit-exact bar:** match `golden/*.txt` exactly. If HiFi's native
   rounding/saturation mode differs from the reference, reconcile it
   explicitly — either match HiFi's mode in `dsp_kernels_scalar.c` and
   regenerate golden via `gen_golden.c` + `golden_to_header.py`, or conform
   the HiFi kernel to the reference. **Never let the two drift silently.**
4. Fill `cyc_begin()/cyc_end()` with CCOUNT and record per-kernel cycle
   counts from the ISS into the metrics output.

Because the harness, golden, kernel set, RVV side, and equivalence gate are
already green out here, the firewall session is a *fill-in-and-run* exercise,
not a build-from-scratch — which is the whole point of doing this now.

---

## 4. Explicitly out of scope

- Real HiFi/Vision/Fusion comparison in the *public* repo — licensed-tools
  appendix only, lives behind the firewall.
- **Optional future:** an `esp32s3` (Xtensa LX7) leg is the closest
  DSP-flavored target reachable open-source (Espressif's open GCC + QEMU
  forks, limited SIMD). If added, it needs its *own* intrinsic kernels and
  must be labeled as intrinsic-kernel results, same as the HiFi arm — not
  auto-vectorized C. Not required for this pass.

---

## 5. Definition of done (this pass, out here)

- [ ] `xtensa/` dc233c leg builds and runs under QEMU; `== RESULT: PASS ==`, exit 0.
- [ ] dc233c leg added to `compare/` dispatcher + CI.
- [ ] RVV exemplar verified on the rv64 toolchain; remaining 6 RVV kernels
      implemented and bit-exact vs golden with `-DHAVE_RVV`.
- [ ] README carries the DSP-arm reframe + open-source-reachability honesty box.
- [ ] `dsp_kernels_hifi.c` left green-via-fallback with all `TODO(ACC)` seams
      intact and annotated, ready for the firewall session.

TTA.
