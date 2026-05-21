#!/usr/bin/env python3
"""
report.py - render the cross-target comparison three ways from the two legs'
(normalized) probe output: a terminal table (stdout), a machine-readable
report.json (--json), and GitHub-flavored Markdown for a CI job summary (--md).

Exit code: 0 if every computation checksum matches across targets, else 1 -
so it doubles as the gate that compare.sh / CI key on.

Usage: report.py --rv <rv.txt> --m7 <m7.txt> --mode clean|hazards
                  [--json out.json] [--md out.md]
"""
import argparse
import json
import re
import sys

def parse(path):
    info, probes, order, combined = {}, {}, [], None
    for raw in open(path):
        line = raw.rstrip("\n").rstrip("\r").strip()
        if not line:
            continue
        if line.startswith("info "):
            m = re.match(r"info\s+(\S+)\s*=\s*(.+)", line)
            if m:
                info[m.group(1)] = m.group(2).strip()
        elif line.startswith("=== combined"):
            m = re.search(r"(0x[0-9a-fA-F]+)", line)
            if m:
                combined = m.group(1)
        elif re.match(r"^[KH][0-9]", line):
            label, _, val = line.partition("= ")
            label = label.rstrip()
            val = val.strip()
            probes[label] = val
            order.append(label)
    return info, probes, order, combined

def union(a, b):
    seen, out = set(), []
    for x in list(a) + list(b):
        if x not in seen:
            seen.add(x); out.append(x)
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rv", required=True)
    ap.add_argument("--m7", required=True)
    ap.add_argument("--mode", default="clean")
    ap.add_argument("--json")
    ap.add_argument("--md")
    a = ap.parse_args()

    rv_info, rv_p, rv_order, rv_comb = parse(a.rv)
    m7_info, m7_p, m7_order, m7_comb = parse(a.m7)

    comp = []
    fails = 0
    for label in union(rv_order, m7_order):
        rvv, m7v = rv_p.get(label, "-"), m7_p.get(label, "-")
        match = rvv == m7v and rvv != "-"
        if not match:
            fails += 1
        comp.append({"probe": label, "rv": rvv, "m7": m7v, "match": match})
    comb_match = rv_comb == m7_comb and rv_comb is not None

    abi = []
    for k in union(rv_info, m7_info):
        rvv, m7v = rv_info.get(k, "-"), m7_info.get(k, "-")
        abi.append({"fact": k, "rv": rvv, "m7": m7v, "match": rvv == m7v})

    verdict_ok = fails == 0

    # ---------- terminal table ----------
    print()
    print("================ ABI / type model (informational) ================")
    if abi:
        print("      %-34s | %-34s" % ("RISC-V (rv64)", "Cortex-M7 (ARM)"))
        for r in abi:
            mark = "=" if r["match"] else "(!)"
            print("  %-3s %-32s | %-32s" % (
                mark, "info %s = %s" % (r["fact"], r["rv"]),
                "info %s = %s" % (r["fact"], r["m7"])))
    else:
        print("  (this probe set emits no ABI/type lines)")

    print()
    print("================ computation (must match) ========================")
    print("      %-34s | %-34s" % ("RISC-V (rv64)", "Cortex-M7 (ARM)"))
    for r in comp:
        mark = "ok" if r["match"] else "XX"
        print("  %-3s %-32s | %-32s" % (
            mark, "%s = %s" % (r["probe"], r["rv"]),
            "%s = %s" % (r["probe"], r["m7"])))

    print()
    print("==================================================================")
    if verdict_ok:
        print("VERDICT: PASS - all kernel results identical across targets.")
        print("         (ABI/type differences above are expected, not failures.)")
    else:
        print("VERDICT: FAIL - %d computation row(s) diverged." % fails)

    # ---------- report.json ----------
    if a.json:
        with open(a.json, "w") as f:
            json.dump({
                "mode": a.mode,
                "verdict": "pass" if verdict_ok else "fail",
                "fail_count": fails,
                "combined": {"rv": rv_comb, "m7": m7_comb, "match": comb_match},
                "computation": comp,
                "abi": abi,
            }, f, indent=2)
            f.write("\n")

    # ---------- GitHub job-summary Markdown ----------
    if a.md:
        badge = "PASS ✅" if verdict_ok else "FAIL ❌"
        L = []
        L.append("## Portability comparison (`%s`) — %s\n" % (a.mode, badge))
        L.append("### Computation (must match)\n")
        L.append("| probe | RISC-V (rv64) | Cortex-M7 | |")
        L.append("|---|---|---|:--:|")
        for r in comp:
            L.append("| `%s` | `%s` | `%s` | %s |" % (
                r["probe"], r["rv"], r["m7"], "✅" if r["match"] else "❌"))
        if rv_comb or m7_comb:
            L.append("| **combined** | `%s` | `%s` | %s |" % (
                rv_comb, m7_comb, "✅" if comb_match else "❌"))
        L.append("")
        if abi:
            L.append("### ABI / type model (informational)\n")
            L.append("| fact | RISC-V (rv64) | Cortex-M7 | |")
            L.append("|---|---|---|:--:|")
            for r in abi:
                L.append("| `%s` | `%s` | `%s` | %s |" % (
                    r["fact"], r["rv"], r["m7"], "✅" if r["match"] else "⚠️ differs"))
            L.append("")
        with open(a.md, "a") as f:
            f.write("\n".join(L) + "\n")

    sys.exit(0 if verdict_ok else 1)

if __name__ == "__main__":
    main()
