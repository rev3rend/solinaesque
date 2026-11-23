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
| HOME | hold (≥ 0.5 s) | Revert: the stored preset again (HOME's `*` shows the sound differs from it) |
| EDIT | press | STRINGS page: 16' level, 8' level, 4' level, shape |
| SCL | press | FILTER page: cutoff, resonance, envelope amount, key tracking |
| ENV | press | ENVELOPE page: attack, decay, sustain, release |
| LFO | press | LFO page: rate, vibrato depth, PWM depth, delay (fade-in) |
| FX | press; again; again | FX 1 (ensemble depth, ensemble rate, flanger amount, flanger rate); FX 2 (delay time, feedback, mix, tone); FX 3 (DLY MODE stereo / ping-pong, DLY SYNC free or 1/2 .. 1/16 with dotted and triplet values; two rows kept for the reverb); then FX 1 again |
| GLO | press; press again | GLOBAL page: trigger mode (retrig / legato), master tune, MIDI channel, volume; again: GLOBAL 2: VELOCITY, AFTERTOUCH, AT DEPTH, CC OUT |
| ARP | tap | Arpeggiator on / off |
| ARP | hold (≥ 0.5 s) | ARP page: mode (up, down, up-down, random, as played), rate (divisions of the tempo), octaves 1..4, gate |
| SEQ | press | Hold (latch) on / off: released keys keep sounding until hold is turned off or a new chord is played |
| REC | tap, hold keys, tap | Chord memory: the notes held while it records become the chord (semitones above the lowest, up to 8 within 4 octaves; the keys play plain while recording); then each key or MIDI note plays the chord on it. Needs 2+ notes. Kept with the settings over power-off |
| REC | hold (≥ 0.5 s) | Clear the chord |
| PLAY | press | The transport: run / stop, the arp on or off. Running sends MIDI Start and MIDI clock (24 a quarter of the internal tempo) on USB and locks the arp's steps to it (its pattern from the top on the first tick); stopping sends MIDI Stop (the arp free-runs on the tempo). While the FM-1 follows MIDI clock, nothing is sent and PLAY stops / continues the arp |
| SAVE | tap | The SAVE screen (below); the keys keep playing |
| SAVE | hold (≥ 0.5 s) | INIT: a plain sound (8' saw, filter open, no LFO or effects), shown as NEW |
| SAVE screen | PRESETS / ALGORITHM / KNOB 1..4 / SAVE | PRESETS: the user slot (U01..U32; from the current user slot, or U01). ALGORITHM: what to store (the current sound, F1..F16, INIT = clear the slot). KNOB 1: the name's cursor, KNOB 2: its letter (space A..Z 0..9 - .), KNOB 3: a word (STRINGS, PAD, CELLO ...), KNOB 4: clear. SAVE stores (the current sound then comes from that slot; a factory sound or INIT goes into the slot and the sound playing stays); any other button cancels |
| SELECT | turn | Next / previous page (STRINGS, FILTER, ENVELOPE, LFO, FX 1, FX 2, ARP, GLOBAL); from HOME, clockwise opens the first, counter-clockwise the last. (The encoders have no push switch: HOME is the way back.) |
| ALGORITHM | turn | Ensemble type: OFF, I (subtle), II (classic three-phase), III (deep), FLANGE (flanger only) |
| PRESETS | turn | Browse sounds: F1..F16 (factory), U01..U32 (user); loads on turn. With ARP held: the arp tempo. On the SAVE screen: the target slot |
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
- ENVELOPE and FILTER use compact rows (32 px) and draw, under them, the ADSR's shape and the filter's response.

### SAVE

- The target slot (U01..U32) large, its current name, `SAVE TO STORE`, `ANY KEY CANCELS`.

### Messages

- A one-second line at the bottom for events: `SAVED U07`, `HOLD ON`, `CHORD SET`, `MIDI CLOCK`, and `SOON` for a
  control whose feature is not built yet.

## Sound

- **Oscillators:** 12 top-octave phase accumulators (one per pitch class, with master tune) divided down by powers
  of two to every MIDI note 0..127: every note of the same pitch class is phase-locked, as in a divide-down organ.
  Only notes that are gated or still in their key-gate release are computed. Band-limited: PolyBLEP saws (a two-sample polynomial at each wrap, 32-bit integer). Measured on B7
  (3951 Hz): the 9th harmonic's fold to 8.5 kHz is 44.5 dB under the fundamental (naive: 19.1), the 7th's to
  16.4 kHz 29.8 dB (naive: 16.9). In the emulator, 27 keys × 3 registers render at ~1.55× the naive cost.
- **Registers:** each sounding note mixes three octaves: 16' (one octave down), 8' (as played), 4' (one octave up),
  each 0..100 %. Pitches past the top of the range fold back by an octave.
- **Shape (KNOB 4 on STRINGS):** continuous, 0..99: saw (0) → saw + square (33) → square (66) → a pulse narrowing
  from 50 % to 10 % (99), shown as SAW, SAW+SQR, SQUARE, PULSE nn%. The square and pulse are two band-limited saws
  a width apart (no DC at any width); the LFO's PWM depth comes on top in milestone 4.
- **Key gates:** each note has a short gate ramp of a few ms on and off so keys never click; the shared envelope
  does the shaping.
- **Shared filter:** one resonant low-pass (state-variable, 12 dB/oct) on the summed notes: cutoff 20 Hz..16 kHz,
  resonance 0..100 %, envelope amount −100..+100 %, key tracking 0..100 % (from the highest sounding note).
  Zero-delay-feedback (trapezoidal) form, in float on the FPU, coefficients once per 32-sample block. ENV AMOUNT
  ±100 % moves the cutoff across its whole range at full envelope; KEY TRACK 100 % follows the keys (1.1 cutoff
  steps a semitone, from C4). RESONANCE 100 % rings but stays bounded (damping down to 0.04, Q 25).
- **Shared envelope:** ADSR, attack 1 ms..5 s (linear), decay 5 ms..5 s, sustain 0..100 %, release 5 ms..10 s
  (decay and release exponential: the time shown is where the sound is ~52 dB down, it ends in exact silence at ~1.4x); drives the
  amplitude and (by the amount) the filter. Trigger mode RETRIG restarts the attack on every new key (from the
  current level, no jump); LEGATO triggers on the first key only. Releases when no key, hold or sustain pedal keeps
  a note.
- **LFO:** one triangle for all notes (as the old machines: the dividers stay locked), 0.05..20 Hz, once per block;
  VIBRATO 0..±50 cents, the mod wheel adds up to ±50 more; PWM 0..100 % sweeps the pulse width ±40 % (kept within
  5..95 %; heard when SHAPE is past SAW); DELAY 0..5 s fades the LFO in after an attack from rest (not on a legato
  or retriggered note while others sound). Pitch modulation is applied to the 12 pitch-class steps per block (a
  cents table, 32-bit), and the BLEP width follows.
- **Effects, in order:** ensemble (multi-tap modulated short delay modelling the three-phase BBD chorus; type on
  ALGORITHM, depth and rate on FX 1) → flanger (short delay with feedback, amount and rate) → stereo delay (time
  10 ms..1 s, or tempo divisions when the arp is synced; feedback 0..95 %; mix 0..100 %; tone: a low-pass in the
  feedback path). The delay uses about 176 KB of pool for 1 s stereo; the ensemble and flanger a few KB.
- **Level:** a soft limiter on the output so full chords at full registers do not clip.
- **Ceiling:** the number of sounding notes is limited by the audio ISR budget (measured; the target is at least 32
  notes × 3 registers, the oldest releasing notes are dropped first). At most 48 notes are rendered per block (held
  first, lowest first): the unit measured under 20 % CPU with all 27 keys × 3 registers.
- **Load shedding:** the main loop watches the audio ISR's load (`audio_cpu_q8`): above 80 % the number of notes
  rendered falls 2 a frame (never below 8), under 60 % it climbs back 1 every 250 ms. Held notes render first, a
  releasing note past the cap is dropped. HOME shows the load (`CPU nn%`, in the accent while shedding).
- **Level:** one register at 100 % peaks at 5000 (Q15) before VOLUME and MASTER; F1 CLASSIC's single note is about
  −18 dBFS at the default VOLUME, a big chord runs into the soft knee (above 20000) and the ceiling.

## Timing and sequencing

- **Arpeggiator:** modes up, down, up-down (the ends once), random, as played (press order); rate 1/4, 1/8, 1/8T,
  1/16, 1/16T, 1/32; octaves 1..4; gate 10..100 %. It takes the held, latched (HOLD) and pedal notes, chord-expanded,
  and sounds one at a time through the shared envelope (RETRIG: each step attacks; gaps release). The first key
  steps at once. Steps land on 32-frame blocks. Measured: 1/16 at 120 BPM steps every 5 512 samples (5 512.5).
- **Tempo:** internal 40..240 BPM (ARP held + PRESETS; the ARP page's footer shows it), counted in samples in the
  audio ISR; MIDI clock in (24 a quarter, any channel) takes over while it comes in (CLK on HOME) and gives way half
  a second after it stops; MIDI start plays the pattern from its top on the next tick, stop silences, continue
  resumes. Measured: ticks every 1 088 samples give a 1/16 step every 6 528.
- **Hold:** latches the held notes; a new set of keys pressed after all were released replaces the latched set.
- **Chord memory:** REC, hold keys, REC: the intervals above the lowest note are stored; then one key (or MIDI
  note) plays the chord on it, paraphonic (all notes into the shared filter and envelope), and HOLD, the pedal and
  the arp see the chord's notes. One chord in v1 (the settings hold room for 8).

## What is stored

In the platform store (36 records of 4 KB) under this app's id:

| Record | Contents |
| --- | --- |
| Settings | Global page (trigger mode, master tune, MIDI channel, volume), TRS MIDI on/off, the last saved preset (loaded at power-on; record version 2 stores it as a code: factory f, user 64 + u, so the factory count can change; a version 1 record is converted), octave shift, arp settings and tempo, the 8 stored chords |
| User presets 1 | U01..U16 (a preset is every sound parameter of the pages and the ensemble type, plus an 11-character name and the 14-bit parameters' fine parts: 68 bytes; record version 2, version 1's 40-byte presets are read too) |
| User presets 2 | U17..U32 |

Factory sounds F1..F16 are built in (read-only): CLASSIC, CELLO, VIOLINS, FULL ENS, DARK PAD, BRIGHT, SWELL,
SPACE, BRASS, ORGAN, HOLLOW, GLASS, BASSES, PLUCK, VOX, ECHOES (designed from the engine; F9..F13 tuned by ear on
the unit, 2026-11-22, the rest approved as they were).
to the defaults. The settings record is written on every SAVE and when the GLOBAL or ARP page is left after a change
(never on a knob turn: flash wear). Each record starts with a version and a payload size (append only).

## MIDI

- **In (USB and TRS):** note on / off on the chosen channel (1..16 or OMNI); sustain pedal (CC64) holds notes; mod
  wheel (CC1) adds vibrato; pitch bend ±2 semitones (all oscillators); CC121 resets bend, mod wheel and pedal; the CC
  map below; MIDI clock, start, stop, continue for the arp (milestone 8). TRS MIDI IN is on by default.
- **CC map** (every control, 0..127 across its range, on the chosen channel; received, and sent when the panel changes
  it, with CC OUT on; a CC received is not sent back; the screen and the screen reader follow):

  | CC | Control | CC | Control | CC | Control | CC | Control |
  | --- | --- | --- | --- | --- | --- | --- | --- |
  | 12 | 16' | 73 | ATTACK | 22 | ENS RATE | 102 | ARP MODE |
  | 13 | 8' | 75 | DECAY | 23 | FLANGER | 103 | ARP RATE |
  | 14 | 4' | 70 | SUSTAIN | 24 | FLG RATE | 104 | ARP OCTAVES |
  | 15 | SHAPE | 72 | RELEASE | 25 | DLY TIME | 105 | ARP GATE |
  | 74 | CUTOFF | 76 | LFO RATE | 26 | FEEDBACK | 106 | TEMPO (40..240 BPM) |
  | 71 | RESONANCE | 77 | VIBRATO | 27 | DLY MIX | 107 | ARP on (>= 64) |
  | 20 | ENV AMOUNT | 78 | PWM | 28 | DLY TONE | 108 | HOLD (>= 64) |
  | 21 | KEY TRACK | 79 | LFO DELAY | 29 | ENSEMBLE type | 109 | PLAY: run (>= 64) / stop |
  | 93 | ENS DEPTH | 30 | TRIGGER | 31 | TUNE | 110 | OCTAVE shift (-2..+2) |
  | 7 | VOLUME | 111 | VELOCITY mode | 112 | AFTERTOUCH mode | 113 | AT DEPTH |

  Also: CC 114 DLY MODE, CC 115 DLY SYNC.

  CC1 mod wheel, CC64 sustain, CC120 / 123 all off, CC121 reset are handled in the audio ISR; a mapped CC is handed
  to the main loop (a counter per control), which owns the parameters. Sending goes the other way through a queue
  to the ISR, which sends it after its notes (one context for MIDI out). A whole new sound (a preset, a revert, INIT,
  a SysEx sound) sends only its Program Change, never all 40 controls.
- **14-bit NRPN** for the filter, the envelope and the effects (16 parameters: CUTOFF, RESONANCE, ENV AMOUNT, KEY
  TRACK, ATTACK, DECAY, SUSTAIN, RELEASE, ENS DEPTH, ENS RATE, FLANGER, FLG RATE, DLY TIME, FEEDBACK, DLY MIX,
  DLY TONE): NRPN 0:<their CC number> (e.g. 0:74 CUTOFF, 0:73 ATTACK), data entry CC 6 (MSB) and CC 38 (LSB),
  0..16383 across the range. These have real 14-bit resolution inside: a fine part of 1/128 step on top of the
  panel's steps, used by the engine (the filter's cutoff continuous, times and rates interpolated, percentages
  scaled), shown on the pages (values between the steps), stored in user presets. Received: NRPN (the MSB alone is
  taken; an RPN selected by CC 101 / 100 is ignored) and their 7-bit CCs (a whole step). Sent when the panel changes
  them: NRPN 0:n as CC 99 0, 98 n, 6 MSB, 38 LSB (all four or none), instead of their CC. The panel's knobs move in
  whole steps (the fine part is kept, and goes at the top of the range).
- **Program Change:** in, 0..15 load F1..F16 and 16..47 U01..U32 (higher: ignored); out when the panel changes the
  preset (not when a Program Change came in).
- **Velocity and aftertouch** (GLOBAL 2): VELOCITY OFF (an organ) / LEVEL (each note's own level, velocity / 127;
  notes from chord memory or arp octaves take the latest velocity) / FILTER (the latest note's velocity: the filter
  envelope scaled by velocity / 100, the cutoff moved -25..+7 steps, centred on the panel keys' 100) / BOTH.
  AFTERTOUCH OFF / VIBRATO (up to +-50 cents x AT DEPTH, on the LFO) / CUTOFF (up to +64 steps x AT DEPTH) / BOTH,
  from the stronger of channel pressure and the strongest held note's polyphonic pressure, smoothed per block; a
  note's pressure goes with it. The FM-1's own keys have no velocity (100) or pressure.
- **SysEx presets** (dump.c, `F0 7D 53 4C <cmd> ... F7`): 01 get the current sound (-> 02); 02 <sound> make it current
  (NEW); 03 <slot> get a user slot (-> 04); 04 <slot> <sound> store it. A sound (format 2, 99 bytes): 02, the name
  (12 bytes), 28, each parameter as two 7-bit bytes (v + 128), each parameter's fine part (28 bytes), a checksum (the
  body's sum & 7F); format 1 (01, no fine parts, 71 bytes) is still taken. Malformed frames change nothing.
  `tools/solinaesque_presets.py` backs the 32 user slots up to JSON and restores them.
- **Out (USB):** the panel keys (as the chord expands them, HOLD keeping them) or, with the arp on, its notes, as note on / off (velocity 100) on the chosen channel (OMNI: channel 1); notes received over MIDI are not echoed (no loops through a DAW); a stop sends every off. No clock out in v1.
- All notes off on MIDI CC123 and when the channel changes.

## Open questions

- Whether the brass/organ "second section" of an ARP Omni style instrument is wanted later.
- More stored chords (8 slots in the settings) and how to pick one.
- The MASTER pot's real range on the unit (the app reads 0..1023 as silent..full).

## Out of scope for v1

- Audio input (the FM-1 has none), sampling.
- A step sequencer or song mode (SEQ is hold).
- MIDI clock out, SysEx preset transfer.
- Per-note filters or envelopes (it is paraphonic by design).

## Version 2 features (planned 2026-11-22, built in this order)

10. **Accessibility (fm1-reader) (done):** the FM-1 speaks through the fm1-reader screen reader
    (https://github.com/charlesvestal/fm1-reader, protocol version 1): USB-MIDI SysEx `F0 7D 53 52 <cmd> ... F7`;
    silent until a reader's HELLO (every 2 s; silent again 5 s after the last, or on BYE); the HELLO answer names the
    firmware ("SOLINAESQUE 0.1", an empty words link: the device speaks in full words); SAY messages (ASCII, at most
    160 characters here: the platform's SysEx queue holds 64 packets) with NOW for what the player caused and KEEP
    for warnings; WHERE (and the first HELLO) reads the whole screen. What is said: page changes with their four
    values, knob turns as "name value" (after 120 ms still, at least every ~500 ms while turning), presets, the
    messages (HOLD, the arp, chords, octave, saves), the SAVE screen and its name editing. Wording per fm1-reader's
    RULES.md: full words, no symbols, the important thing first. Never blocks: a message that does not fit the USB
    queue is dropped. Speech on the computer only (no sounds from the FM-1).
    Built: `sr.c` (the protocol; a readout longer than one SAY goes out in parts as the queue has room) and `speak.c`
    (what changed, spoken); the platform's main loop calls the app's new `app_sysex()` hook before the update entry,
    which takes only frames starting 7D 53 52 and leaves the update protocol's to `ota_service`. Tested: the HELLO
    answer byte for byte, silence before HELLO and after BYE / 5 s, every page's readout within the rules,
    debouncing, warnings, a full queue, and the update frames left alone. On the unit: the fm1-reader web page connects
    and speaks it (2026-11-22); a full re-install through the new hook works.
11. **Full MIDI control (done):** a CC for every parameter (sound, effects, LFO, arp, global), Program Change for presets,
    both received and sent (a knob turned sends its CC, a preset change its Program Change), and a SysEx dump of a
    preset (send and receive).
12. **Velocity and aftertouch (done)** (MIDI only; the keys have none), switchable: velocity to nothing (organ), note level,
    filter envelope, or both; aftertouch (channel and polyphonic) to vibrato, cutoff, or both. Settings on a second
    GLOBAL page (GLOBAL 2, the 9th page; stored at the end of the settings record).
\1
    Built: PLAY is a transport of its own (MIDI Start, clock at 24 a quarter, Stop), the arp on or off; the arp's
    steps lock to the clock it sends (a step on every 6th tick at 1/16, the pattern's top on the first); it
    free-runs when stopped; nothing goes out while following MIDI clock. FX 3: DLY MODE (ping-pong: in on the left,
    each repeat crossing sides) and DLY SYNC (the time from the tempo, or from MIDI clock while it comes in; DLY TIME
    stays for FREE). Measured: 48 ticks a second at 120 BPM, every arp step on its tick; DLY SYNC 1/8 at 120 BPM is
    11 025 samples; ping-pong's first repeat left only, the second right only.
14. **Reverb** after the delay (size, damping, mix), its CPU measured on the unit.
15. **Brass section** (ARP Omni style): a second voicing with its own registers, filter and envelope, layered on
    the strings.
16. **Step sequencer** (asked for 2026-11-23): steps on the transport's clock (the same as the arp and MIDI clock
    out); its design (steps, tracks, the buttons it takes: SEQ is HOLD today) to be settled when it is built.

## Milestones

1. **Skeleton (done, 0.1):** every control from the table wired to a visible effect, all screens drawn, the settings
   and user preset records with defaults, the divide-down bank with naive saws in all three registers through the
   shared ADSR (RETRIG / LEGATO), HOLD, MIDI notes on a channel, sustain pedal, all notes off, note-off rules and
   `app_audio_stop`; SHAPE, FILTER, LFO, FX, ARP, PLAY and REC say `SOON`.
2. **Divide-down bank (done, on the unit):** PolyBLEP saws, the SHAPE morph, load shedding with held notes first
   and releasing notes dropped first, CPU % on HOME. Measured on the FM-1: all 27 keys × 3 registers never above
   20 % CPU; MAX_SOUNDING raised from 32 to 48.
3. **Shared filter and envelope (done):** the filter (a zero-delay-feedback state-variable low-pass on the FPU),
   ADSR with RETRIG / LEGATO, key tracking, the soft knee and ceiling. Measured: CUTOFF 990 Hz leaves 131 Hz within
   0.2 dB and takes 2.1 kHz down 15 dB, 4.2 kHz 26 dB; RESONANCE 90 % lifts the cutoff's harmonic 18.6 dB. In the
   emulator the filter costs about as much as 7..8 note-registers; on the unit, to be read on HOME.
4. **LFO and MIDI in (done):** vibrato, PWM, the LFO's fade-in, pitch bend, mod wheel, CC121, the CC map. Measured:
   VIBRATO 100 % swings −50..+49 cents (the audio's periods vary 1.057×, ±50 cents is 1.0595), PWM 100 % sweeps
   the width 10..90 %, a full bend lands within 0.1 Hz of ±2 semitones.
5. **Ensemble (done):** three taps of one short line, each swept by a slow sine (ENS RATE) plus a fast one at 10×,
   120° apart, taps 1 / 3 left / right and tap 2 in both; types I (slow, subtle), II (slow + fast), III (deep),
   FLANGE (the flanger alone); ENS DEPTH scales the sweep (60 %: the type's own; 0 %: still taps, L = R). Equal-power
   dry / wet: the level stays (measured 0.97× dry). Measured L/R correlation: I 0.56, II 0.25, III 0.48.
6. **Flanger and delay (done):** flanger 0.3..3.8 ms swept by a triangle, L / R in opposite phase, FLANGER sets
   feedback and mix (FLANGE type: at least 60 %). Delay: stereo, up to 1.02 s (int16 lines, 176 KB of pool), time
   changes glide like tape, FEEDBACK with DLY TONE's low-pass in the loop, DLY MIX added to the dry; tails end in
   exact silence, a stop silences it at once. Measured: the echo within 0.1 ms of DLY TIME, FEEDBACK 50 % gives
   half the level on the second echo. All three in float on the FPU (fx.c, no soft float).
   On the unit: F8 SPACE (ensemble, flanger and delay all on) with a big chord peaks at 25 % CPU.
   With the arp and chord memory (milestone 8 build) the unit peaks near 45 % (a big chord through chord memory
   reaches the 48-note ceiling); shedding starts at 80 %.
7. **Presets (done):** 16 factory sounds (8 redesigned, 8 new), naming on the SAVE screen, ALGORITHM there picks
   what to store (restore a factory sound into a slot, INIT clears one), the edited `*` and HOME held to revert,
   SAVE held for INIT. Tuning the factory sounds by ear on the unit stays open.
8. **Hold, arpeggiator, chord memory (done):** HOLD (milestone 1), the arp with its five modes, internal tempo and
   MIDI clock, PLAY and MIDI start / stop / continue, chord memory (one chord, stored), MIDI note out over USB.
9. **Polish (done):** the ENVELOPE page draws the ADSR (widths by the knobs, decay and release as they sound), the
   FILTER page the low-pass's response (log frequency, +24..−36 dB, the resonance peak, integer maths), both on
   compact rows; the CC map in the spec; budgets checked (image 85 KB of 582, RAM 9.2 KB of 50, pool 252 KB of
   382); the README describes the instrument; installed and played on the unit through every milestone.
