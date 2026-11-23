#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 R. Luke DuBois
"""docs/synthaccess/Solinaesque.json (the MIDItoSpeech sidecar) against the firmware: the file is the one
tools/gen_synthaccess.c makes now; every control the FM-1 sends has an entry; for every value it sends at every panel
step, MIDItoSpeech's reading (its common/parser.js modes, done here the same way) says the named choices word for word,
the percentages and linear ranges within one step of the firmware's value, and the raw value for the exponential ones.
  tests/synthaccess_check.py build/host/gen_synthaccess docs/synthaccess/Solinaesque.json"""
import json
import math
import subprocess
import sys

gen, path = sys.argv[1], sys.argv[2]
fresh = subprocess.run([gen], capture_output=True, text=True, check=True).stdout
committed = open(path).read()
failed = 0


def check(what, ok):
    global failed
    print(f"{what:<62} {'ok' if ok else 'FAIL'}")
    failed += not ok


def js_round(x):                                   # Math.round
    return math.floor(x + 0.5)


def read(entry, v):                                # parser.js parseSpeak, for the modes the sidecar uses
    d = entry["data"]
    if d == "percent":
        return js_round(v / 127 * 100)
    if d == "intmap":
        a, b, c, e = entry["map"]
        t = (v - a) / (b - a)
        if entry.get("clamp") == "true":
            t = min(max(t, 0.0), 1.0)
        return js_round(c + t * (e - c))
    if d == "enumsplit":
        idx = 0
        for i, s in enumerate(entry["split"]):
            if v >= s:
                idx = i
        return entry["enum"][idx]
    if d == "offon":
        return "off" if v == 0 else "on"
    return v                                       # value


check("the sidecar is current (as gen_synthaccess makes it now)", fresh == committed)
dev = json.loads(committed)["device"]
bad, n, counts = [], 0, {}
for line in subprocess.run([gen, "--pairs"], capture_output=True, text=True, check=True).stdout.splitlines():
    kind, key, sent, mode, want = line.split(" ", 4)
    entry = dev["CC" if kind == "CC" else "NRPN"].get(key)
    n += 1
    if entry is None:
        bad.append(f"{kind} {key}: no entry")
        continue
    got = read(entry, int(sent))
    counts[mode] = counts.get(mode, 0) + 1
    if mode in ("enum", "offon"):
        ok = got == want
    elif mode == "value":
        ok = got == int(sent)
    else:                                          # within one step of the firmware's value (in the entry's unit)
        lo, hi = (entry["map"][2], entry["map"][3]) if entry["data"] == "intmap" else (0, 100)
        span = entry["map"][1] if entry["data"] == "intmap" else 127
        ok = abs(got - int(want)) <= max(1, math.ceil(abs(hi - lo) / span))
    if not ok:
        bad.append(f"{kind} {key} sent {sent}: reads {got}, the FM-1 means {want}")
for b in bad[:6]:
    print("  " + b)
print(f"  ({n} values: " + ", ".join(f"{m} {c}" for m, c in sorted(counts.items())) + ")")
check("every value sent reads right (names exactly, numbers within a step)", not bad and n > 1500)
check("transport left out (no CC 109): MIDItoSpeech does not need it", "109" not in dev["CC"])
check("Program Change: the 48 presets by name", len(dev["presets"]) == 48 and dev["presets"][2] == "factory 3, violins")
check("the 14-bit ones as NRPN, hires (18)", len(dev["NRPN"]) == 18 and all(e.get("hires") == "true" for e in dev["NRPN"].values()))
check("the exponential ones read as the raw value", all(dev["NRPN"][k]["data"] == "value" for k in ("74", "73", "75", "72", "25")))
print("synthaccess sidecar FAILED" if failed else "synthaccess sidecar passed")
sys.exit(1 if failed else 0)
