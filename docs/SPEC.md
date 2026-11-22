# Solinaesque: specification

Version 0.1 (skeleton) · R. Luke DuBois

## What it is

Solinaesque is a paraphonic string machine for the M-VAVE FM-1, in the spirit of the 1970s divide-down string
ensembles. A bank of 12 top-octave oscillators is divided down to every note of the MIDI range, as in an organ, so
every key has its own phase-locked oscillator and any number of keys can sound together. Each note sounds in up to
three registers (16', 8', 4') with their own levels. All notes share one filter and one envelope (retriggered by
each new key, or legato: selectable). The signature ensemble chorus, a flanger and a stereo delay follow. It has an
arpeggiator, hold (latch) and chord memory, 8 factory sounds and 32 user presets, and plays over USB and TRS MIDI;
the keys and the arpeggiator are sent out as MIDI notes over USB.

## Controls

The keys play notes in every mode (except while a chord is being recorded, see REC). Pages are opened with their
printed buttons; KNOB 1..4 always edit the four parameters of the page on screen.

| Control | Action | What it does |
| --- | --- | --- |
| 27 keys (F3..G5) | press / release | Note on / off at the current octave shift (the arp's input when ARP is on; the chord's root when a chord is stored) |
| OCT− / OCT+ | press | Octave shift −2..+2 (shown on HOME) |
| OCT− + OCT+ | hold 5 s | Reserved by the platform (recovery) |
| HOME | press | HOME screen |
| EDIT | press | STRINGS page: 16' level, 8' level, 4' level, shape |
| SCL | press | FILTER page: cutoff, resonance, envelope amount, key tracking |
| ENV | press | ENVELOPE page: attack, decay, sustain, release |
| LFO | press | LFO page: rate, vibrato depth, PWM depth, delay (fade-in) |
| FX | press; press again | FX 1 page (ensemble depth, ensemble rate, flanger amount, flanger rate); pressed on FX 1: FX 2 page (delay time, delay feedback, delay mix, delay tone); pressed on FX 2: back to FX 1 |
| GLO | press | GLOBAL page: trigger mode (retrig / legato), master tune, MIDI channel, volume |
| ARP | press | Arpeggiator on / off |
| ARP | hold (≥ 0.5 s) | ARP page: mode (up, down, up-down, random, as played), rate (divisions of the tempo), octaves 1..4, gate |
| SEQ | press | Hold (latch) on / off: released keys keep sounding until hold is turned off or a new chord is played |
| REC | press, play keys, press REC again | Chord memory: the held keys become the chord; then each key plays the chord transposed to it. REC held ≥ 0.5 s clears the chord. Up to 8 chords stored; the chord slot is picked on the ARP page's second view (v1: one chord, see open questions) |
| PLAY | press | Arp clock: runs on the internal tempo, or follows MIDI clock when it is present; PLAY starts / stops a latched arp |
| SAVE | press | Opens SAVE: PRESETS picks the user slot (U01..U32; starts at the current user slot, or U01 from a factory sound), SAVE again stores the sound under its current name (and the settings), any other button cancels; the keys keep playing |
| SELECT | turn | Next / previous page (STRINGS, FILTER, ENVELOPE, LFO, FX 1, FX 2, ARP, GLOBAL); from HOME, clockwise opens the first, counter-clockwise the last. (The encoders have no push switch: HOME is the way back.) |
| ALGORITHM | turn | Ensemble type: OFF, I (subtle), II (classic three-phase), III (deep), FLANGE (flanger only) |
| PRESETS | turn | Browse sounds: F1..F8 (factory), U01..U32 (user); loads on turn. With ARP held: the arp tempo. On the SAVE screen: the target slot |
| KNOB 1..4 | turn | The four parameters of the current page (on HOME: 16', 8', 4', cutoff) |
| MASTER pot | turn | Output level, with VOLUME (read by the app every 4 frames, smoothed, a small dead band) |

## Screens

240 × 240, dark and warm: near-black background, amber and orange text and bars, dim brown for inactive parts, like
the panel of a 1970s string machine. Every screen is drawn lazily (a signature per screen, redrawn only on change).

### HOME

- Top: the name SOLINAESQUE (large font), the preset name and number below it (e.g. `F1 CLASSIC`).
- Middle: three vertical drawbars for 16', 8', 4' and a fourth bar for cutoff (KNOB 1..4 on HOME).
- Bottom line: ensemble type, octave shift, trigger mode, status flags (ARP, HOLD, CHORD, MIDI clock), and a row of
  dots showing the sounding notes.

### Parameter pages (STRINGS, FILTER, ENVELOPE, LFO, FX 1, FX 2, ARP, GLOBAL)

- Title at the top with the page's position (e.g. `ENVELOPE 3/8`).
- Four rows, one per knob: name, value (with unit where it has one: ms, Hz, %, semitones) and a horizontal bar.
- ENVELOPE also draws the ADSR shape; FILTER a small response curve (milestone, a plain bar first).

### SAVE

- The target slot (U01..U32) large, its current name, `SAVE TO STORE`, `ANY KEY CANCELS`.

### Messages

- A one-second line at the bottom for events: `SAVED U07`, `HOLD ON`, `CHORD SET`, `MIDI CLOCK`, and `SOON` for a
  control whose feature is not built yet.

## Sound

- **Oscillators:** 12 top-octave phase accumulators (one per pitch class, with master tune) divided down by powers
  of two to every MIDI note 0..127: every note of the same pitch class is phase-locked, as in a divide-down organ.
  Only notes that are gated or still in their key-gate release are computed. Band-limited (PolyBLEP or equivalent).
- **Registers:** each sounding note mixes three octaves: 16' (one octave down), 8' (as played), 4' (one octave up),
  each 0..100 %. Pitches past the top of the range fold back by an octave.
- **Shape (KNOB 4 on STRINGS):** saw → saw+square → square → PWM (continuous, with the LFO's PWM depth on top).
- **Key gates:** each note has a short gate ramp of a few ms on and off so keys never click; the shared envelope
  does the shaping.
- **Shared filter:** one resonant low-pass (state-variable, 12 dB/oct) on the summed notes: cutoff 20 Hz..16 kHz,
  resonance 0..100 %, envelope amount −100..+100 %, key tracking 0..100 % (from the highest sounding note).
- **Shared envelope:** ADSR, attack 1 ms..5 s (linear), decay 5 ms..5 s, sustain 0..100 %, release 5 ms..10 s
  (decay and release exponential: the time shown is where the sound is ~52 dB down, it ends in exact silence at ~1.4x); drives the
  amplitude and (by the amount) the filter. Trigger mode RETRIG restarts the attack on every new key (from the
  current level, no jump); LEGATO triggers on the first key only. Releases when no key, hold or sustain pedal keeps
  a note.
- **LFO:** triangle, 0.05..20 Hz; vibrato 0..±50 cents; PWM 0..100 %; delay 0..5 s fade-in from the trigger. The mod
  wheel adds vibrato.
- **Effects, in order:** ensemble (multi-tap modulated short delay modelling the three-phase BBD chorus; type on
  ALGORITHM, depth and rate on FX 1) → flanger (short delay with feedback, amount and rate) → stereo delay (time
  10 ms..1 s, or tempo divisions when the arp is synced; feedback 0..95 %; mix 0..100 %; tone: a low-pass in the
  feedback path). The delay uses about 176 KB of pool for 1 s stereo; the ensemble and flanger a few KB.
- **Level:** a soft limiter on the output so full chords at full registers do not clip.
- **Ceiling:** the number of sounding notes is limited by the audio ISR budget (measured; the target is at least 32
  notes × 3 registers, the oldest releasing notes are dropped first). The skeleton renders at most 32 notes per block,
  lowest first.
- **Load shedding:** the main loop watches the audio ISR's load (`audio_cpu_q8`): above 80 % the number of notes
  rendered falls 2 a frame (never below 8), under 60 % it climbs back 1 every 250 ms. Held notes render first, a
  releasing note past the cap is dropped. HOME shows the load (`CPU nn%`, in the accent while shedding).
- **Level:** one register at 100 % peaks at 5000 (Q15) before VOLUME and MASTER; F1 CLASSIC's single note is about
  −18 dBFS at the default VOLUME, a big chord runs into the soft knee (above 20000) and the ceiling.

## Timing and sequencing

- **Arpeggiator:** modes up, down, up-down, random, as played; rate 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32; octaves
  1..4; gate 10..100 %. Takes the held (or latched) notes as input; with a stored chord, the chord's notes.
- **Tempo:** internal 40..240 BPM (on the ARP page, PRESETS turns it while ARP is held), or MIDI clock in when it is
  present (it wins; `MIDI CLOCK` shown). The clock counts samples in the audio ISR.
- **Hold:** latches the held notes; a new set of keys pressed after all were released replaces the latched set.
- **Chord memory:** REC, hold keys, REC: the intervals are stored relative to the lowest note; then one key plays
  the chord on it (paraphonic: all notes into the shared filter and envelope).

## What is stored

In the platform store (36 records of 4 KB) under this app's id:

| Record | Contents |
| --- | --- |
| Settings | Global page (trigger mode, master tune, MIDI channel, volume), TRS MIDI on/off, the last saved preset (loaded at power-on), octave shift, arp settings and tempo, the 8 stored chords |
| User presets 1 | U01..U16 (a preset is every sound parameter of the pages and the ensemble type, plus an 11-character name: 40 bytes) |
| User presets 2 | U17..U32 |

Factory sounds F1..F8 are built in (read-only): e.g. CLASSIC, CELLO, VIOLINS, FULL ENS, DARK PAD, BRIGHT, SWELL,
SPACE. A preset loads the sound; SAVE stores the current sound to a user slot. Damaged or foreign records fall back
to the defaults. The settings record is written on every SAVE and when the GLOBAL or ARP page is left after a change
(never on a knob turn: flash wear). Each record starts with a version and a payload size (append only).

## MIDI

- **In (USB and TRS):** note on / off on the chosen channel (1..16 or OMNI); sustain pedal (CC64) holds notes; mod
  wheel (CC1) adds vibrato; pitch bend ±2 semitones (all oscillators); CCs for the main parameters (registers,
  cutoff, resonance, envelope, ensemble, delay; a fixed map listed in the docs); MIDI clock, start, stop, continue
  for the arp. TRS MIDI IN is on by default (can be turned off).
- **Out (USB):** the keys and the arpeggiator's notes as note on / off on the chosen channel; no clock out in v1.
- All notes off on MIDI CC123 and when the channel changes.

## Open questions

- How many sounding notes the ISR really carries (registers × notes × the filter and effects): measured in the
  emulator and on the unit, then the ceiling set.
- Chord memory: one chord in the first build; the 8 stored chords and how to pick them (ARP page second view, or
  REC + a black key) to be decided once it plays.
- Whether the brass/organ "second section" of an ARP Omni style instrument is wanted later.
- The factory sounds' exact values (tuned by ear in the emulator).
- The CC map's numbers.
- Naming user presets (v1 keeps the name of the sound that was saved: a name editor on the keys?).
- The MASTER pot's real range on the unit (the app reads 0..1023 as silent..full).

## Out of scope for v1

- Audio input (the FM-1 has none), sampling.
- A step sequencer or song mode (SEQ is hold).
- MIDI clock out, SysEx preset transfer.
- Per-note filters or envelopes (it is paraphonic by design).

## Milestones

1. **Skeleton (done, 0.1):** every control from the table wired to a visible effect, all screens drawn, the settings
   and user preset records with defaults, the divide-down bank with naive saws in all three registers through the
   shared ADSR (RETRIG / LEGATO), HOLD, MIDI notes on a channel, sustain pedal, all notes off, note-off rules and
   `app_audio_stop`; SHAPE, FILTER, LFO, FX, ARP, PLAY and REC say `SOON`.
2. **Divide-down bank:** band-limited saws (PolyBLEP or equivalent), shape morph, the voice ceiling measured on the
   unit (`audio_cpu_q8`) and the oldest releasing notes dropped first.
3. **Shared filter and envelope:** the state-variable filter, ADSR with RETRIG / LEGATO, key tracking, the limiter.
4. **LFO and MIDI in completed:** vibrato, PWM, delay; pitch bend, mod wheel, sustain, the CC map.
5. **Ensemble:** the three-phase chorus and its types on ALGORITHM.
6. **Flanger and delay:** both effects, the pool budget checked.
7. **Presets:** the factory sounds tuned, SAVE to user slots, PRESETS browsing.
8. **Hold, arpeggiator, chord memory:** latch, the arp with internal tempo and MIDI clock, MIDI note out, chords.
9. **Polish:** ENVELOPE and FILTER drawings, the CC map in the docs, budgets checked, first install on a unit.
