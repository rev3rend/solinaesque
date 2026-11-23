#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 R. Luke DuBois
"""Back up and restore Solinaesque's presets over USB-MIDI SysEx (firmware/app/dump.c).

  solinaesque_presets.py backup presets.json [--port NAME]     the 32 user slots -> a JSON file (reads only)
  solinaesque_presets.py restore presets.json [--port NAME] [--yes]   the file -> the user slots (writes flash)
  solinaesque_presets.py get-current [sound.json] [--port NAME]        the sound playing now
  solinaesque_presets.py put-current sound.json [--port NAME]          a sound -> playing now (SAVE stores it)
  solinaesque_presets.py --selftest                                    the encoding, no FM-1 needed

Frames: F0 7D 53 4C <cmd> ... F7. 01 get the current sound (-> 02 <sound>); 02 <sound> make it current; 03 <slot>
get a user slot (-> 04 <slot> <sound>); 04 <slot> <sound> store it. A sound: 01, the name (12 bytes), 28, each
parameter as two 7-bit bytes (v + 128), a checksum (the sum of the body's bytes, & 7F). Needs mido with
python-rtmidi. The FM-1's port name can differ (macOS may show a cached name such as "BlessedUnion BlessedMIDI");
without --port the first port that answers is used.
"""
import argparse
import json
import sys
import time

HEADER = [0x7D, 0x53, 0x4C]
GET, SOUND, GETSLOT, SLOT = 1, 2, 3, 4
NAME_LEN, NPARAM, NUSER = 12, 28, 32
PARAMS = ["16'", "8'", "4'", "SHAPE", "CUTOFF", "RESONANCE", "ENV AMOUNT", "KEY TRACK", "ATTACK", "DECAY",
          "SUSTAIN", "RELEASE", "LFO RATE", "VIBRATO", "PWM", "LFO DELAY", "ENS DEPTH", "ENS RATE", "FLANGER",
          "FLG RATE", "DLY TIME", "FEEDBACK", "DLY MIX", "DLY TONE", "ENSEMBLE"]   # data.c SP order; the rest spare


def encode(sound):
    """{"name": str, "params": [int x 28]} -> the body bytes"""
    name = sound["name"].encode("ascii", "replace")[:NAME_LEN - 1]
    b = [1] + [c & 0x7F for c in name] + [0] * (NAME_LEN - len(name)) + [NPARAM]
    params = list(sound["params"]) + [0] * (NPARAM - len(sound["params"]))
    for v in params[:NPARAM]:
        u = int(v) + 128
        if not 0 <= u <= 255:
            raise ValueError(f"parameter {v} out of range")
        b += [u >> 7, u & 0x7F]
    return b + [sum(b) & 0x7F]


def decode(b):
    """the body bytes -> {"name", "params"}; ValueError if it is not well formed"""
    b = list(b)
    if len(b) != 1 + NAME_LEN + 1 + 2 * NPARAM + 1 or b[0] != 1 or b[1 + NAME_LEN] != NPARAM:
        raise ValueError("not a Solinaesque sound")
    if sum(b[:-1]) & 0x7F != b[-1]:
        raise ValueError("bad checksum")
    name = bytes(c for c in b[1:1 + NAME_LEN] if c).decode("ascii", "replace")
    params = [((b[2 + NAME_LEN + 2 * i] << 7) | b[3 + NAME_LEN + 2 * i]) - 128 for i in range(NPARAM)]
    return {"name": name, "params": params}


def readable(sound):
    d = dict(sound)
    d["named"] = {PARAMS[i]: v for i, v in enumerate(sound["params"][:len(PARAMS)])}
    return d


class FM1:
    def __init__(self, port=None):
        import mido
        self.mido = mido
        names = [port] if port else [p for p in mido.get_output_names() if p in mido.get_input_names()]
        for name in names:
            out, inp = mido.open_output(name), mido.open_input(name)
            self.out, self.inp = out, inp
            if self.ask(GET, [], SOUND, timeout=1.0, quiet=True) is not None:
                self.name = name
                return
            out.close()
            inp.close()
        raise SystemExit("no Solinaesque answered (USB data cable? another app using the port? --port NAME)")

    def ask(self, cmd, args, want, timeout=2.0, quiet=False):
        for _ in self.inp.iter_pending():
            pass
        self.out.send(self.mido.Message("sysex", data=HEADER + [cmd] + list(args)))
        t = time.time()
        while time.time() - t < timeout:
            for m in self.inp.iter_pending():
                if m.type == "sysex" and list(m.data[:4]) == HEADER + [want]:
                    return list(m.data[4:])
            time.sleep(0.005)
        if not quiet:
            raise SystemExit("the FM-1 did not answer")
        return None

    def send(self, cmd, data):
        self.out.send(self.mido.Message("sysex", data=HEADER + [cmd] + list(data)))


def selftest():
    s = {"name": "BRIGHT", "params": [40, 100, 100, 0, 127, 10, 0, 50, 55, 64, 100, 70, 98, 0, 0, 0, 55, 52, 0, 20,
                                      90, 30, 0, 60, 1, 0, 0, 0]}
    b = encode(s)
    assert len(b) == 71 and b[0] == 1 and b[13] == 28 and b[14:16] == [1, 40], b[:16]   # 16' 40: 168 = 1, 40
    assert decode(b) == s
    s2 = {"name": "X", "params": [-100] + [127] * 27}
    assert decode(encode(s2)) == s2
    bad = encode(s)
    bad[-1] ^= 1
    try:
        decode(bad)
        raise AssertionError("a bad checksum was accepted")
    except ValueError:
        pass
    print("selftest passed")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("action", nargs="?", choices=["backup", "restore", "get-current", "put-current"])
    ap.add_argument("file", nargs="?")
    ap.add_argument("--port")
    ap.add_argument("--yes", action="store_true", help="restore without asking")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if not a.action:
        ap.error("an action or --selftest")
    fm = FM1(a.port)
    print(f"Solinaesque on {fm.name}")
    if a.action == "backup":
        if not a.file:
            ap.error("backup needs a file")
        presets = []
        for slot in range(NUSER):
            d = fm.ask(GETSLOT, [slot], SLOT)
            if d[0] != slot:
                raise SystemExit(f"slot {slot + 1}: another slot answered")
            presets.append({"slot": slot + 1, **readable(decode(d[1:]))})
            print(f"  U{slot + 1:02d}  {presets[-1]['name']}")
        with open(a.file, "w") as f:
            json.dump({"format": 1, "presets": presets}, f, indent=1)
        print(f"{len(presets)} presets -> {a.file}")
    elif a.action == "restore":
        if not a.file:
            ap.error("restore needs a file")
        presets = json.load(open(a.file))["presets"]
        if not a.yes and input(f"write {len(presets)} presets into the FM-1's user slots? [y/N] ").lower() != "y":
            raise SystemExit("nothing written")
        for p in presets:
            slot = int(p["slot"]) - 1
            if not 0 <= slot < NUSER:
                raise SystemExit(f"slot {p['slot']} is not 1..{NUSER}")
            fm.send(SLOT, [slot] + encode(p))
            time.sleep(0.25)                      # (each slot is a flash write on the FM-1, ~50 ms)
            print(f"  U{slot + 1:02d}  {p['name']}")
        print("done")
    elif a.action == "get-current":
        s = readable(decode(fm.ask(GET, [], SOUND)))
        text = json.dumps(s, indent=1)
        open(a.file, "w").write(text) if a.file else print(text)
    else:
        if not a.file:
            ap.error("put-current needs a file")
        fm.send(SOUND, encode(json.load(open(a.file))))
        print("sent (the FM-1 shows SOUND RECEIVED; SAVE stores it)")


if __name__ == "__main__":
    sys.exit(main())
