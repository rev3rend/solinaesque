# Solinaesque for MIDItoSpeech

`Solinaesque.json` is a device file for **MIDItoSpeech**, part of [synthaccess](https://github.com/IDMNYU/synthaccess)
(IDM NYU): software on a computer that speaks what a synthesizer sends over MIDI. With it, MIDItoSpeech says what
you change on the FM-1, e.g. "filter resonance 62 percent", "delay sync eighth note triplets", "preset factory 3,
violins".

## Using it

1. Connect the FM-1 by USB. On GLOBAL 2 (press GLO twice), keep **CC OUT** on: that is what sends your changes.
2. Copy `Solinaesque.json` into MIDItoSpeech's `devices` folder.
   - Web version: also add the file name to `WebMIDI/devlist.txt` (or run `makedevlist.sh`), or load it with the
     page's file control (key `l`).
   - Max version: open it from the patch.
3. Choose the FM-1's MIDI port as MIDItoSpeech's input (macOS may show it under another name, e.g. "BlessedUnion
   BlessedMIDI") and channel 1. The FM-1 sends on its MIDI channel (GLOBAL page; OMNI sends on channel 1).

## What it covers

- **CC:** the controls the FM-1 sends as 7-bit CC: registers, shape, the LFO, the ensemble type, trigger mode,
  tune, volume, the arpeggiator, tempo, ARP and HOLD on / off, octave shift, velocity and aftertouch settings, delay
  mode and sync.
- **NRPN:** the filter, envelope and effect parameters, which the FM-1 sends as 14-bit NRPN 0:<CC number>
  (`"hires": "true"`).
- **Program Change:** the presets by name (factory 1..16, user 1..32).
- Percentages and linear ranges read as numbers with their unit; named choices read as words; the exponential ones
  (cutoff, attack, decay, release, the rates, delay time) use MIDItoSpeech's `floatmapexp`: `map` is
  `[0, max, lo, hi, hi/lo]`, which reads as `lo × (hi/lo)^t`, the firmware's own curves (within its rounding).
  They speak whole milliseconds and hertz (`"precision": 0`), and the rates with one decimal.
- No transport (PLAY, MIDI clock, start, stop): MIDItoSpeech does not need it.

## Keeping it current

The file is generated from the firmware's own tables, so it matches the FM-1's screen and its screen reader:

```sh
cc -O1 -Ibuild/gen -Ifirmware/src -Ifirmware/app -Itests -o build/host/gen_synthaccess tools/gen_synthaccess.c -lm
build/host/gen_synthaccess > docs/synthaccess/Solinaesque.json
```

`tests/run_tests.sh` fails if the committed file is not what the firmware generates now, and checks every value the
FM-1 sends against MIDItoSpeech's reading of it (`tests/synthaccess_check.py`).
