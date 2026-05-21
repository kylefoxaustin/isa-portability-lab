#!/usr/bin/env python3
"""
codegen.py - ISA-neutral codegen fingerprint comparison.

Reads two objdump -d disassemblies (RISC-V and Cortex-M7) and, for each probe
function, reports a structural fingerprint: instruction count, bytes, a
category histogram (mem/int/fp/simd/branch/call/other), whether fused
multiply-add and SIMD/vector instructions were emitted, and any libcalls
(soft-emulated ops). You can't diff incomparable instruction streams, so this
compares *shape*, not equality - a diagnostic, not a pass/fail gate.

Usage: codegen.py <rv.dis> <m7.dis> <func-prefix>   e.g. prefix "k_" or "h_"
"""
import re
import sys

HDR = re.compile(r'^[0-9a-fA-F]+ <([^>]+)>:')
INSN = re.compile(r'^\s*[0-9a-fA-F]+:\t')
SYM = re.compile(r'<([^>+]+)(?:\+0x[0-9a-fA-F]+)?>')

def parse(path):
    funcs, cur = {}, None
    for line in open(path):
        m = HDR.match(line)
        if m:
            cur = m.group(1); funcs[cur] = []; continue
        if cur is None or not INSN.match(line):
            continue
        parts = line.rstrip('\n').split('\t')
        if len(parts) < 3:
            continue
        enc = parts[1].strip().replace(' ', '')
        mnem = parts[2].strip()
        ops = parts[3].strip() if len(parts) > 3 else ''
        # Skip data directives: ARM Thumb embeds literal pools (and objdump
        # renders adjacent .rodata) as `.word`/`.short`/... lines inside the
        # function block. Those are data, not instructions.
        if mnem.startswith('.'):
            continue
        funcs[cur].append((mnem, ops, len(enc) // 2))
    return funcs

RV_MEM = set("lb lh lw ld lbu lhu lwu sb sh sw sd flw fld fsw fsd".split())
RV_BR  = set("beq bne blt bge bltu bgeu j jr ret tail beqz bnez blez bgez bltz bgtz bgt ble".split())
RV_CALL = set("jal jalr call".split())
RV_SYS = set("fence fence.i nop ecall ebreak wfi mret unimp csrr csrw csrs csrc "
             "csrrw csrrs csrrc csrrwi csrrsi csrrci csrwi".split())

def classify_rv(mnem):
    b = mnem[2:] if mnem.startswith("c.") else mnem
    if b in RV_MEM: return "mem"
    if b[:1] == "v": return "simd"
    if b in RV_CALL: return "call"
    if b in RV_BR: return "branch"
    if b in RV_SYS: return "other"
    if b[:1] == "f": return "fp"
    return "int"

ARM_MEM = set("push pop ldm stm ldmia stmia stmdb ldmdb vpush vpop".split())
ARM_BR  = set("b bx bxns cbz cbnz beq bne bcs bcc bmi bpl bvs bvc bhi bls bge blt bgt ble bal".split())
ARM_CALL = set("bl blx".split())
ARM_SYS = set("nop it ite itt itte dsb isb dmb bkpt svc mrs msr wfi wfe".split())

def classify_arm(mnem):
    base = mnem.split('.')[0].lower()
    if base.startswith("ldr") or base.startswith("str"): return "mem"
    if base in ("vldr", "vstr", "vldm", "vstm", "vpush", "vpop"): return "mem"
    if base in ARM_MEM: return "mem"
    if base[:1] == "v": return "fp"          # M7 = scalar VFP (no NEON)
    if base in ARM_CALL: return "call"
    if base in ARM_BR: return "branch"
    if base in ARM_SYS: return "other"
    return "int"

FMA = ("fmadd", "fmsub", "fnmadd", "fnmsub", "macc", "msac", "vfma", "vfms", "vfnm")
def is_fma(mnem):
    s = mnem.lower()
    return any(k in s for k in FMA)

CATS = ["mem", "int", "fp", "simd", "branch", "call", "other"]

def fingerprint(insns, classify):
    fp = {"insns": 0, "bytes": 0, "fma": False, "libcalls": set()}
    for c in CATS:
        fp[c] = 0
    for mnem, ops, nbytes in insns:
        fp["insns"] += 1
        fp["bytes"] += nbytes
        fp[classify(mnem)] += 1
        if is_fma(mnem):
            fp["fma"] = True
        for s in SYM.findall(ops):
            if s.startswith("__"):
                fp["libcalls"].add(s)
    return fp

def main():
    rv_dis, m7_dis, prefix = sys.argv[1], sys.argv[2], sys.argv[3]
    rv = parse(rv_dis)
    m7 = parse(m7_dis)

    names = sorted(n for n in set(rv) | set(m7) if n.startswith(prefix))

    print("=== codegen fingerprint: RISC-V (rv64) vs Cortex-M7 (ARM) ===")
    print("    Structural shape, not equality - a diagnostic. Both built at -O2,")
    print("    which auto-vectorizes (GCC 12+). Watch the 'simd' column: RISC-V")
    print("    lowers vectorizable loops to RVV, while Cortex-M7 stays scalar")
    print("    (no SIMD unit on M7) - the same C, very different machine code.")
    print()
    hdr = "%-16s %-3s %5s %5s %4s %4s %4s %4s %4s %4s %4s %4s" % (
        "function", "tgt", "insn", "byte", "mem", "int", "fp", "simd",
        "br", "call", "fma", "other")
    print(hdr)
    print("-" * len(hdr))

    libcall_notes = []
    for name in names:
        for tgt, table, classify in (("RV", rv, classify_rv), ("M7", m7, classify_arm)):
            if name not in table:
                print("%-16s %-3s   (absent on this target)" % (name, tgt))
                continue
            fp = fingerprint(table[name], classify)
            print("%-16s %-3s %5d %5d %4d %4d %4d %4d %4d %4d %4s %4d" % (
                name, tgt, fp["insns"], fp["bytes"], fp["mem"], fp["int"],
                fp["fp"], fp["simd"], fp["branch"], fp["call"],
                "yes" if fp["fma"] else "no", fp["other"]))
            if fp["libcalls"]:
                libcall_notes.append("  %-16s %s: %s" %
                                     (name, tgt, ", ".join(sorted(fp["libcalls"]))))
        print()

    print("libcalls (soft-emulated / runtime-helper ops):")
    print("\n".join(libcall_notes) if libcall_notes else "  (none - every op mapped to native instructions)")

if __name__ == "__main__":
    main()
