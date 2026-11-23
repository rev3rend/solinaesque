# Solinaesque

A paraphonic divide-down string machine with 16'/8'/4' registers, a shared filter and envelope, ensemble chorus, flanger, delay, arpeggiator and chord memory.

Custom firmware for the **M-VAVE FM-1** pocket synthesizer, version 0.1. Every feature of the spec is built,
tested on the host and in the browser emulator, and running on a real FM-1 (peak audio load ~45 % with chord
memory, the arp and every effect). The full description: [docs/SPEC.md](docs/SPEC.md).

## What it does

- **Divide-down strings:** 12 phase-locked top oscillators divided down to every note, so any number of keys sound;
  band-limited saws, SHAPE morphs to square and a narrowing pulse; 16', 8' and 4' registers per note.
- **Paraphonic:** one resonant 12 dB low-pass (cutoff, resonance, envelope amount, key tracking) and one ADSR
  (retrigger or legato) for all notes; an LFO for vibrato, PWM and a delayed fade-in.
- **Effects:** the three-phase string ensemble (I, II, III), a flanger and a 1 s stereo tape-like delay.
- **Playing:** HOLD, chord memory, an arpeggiator (five modes, internal tempo or MIDI clock), octave shift.
- **Sounds:** 16 factory sounds, 32 named user slots; settings and chords kept over power-off.
- **MIDI:** notes, pedal, bend, mod wheel and a CC map in over USB and TRS; the keys and the arp out over USB.

## Playing it

| Control | |
| --- | --- |
| Keys, OCT− / OCT+ | Notes, ±2 octaves |
| EDIT · SCL · ENV · LFO · FX (twice: FX 2) · GLO | Strings · Filter · Envelope · LFO · Ensemble/Flanger · Delay · Global pages; KNOB 1..4 edit the page |
| SELECT · HOME | Step through the pages · back home (hold HOME: revert the sound) |
| PRESETS · ALGORITHM | Browse F1..F16, U01..U32 · the ensemble type |
| SEQ · ARP · PLAY · REC | HOLD · arp on/off (hold: its page; + PRESETS: tempo) · arp start/stop · chord memory (hold: clear) |
| SAVE | Tap: name and store a sound (ALGORITHM picks what: the sound, a factory sound, INIT); hold: a plain INIT sound |

Every control in detail, the CC map and the stored data: [docs/SPEC.md](docs/SPEC.md).

Built on the **FM-1 Firmware Kit**: the platform of [Felucca](https://github.com/hugelton/Felucca) (Leo Kuroshita,
Hügelton Instruments) as [M-Vave-OP1](https://github.com/janschulten/m-vave-op1) runs it.

```
firmware/
  hal/        header-only HAL: every register access lives here          (platform, from Felucca)
  loader/     the update loader that writes a new app                     (platform, from Felucca; do not touch)
  src/        boot, main loop, audio ISR, USB-MIDI, update entry, flash,  (platform)
              display and drawing, panel map, MIDI helpers, record store
  app/        app.h: the contract; app.c and its files: this firmware
docs/         SPEC.md (what the firmware does), PROJECT.json (how the repository was set up)
tests/        host tests: the platform's update path, input, USB, store; the app on a simulated FM-1
web/          the web installer (with "Return to official V15") and the browser emulator
tools/        build, package, install, font generators
.claude/      Claude Code skills: fm1-platform (rules, budgets, tests, safety), graphify (code graph)
```

## Build, test, try

```sh
sh .claude/skills/fm1-platform/scripts/setup_env.sh --emsdk   # once: toolchain, SDK files, Emscripten
./build.sh                                                     # build/firmware.fwsc
tests/run_tests.sh                                             # host tests, screenshots in build/shots/
sh web/emu/build.sh && python3 -m http.server -d build/emu 8000   # the emulator: http://localhost:8000/
```

Details: [BUILDING.md](BUILDING.md). With Claude Code, the `fm1-platform` skill holds the rules for every change.

On **macOS**: Docker Desktop must run for `./build.sh` (the JieLi compiler is Linux-only); the build's Python needs
a Pillow with libraqm (e.g. a venv: `PYTHON=~/venv/bin/python3 ./build.sh`); `tests/run_tests.sh` expects a Linux
compiler, so run it in a container (`docker run --rm -v "$PWD":/w -w /w node:22-bookworm sh -c 'apt-get update &&
apt-get install -y python3-pil python3-fonttools && tests/run_tests.sh'`) and the emulator test on macOS with
Emscripten. Running this firmware, macOS may list the FM-1's MIDI port under another device's cached name (the
shared USB id 1209:0001): pass it to the installer with `--port`.

## Safety

The FM-1 has **one firmware bank and no recovery button**. A firmware that does not start needs extra hardware to
recover. So the working order is always **host tests → browser emulator → careful install**, never the reverse.

- The platform keeps the ways back: the boot guard sends the unit to the chip's boot mode (`WL80UBOOT`) after two
  failed boots; OCT− and OCT+ held 5 s does the same on purpose; the update entry accepts a new package over USB
  while the firmware runs. Do not remove or bypass them, and do not change `firmware/loader/`, the boot guard, the
  package identity (`FM-1_900`) or the update path.
- This firmware has run on a real FM-1, but every new build is a new image: test it first, and keep the recovery
  hardware at hand when you can:
  [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter) (a Seeed XIAO RP2040 on the FM-1's USB lines,
  which reads and writes the flash from the boot mode). Back up the flash first.
- Way back while a firmware still starts: `python3 tools/fm1_install.py --stock`, the web installer's "Return to
  official V15", or M-VAVE's own updater.

## Licence

GPL-3.0-only, as Felucca. Copyright (C) 2026 R. Luke DuBois; the platform: Leo Kuroshita (Hügelton Instruments) and
the M-Vave-OP1 contributors. Keep the licence headers, credit Felucca (and M-Vave-OP1) wherever you publish, and give
your users the source. Details and third-party parts: [LICENSING.md](LICENSING.md).

M-VAVE and FM-1 are trademarks of their respective owners. This firmware is not affiliated with or endorsed by them,
nor by Hügelton Instruments.
