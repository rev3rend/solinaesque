/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 R. Luke DuBois */
/* Solinaesque's data: the parameters (what each knob edits, its range and how it reads), the sound being played
 * (cur), the global settings (set), the 8 factory sounds and the 32 user presets, and their records in the store
 * (store.c, docs/SPEC.md "What is stored"). Main loop only: the sound engine (sound.c) never reads these directly,
 * it gets the derived numbers through snd_update(). Every stored value is checked against PARAMS on load. */

/* ------------------------------------------------------------ parameters --- */
/* a parameter's kind: how its stored int8 reads on the screen and what it means to the engine */
enum { K_PCT, K_BIP, K_ATK, K_DEC, K_REL, K_HZ, K_CUT, K_DLY, K_MS, K_SHAPE, K_TRIG, K_TUNE, K_CH, K_ARPM, K_ARPR,
       K_OCT, K_VELM, K_ATM, K_ONOFF, K_DMODE, K_DSYNC };

typedef struct {
    const char *name;
    uint8_t kind;
    int8_t lo, hi, def;
    uint8_t live;                /* 1: the engine uses it already; 0: drawn with SOON (a later milestone) */
} param_t;

/* the sound's parameters (a preset): pages STRINGS, FILTER, ENVELOPE, LFO, FX 1, FX 2, then the ensemble type on
 * ALGORITHM. Stored by index: append only, never reorder (the store's records name them by position). */
enum { P_R16, P_R8, P_R4, P_SHAPE, P_CUT, P_RES, P_FENV, P_KTRK, P_ATK, P_DEC, P_SUS, P_REL, P_LRATE, P_VIB, P_PWM,
       P_LDLY, P_ENSD, P_ENSR, P_FLGA, P_FLGR, P_DTIME, P_DFB, P_DMIX, P_DTONE, P_ENS, P_DMODE, P_DSYNC, P_RVMIX, P_RVSIZE, NSP };
#define NSP_STORED 32            /* room in a stored preset for parameters added later (28 up to user records v2) */
static const param_t SP[NSP] = {
    {"16'", K_PCT, 0, 100, 70, 1},     {"8'", K_PCT, 0, 100, 100, 1},    {"4'", K_PCT, 0, 100, 40, 1},
    {"SHAPE", K_SHAPE, 0, 99, 0, 1},   {"CUTOFF", K_CUT, 0, 127, 127, 1}, {"RESONANCE", K_PCT, 0, 100, 0, 1},
    {"ENV AMOUNT", K_BIP, -100, 100, 0, 1}, {"KEY TRACK", K_PCT, 0, 100, 0, 1},
    {"ATTACK", K_ATK, 0, 127, 70, 1},  {"DECAY", K_DEC, 0, 127, 64, 1},  {"SUSTAIN", K_PCT, 0, 100, 100, 1},
    {"RELEASE", K_REL, 0, 127, 70, 1},
    {"RATE", K_HZ, 0, 127, 64, 1},     {"VIBRATO", K_PCT, 0, 100, 0, 1}, {"PWM", K_PCT, 0, 100, 0, 1},
    {"DELAY", K_MS, 0, 125, 0, 1},
    {"ENS DEPTH", K_PCT, 0, 100, 60, 1}, {"ENS RATE", K_HZ, 0, 127, 40, 1}, {"FLANGER", K_PCT, 0, 100, 0, 1},
    {"FLG RATE", K_HZ, 0, 127, 20, 1},
    {"DLY TIME", K_DLY, 0, 127, 90, 1}, {"FEEDBACK", K_PCT, 0, 95, 30, 1}, {"DLY MIX", K_PCT, 0, 100, 0, 1},
    {"DLY TONE", K_PCT, 0, 100, 60, 1},
    {"ENSEMBLE", K_PCT, 0, 4, 2, 1},
    /* (appended, milestone 13: older presets hold 0 here, STEREO and FREE) */
    {"DLY MODE", K_DMODE, 0, 1, 0, 1}, {"DLY SYNC", K_DSYNC, 0, 9, 0, 1},
    /* (appended, milestone 14) */
    {"REVERB", K_PCT, 0, 100, 0, 1},   {"REV SIZE", K_PCT, 0, 100, 50, 1},
};
static const char *const ENS_NAME[5] = {"OFF", "I", "II", "III", "FLANGE"};

/* the global settings on the GLOBAL and ARP pages (stored in the settings record, not in a preset) */
enum { G_TRIG, G_TUNE, G_MIDICH, G_VOL, G_ARPM, G_ARPR, G_ARPO, G_ARPG, NG };
static const param_t GP[NG] = {
    {"TRIGGER", K_TRIG, 0, 1, 0, 1},   {"TUNE", K_TUNE, -100, 100, 0, 1}, {"MIDI CH", K_CH, 0, 16, 0, 1},
    {"VOLUME", K_PCT, 0, 100, 80, 1},
    {"MODE", K_ARPM, 0, 4, 0, 1},      {"RATE", K_ARPR, 0, 5, 3, 1},      {"OCTAVES", K_OCT, 1, 4, 1, 1},
    {"GATE", K_PCT, 10, 100, 50, 1},
};

/* pages: four parameters each, KNOB 1..4 top to bottom; 0..5 the sound's, 6..7 the settings' */
/* the MIDI settings on GLOBAL 2 (GLO pressed again): kept at the end of the settings record (set.g2: append only) */
enum { G2_VEL, G2_AT, G2_ATD, G2_CCOUT, NG2 };
static const param_t GP2[NG2] = {
    {"VELOCITY", K_VELM, 0, 3, 0, 1},  {"AFTERTOUCH", K_ATM, 0, 3, 0, 1}, {"AT DEPTH", K_PCT, 0, 100, 50, 1},
    {"CC OUT", K_ONOFF, 0, 1, 1, 1},
};

enum { PG_STR, PG_FLT, PG_ENV, PG_LFO, PG_FX1, PG_FX2, PG_FX3, PG_ARP, PG_GLO, PG_MIDI, NPAGES, PG_HOME = NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"STRINGS", "FILTER", "ENVELOPE", "LFO", "FX 1", "FX 2", "FX 3", "ARP",
                                              "GLOBAL",
                                              "GLOBAL 2"};

/* the sound pages' rows: their parameters (-1 would be an empty row) */
static const int8_t PAGE_P[PG_ARP][4] = {
    {P_R16, P_R8, P_R4, P_SHAPE},     {P_CUT, P_RES, P_FENV, P_KTRK},   {P_ATK, P_DEC, P_SUS, P_REL},
    {P_LRATE, P_VIB, P_PWM, P_LDLY},  {P_ENSD, P_ENSR, P_FLGA, P_FLGR}, {P_DTIME, P_DFB, P_DMIX, P_DTONE},
    {P_DMODE, P_DSYNC, P_RVMIX, P_RVSIZE},
};

/* DLY SYNC: the delay's time as a share of a quarter note, x 24 (0: FREE, DLY TIME) */
static const uint8_t DSYNC_F24[10] = {0, 48, 36, 24, 16, 18, 12, 8, 9, 6};

/* the exponential curves: lo * r^x, r in Q16 so that x = 127 lands on hi (ratio = (hi / lo)^(1 / 127)); an integer
 * loop instead of a libm the target does not have, main loop only (<= 127 multiplies) */
static uint32_t expo(uint32_t lo, uint32_t r_q16, int32_t x)
{
    uint64_t v = (uint64_t)lo << 16;
    while (x-- > 0)
        v = v * r_q16 >> 16;
    return (uint32_t)((v + 32768u) >> 16);
}

/* a kind's engine value: ms for times, mHz for rates, Hz for the cutoff */
static uint32_t pv_ms(uint8_t kind, int32_t x)
{
    switch (kind) {
    case K_ATK: return expo(1, 70082u, x);                /* 1 ms .. 5 s */
    case K_DEC: return expo(5, 69199u, x);                /* 5 ms .. 5 s */
    case K_REL: return expo(5, 69578u, x);                /* 5 ms .. 10 s */
    case K_HZ: return expo(50, 68702u, x);                /* 0.05 .. 20 Hz, in mHz */
    case K_CUT: return expo(20, 69078u, x);               /* 20 Hz .. 16 kHz */
    case K_DLY: return expo(10, 67956u, x);               /* 10 ms .. 1 s */
    case K_MS: return (uint32_t)x * 40u;                  /* 0 .. 5 s, linear */
    }
    return (uint32_t)x;
}

/* a kind's engine value between steps x and x + 1 (f / 128 of the way): the 14-bit parameters' fine part */
static uint32_t pv_ms_f(uint8_t kind, int32_t x, uint32_t f)
{
    uint32_t a = pv_ms(kind, x);
    if (!f)
        return a;
    return a + (uint32_t)(((uint64_t)(pv_ms(kind, x + 1) - a) * f) >> 7);
}

static const char *const SHAPE_NAME[3] = {"SAW", "SAW+SQR", "SQUARE"};
static const char *const TRIG_NAME[2] = {"RETRIG", "LEGATO"};
static const char *const ARPM_NAME[5] = {"UP", "DOWN", "UP-DOWN", "RANDOM", "PLAYED"};
static const char *const ARPR_NAME[6] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};

static void cat(char *d, const char *s, unsigned max) { str_cpy(d + str_len(d), s, max - str_len(d)); }

/* the text a parameter's value shows: "100%", "250 ms", "1.20 s", "2.5 kHz", "RETRIG", "+12 ct" */
static void pv_text_f(char *b, unsigned max, const param_t *p, int32_t x, uint32_t f);
static void pv_text(char *b, unsigned max, const param_t *p, int32_t x) { pv_text_f(b, max, p, x, 0); }

/* ... with a fine part: times, rates and the cutoff show the value between the steps */
static void pv_text_f(char *b, unsigned max, const param_t *p, int32_t x, uint32_t f)
{
    uint32_t v = pv_ms_f(p->kind, x, f);
    char t[16];
    b[0] = 0;
    switch (p->kind) {
    case K_PCT:
    case K_BIP:
        fmt_int(t, x);
        if (p->kind == K_BIP && x > 0)
            cat(b, "+", max);
        cat(b, t, max);
        cat(b, "%", max);
        return;
    case K_ATK: case K_DEC: case K_REL: case K_DLY: case K_MS:
        if (v < 1000u) {
            fmt_int(t, (int32_t)v);
            cat(b, t, max);
            cat(b, " ms", max);
        } else {
            fmt_fix(t, (int32_t)(v / 10u), 2);
            cat(b, t, max);
            cat(b, " s", max);
        }
        return;
    case K_HZ:
        fmt_fix(t, (int32_t)(v < 10000u ? v / 10u : v / 100u), v < 10000u ? 2 : 1);
        if (t[0] == '.')                         /* fmt_fix writes ".33": "0.33" */
            cat(b, "0", max);
        cat(b, t, max);
        cat(b, " Hz", max);
        return;
    case K_CUT:
        if (v < 1000u) {
            fmt_int(t, (int32_t)v);
            cat(b, t, max);
            cat(b, " Hz", max);
        } else {
            fmt_fix(t, (int32_t)(v / 100u), 1);
            cat(b, t, max);
            cat(b, " kHz", max);
        }
        return;
    case K_SHAPE:                                /* sound.c snd_update: 0 saw, 33 saw + square, 66 square, 99 pulse 10 % */
        if (x > 66) {                            /* the pulse's width: 50 % at 66 .. 10 % at 99 */
            fmt_int(t, 50 - (x - 66) * 40 / 33);
            str_cpy(b, "PULSE ", max);
            cat(b, t, max);
            cat(b, "%", max);
        } else
            str_cpy(b, SHAPE_NAME[!x ? 0 : x < 50 ? 1 : 2], max);
        return;
    case K_TRIG: str_cpy(b, TRIG_NAME[x & 1], max); return;
    case K_TUNE:
        fmt_int(t, x);
        if (x > 0)
            cat(b, "+", max);
        cat(b, t, max);
        cat(b, " ct", max);
        return;
    case K_CH:
        if (!x) {
            str_cpy(b, "OMNI", max);
            return;
        }
        fmt_int(t, x);
        str_cpy(b, "CH ", max);
        cat(b, t, max);
        return;
    case K_ARPM: str_cpy(b, ARPM_NAME[x % 5], max); return;
    case K_ARPR: str_cpy(b, ARPR_NAME[x % 6], max); return;
    case K_VELM: {
        static const char *const N[4] = {"OFF", "LEVEL", "FILTER", "BOTH"};
        str_cpy(b, N[x & 3], max);
        return;
    }
    case K_ATM: {
        static const char *const N[4] = {"OFF", "VIBRATO", "CUTOFF", "BOTH"};
        str_cpy(b, N[x & 3], max);
        return;
    }
    case K_ONOFF: str_cpy(b, x ? "ON" : "OFF", max); return;
    case K_DMODE: str_cpy(b, x ? "PING-PONG" : "STEREO", max); return;
    case K_DSYNC: {
        static const char *const N[10] = {"FREE", "1/2", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16.", "1/16"};
        str_cpy(b, N[x < 0 ? 0 : x > 9 ? 9 : x], max);
        return;
    }
    case K_OCT:
        fmt_int(t, x);
        str_cpy(b, t, max);
        cat(b, x == 1 ? " OCTAVE" : " OCTAVES", max);
        return;
    }
    fmt_int(b, x);
}

static int8_t clamp8(int32_t v, const param_t *p) { return (int8_t)(v < p->lo ? p->lo : v > p->hi ? p->hi : v); }

/* ------------------------------------------------------------ presets --- */
#define NAME_LEN 12              /* a preset's name, 0-terminated (10 characters shown) */
typedef struct {
    char name[NAME_LEN];
    int8_t p[NSP_STORED];        /* SP order; the tail beyond NSP is 0 */
    uint8_t f[NSP_STORED];       /* (user records version 2) the fine part of the 14-bit parameters (HIRES), in
                                  * 1/128 of a step: the value is p + f / 128; 0 for the others and at the top */
} preset_t;

/* the parameters with 14-bit resolution (sent and received as NRPN 0:<their CC number>, docs/SPEC.md): the filter,
 * the envelope, the effects. A bit per parameter index. */
#define HIRES_MASK (1u << P_CUT | 1u << P_RES | 1u << P_FENV | 1u << P_KTRK | 1u << P_ATK | 1u << P_DEC | \
                    1u << P_SUS | 1u << P_REL | 1u << P_ENSD | 1u << P_ENSR | 1u << P_FLGA | 1u << P_FLGR | \
                    1u << P_DTIME | 1u << P_DFB | 1u << P_DMIX | 1u << P_DTONE | 1u << P_RVMIX | 1u << P_RVSIZE)
#define NHIRES 18u
static const uint8_t HIRES[NHIRES] = {P_CUT, P_RES, P_FENV, P_KTRK, P_ATK, P_DEC, P_SUS, P_REL,
                                      P_ENSD, P_ENSR, P_FLGA, P_FLGR, P_DTIME, P_DFB, P_DMIX, P_DTONE,
                                      P_RVMIX, P_RVSIZE};
static int hires(uint32_t i) { return i < 32u && ((HIRES_MASK >> i) & 1u); }

#define NFACT 16
#define NUSER 32
#define NPRESETS (NFACT + NUSER) /* PRESETS browses F1..F16 then U01..U32 */
#define PRESET_NEW NPRESETS      /* cur_index of a sound from no preset (INIT): shown as NEW */
/* the factory sounds, by parameter (data.c SP order):
 *   16' 8' 4' SHAPE | CUTOFF RES ENV-AMT KEY-TRK | ATTACK DECAY SUSTAIN RELEASE | LFO RATE VIBRATO PWM DELAY |
 *   ENS-DEPTH ENS-RATE FLANGER FLG-RATE | DLY-TIME FEEDBACK DLY-MIX DLY-TONE | ENSEMBLE (0 off, 1 I, 2 II, 3 III, 4 FL)
 * Indexes, not units: CUTOFF 80 ~1.4 kHz, 100 ~3.8 kHz, 110 ~6.5 kHz; ATTACK 60 ~60 ms, 80 ~210 ms, 100 ~830 ms;
 * RELEASE 70 ~330 ms, 85 ~810 ms, 100 ~2 s; LFO RATE 52 ~0.6 Hz, 98 ~5 Hz; DLY TIME 90 ~260 ms, 110 ~540 ms.
 * Designed from the engine; to be tuned by ear on the unit. */
static const preset_t FACTORY[NFACT] = {
    /* F1 the string machine: 16' + 8' + some 4', a gentle swell, ensemble II at ~0.6 / 6 Hz */
    {"CLASSIC", {60, 100, 50, 0, 105, 0, 0, 30, 75, 64, 100, 85, 98, 0, 0, 0, 60, 52, 0, 20, 90, 30, 0, 60, 2, 0, 0, 0, 50}},
    /* F2 low, darker, a slow bow and late vibrato */
    {"CELLO", {100, 70, 0, 10, 85, 15, 20, 50, 85, 70, 90, 80, 98, 15, 0, 15, 50, 52, 0, 20, 90, 30, 0, 60, 1, 0, 0, 0, 50}},
    /* F3 8' + 4', bright, vibrato fading in, a touch of echo */
    {"VIOLINS", {0, 100, 80, 0, 112, 5, 10, 40, 80, 64, 100, 85, 100, 20, 0, 20, 65, 52, 0, 20, 100, 25, 10, 50, 2, 0, 0, 0, 50}},
    /* F4 every register, the deep ensemble, a room of echo */
    {"FULL ENS", {90, 100, 80, 10, 120, 0, 0, 20, 78, 64, 100, 90, 98, 0, 0, 0, 70, 50, 0, 20, 110, 35, 15, 55, 3, 0, 0, 0, 50}},
    /* F5 saw + square, low cutoff with resonance, slow in and out, dark echoes */
    {"DARK PAD", {100, 80, 0, 40, 72, 25, 15, 0, 100, 90, 85, 100, 30, 0, 0, 0, 80, 40, 0, 20, 115, 45, 20, 35, 3, 0, 0, 0, 50}},
    /* F6 8' + 4' wide open, quick, ensemble I */
    {"BRIGHT", {40, 100, 100, 0, 127, 10, 0, 50, 55, 64, 100, 70, 98, 0, 0, 0, 55, 52, 0, 20, 90, 30, 0, 60, 1, 0, 0, 0, 50}},
    /* F7 a long attack opening the filter with it, late vibrato */
    {"SWELL", {80, 100, 50, 0, 50, 30, 70, 20, 112, 100, 100, 100, 96, 12, 0, 50, 60, 52, 0, 20, 105, 40, 20, 50, 2, 0, 0, 0, 50}},
    /* F8 a pulse with slow PWM through the flanger and long echoes */
    {"SPACE", {60, 100, 60, 75, 110, 10, 0, 0, 88, 64, 100, 110, 35, 0, 40, 0, 70, 40, 45, 15, 120, 70, 45, 45, 4, 0, 0, 0, 50}},
    /* F9 a brass section: the filter envelope bites and falls back, a little vibrato, ensemble II (by ear) */
    {"BRASS", {0, 100, 40, 0, 60, 20, 55, 40, 60, 85, 70, 70, 98, 8, 0, 12, 40, 52, 0, 20, 90, 30, 0, 60, 2, 0, 0, 0, 50}},
    /* F10 squares in all registers (16' 60, 8' and 4' 80: balanced by ear), instant on and off, a fast ensemble */
    {"ORGAN", {60, 80, 80, 66, 115, 0, 0, 0, 10, 0, 100, 30, 104, 0, 0, 0, 50, 70, 0, 20, 90, 30, 0, 60, 2, 0, 0, 0, 50}},
    /* F11 8' alone, a square with deep slow PWM fading in after 120 ms: hollow and moving (by ear) */
    {"HOLLOW", {0, 100, 0, 66, 95, 15, 0, 20, 90, 64, 100, 95, 45, 0, 90, 3, 50, 45, 0, 20, 100, 30, 15, 40, 1, 0, 0, 0, 50}},
    /* F12 a narrow pulse, 4' leading, a slow 585 ms attack, 717 Hz with strong resonance tracking the keys (by ear) */
    {"GLASS", {0, 60, 100, 85, 68, 80, 10, 80, 95, 64, 100, 100, 60, 0, 30, 0, 50, 52, 0, 20, 90, 50, 30, 70, 2, 0, 0, 0, 50}},
    /* F13 16' first, rounded, for the left hand: instant attack, a 21 ms decay to 70 % sustain: a punch (by ear) */
    {"BASSES", {100, 50, 0, 0, 75, 10, 0, 60, 0, 26, 70, 80, 98, 10, 0, 15, 45, 52, 0, 20, 90, 30, 0, 60, 1, 0, 0, 0, 50}},
    /* F14 no sustain: the filter envelope plucks, the echo carries it */
    {"PLUCK", {30, 100, 60, 0, 55, 30, 75, 40, 0, 75, 0, 75, 98, 0, 0, 0, 50, 52, 0, 20, 95, 35, 25, 50, 2, 0, 0, 0, 50}},
    /* F15 resonance at a vowel-like cutoff, the deep ensemble, late vibrato */
    {"VOX", {40, 100, 30, 25, 88, 35, 0, 30, 92, 64, 100, 95, 100, 10, 0, 25, 75, 48, 0, 20, 115, 35, 15, 40, 3, 0, 0, 0, 50}},
    /* F16 a plain voice into a dark, long tape echo */
    {"ECHOES", {0, 100, 40, 0, 100, 0, 0, 30, 60, 80, 60, 85, 98, 0, 0, 0, 40, 52, 0, 20, 108, 65, 55, 30, 1, 0, 0, 0, 50}},
};

/* INIT (SAVE held; a slot cleared): one 8' saw, the filter open, no LFO, no effects, a quick envelope */
static const preset_t INIT_SOUND =
    {"INIT", {0, 100, 0, 0, 127, 0, 0, 0, 20, 64, 100, 50, 64, 0, 0, 0, 60, 52, 0, 20, 90, 30, 0, 60, 0, 0, 0, 0, 50}};

static preset_t cur;             /* the sound being played (edited by the pages; SAVE stores it) */
static uint32_t snd_epoch;       /* bumped when the whole sound is replaced (a preset, a revert, INIT, a dump): MIDI
                                  * out then sends a Program Change, not every parameter */
static uint8_t cur_index;        /* which preset it came from: 0..15 F1..F16, 16..47 U01..U32, PRESET_NEW none */
static preset_t user[NUSER];     /* the user slots as they are in the store */

static void preset_init(preset_t *p) { *p = INIT_SOUND; }   /* an empty user slot holds INIT */

static void preset_check(preset_t *p)         /* never trust what was stored: ranges, the name's characters */
{
    uint32_t i, end = 0;
    for (i = 0; i < NSP; i++)
        p->p[i] = clamp8(p->p[i], &SP[i]);
    for (i = NSP; i < NSP_STORED; i++)
        p->p[i] = 0;
    for (i = 0; i < NSP_STORED; i++)             /* the fine parts: only the 14-bit ones, < 128, none at the top */
        if (!hires(i) || i >= NSP || p->p[i] >= SP[i].hi)
            p->f[i] = 0;
        else if (p->f[i] > 127u)
            p->f[i] = 127;
    for (i = 0; i < NAME_LEN && !end; i++) {
        if (!p->name[i])
            end = 1;
        else if (p->name[i] < 32 || p->name[i] > 126)
            p->name[i] = '?';
    }
    p->name[NAME_LEN - 1] = 0;
    if (!p->name[0])
        str_cpy(p->name, "INIT", NAME_LEN);
}

static void preset_label(char *b, unsigned max, uint32_t index)   /* "F1", "F16", "U07", "NEW" */
{
    char t[4];
    if (index >= NPRESETS) {
        str_cpy(b, "NEW", max);
        return;
    }
    if (index < NFACT) {
        str_cpy(b, "F", max);
        fmt_int(t, (int32_t)index + 1);
    } else {
        str_cpy(b, index < NFACT + 9u ? "U0" : "U", max);
        fmt_int(t, (int32_t)(index - NFACT) + 1);
    }
    cat(b, t, max);
}

static const preset_t *preset_at(uint32_t index)
{
    return index < NFACT ? &FACTORY[index] : index < NPRESETS ? &user[index - NFACT] : &INIT_SOUND;
}

/* the sound differs from the preset it came from (HOME's asterisk; HOME held reverts) */
static int preset_edited(void)
{
    const preset_t *p = preset_at(cur_index);
    return cur_index < NPRESETS && (memcmp(cur.p, p->p, sizeof cur.p) || memcmp(cur.f, p->f, sizeof cur.f) ||
                                    !str_eq(cur.name, p->name));
}

/* the stored code of a preset (settings' last_preset): stable when the factory count changes. Factory f: f; user u:
 * 64 + u. (Version 1 stored the plain index with 8 factory sounds: 0..7, 8..39.) */
#define CODE_USER 64u
static uint8_t preset_code(uint32_t index)
{
    return (uint8_t)(index < NFACT ? index : index < NPRESETS ? CODE_USER + index - NFACT : 0u);
}
static uint32_t preset_of_code(uint32_t code)
{
    return code < NFACT ? code : code >= CODE_USER && code < CODE_USER + NUSER ? NFACT + code - CODE_USER : 0u;
}

/* ------------------------------------------------------------ settings --- */
#define NCHORDS 8
#define CHORD_MAX 8
static struct {
    int8_t g[NG];                /* GP order */
    uint8_t last_preset;         /* loaded at power-on: its code (preset_code) */
    int8_t oct;                  /* OCT- / OCT+: -2 .. +2 */
    uint8_t trs_midi;            /* TRS MIDI IN on (1) / off (0) */
    uint8_t bpm;                 /* the arp's internal tempo, 40 .. 240 */
    uint8_t chord_n[NCHORDS];    /* notes in each stored chord (0: none) */
    int8_t chord[NCHORDS][CHORD_MAX];   /* semitones above the chord's lowest note */
    int8_t g2[NG2];              /* (version 2, appended) GLOBAL 2: velocity, aftertouch, its depth, CC out */
} set;

static void settings_init(void)
{
    uint32_t i;
    memset(&set, 0, sizeof set);
    for (i = 0; i < NG; i++)
        set.g[i] = GP[i].def;
    set.trs_midi = 1;
    set.bpm = 120;
    for (i = 0; i < NG2; i++)
        set.g2[i] = GP2[i].def;
}

static void settings_check(void)
{
    uint32_t i, k;
    for (i = 0; i < NG; i++)
        set.g[i] = clamp8(set.g[i], &GP[i]);
    for (i = 0; i < NG2; i++)
        set.g2[i] = clamp8(set.g2[i], &GP2[i]);
    set.last_preset = preset_code(preset_of_code(set.last_preset));   /* (anything else: F1) */
    if (set.oct < -2 || set.oct > 2)
        set.oct = 0;
    set.trs_midi = set.trs_midi ? 1u : 0u;
    if (set.bpm < 40u || set.bpm > 240u)
        set.bpm = 120;
    for (i = 0; i < NCHORDS; i++) {
        if (set.chord_n[i] > CHORD_MAX)
            set.chord_n[i] = 0;
        for (k = 0; k < CHORD_MAX; k++)
            if (set.chord[i][k] < 0 || set.chord[i][k] > 48)
                set.chord[i][k] = 0;
    }
}

/* ------------------------------------------------------------ records --- */
/* slot 0: the settings; slots 1 and 2: U01..U16 and U17..U32. Each starts with a small header so a later version can
 * tell what an older record holds (append only: new fields at the end, the length says what is there). */
#define REC_SETTINGS 0u
#define REC_USER0 1u
#define USER_PER_REC 16u
#define DATA_VER 3u              /* the user presets' records: 3 holds 32 parameters and their fine parts (76 bytes);
                                  * 2: 28 and 28 (68), 1: 28 parameters, no fine parts (40) */
#define PRESET_V1 40u
#define PRESET_V2 68u
#define NSP_V12 28u              /* the parameters a version 1 or 2 preset holds */
#define SET_VER 2u               /* the settings' record: 2 stores last_preset as a code (1: a plain index) */
typedef struct {
    uint16_t ver, size;          /* DATA_VER; sizeof the payload that follows */
} rec_head_t;
static struct {
    rec_head_t h;
    preset_t p[USER_PER_REC];
} rec_user;
static struct {
    rec_head_t h;
    uint8_t s[sizeof set];
} rec_set;

static int32_t data_save_settings(void)
{
    rec_set.h.ver = SET_VER;
    rec_set.h.size = sizeof set;
    memcpy(rec_set.s, &set, sizeof set);
    return store_save(REC_SETTINGS, APP_ID, &rec_set, sizeof rec_set);
}

static int32_t data_save_user(uint32_t slot)     /* slot 0..31: its record of 16 presets */
{
    uint32_t r = slot / USER_PER_REC;
    rec_user.h.ver = DATA_VER;
    rec_user.h.size = sizeof rec_user.p;
    memcpy(rec_user.p, &user[r * USER_PER_REC], sizeof rec_user.p);
    return store_save(REC_USER0 + r, APP_ID, &rec_user, sizeof rec_user);
}

static void data_load(void)                      /* defaults first, then what was stored over them, checked */
{
    uint32_t r, i;
    int32_t n;
    settings_init();
    memset(&rec_set, 0, sizeof rec_set);
    n = store_load(REC_SETTINGS, APP_ID, &rec_set, sizeof rec_set);
    if (n >= (int32_t)sizeof(rec_head_t) && rec_set.h.ver >= 1u) {
        uint32_t m = rec_set.h.size < sizeof set ? rec_set.h.size : sizeof set;
        if ((uint32_t)n - sizeof(rec_head_t) < m)
            m = (uint32_t)n - sizeof(rec_head_t);
        memcpy(&set, rec_set.s, m);              /* a shorter (older) record leaves the newer fields at defaults */
        if (rec_set.h.ver == 1u)                 /* version 1: 8 factory sounds, the plain index */
            set.last_preset = set.last_preset < 8u ? set.last_preset
                              : set.last_preset < 40u ? (uint8_t)(CODE_USER + set.last_preset - 8u) : 0u;
    }
    settings_check();
    for (i = 0; i < NUSER; i++)
        preset_init(&user[i]);
    for (r = 0; r < NUSER / USER_PER_REC; r++) {
        memset(&rec_user, 0, sizeof rec_user);
        n = store_load(REC_USER0 + r, APP_ID, &rec_user, sizeof rec_user);
        if (n >= (int32_t)sizeof(rec_head_t) && rec_user.h.ver >= 1u) {
            /* older versions held 28 parameters (and, from 2, their fine parts): read field by field, the parameters
             * they did not have at their defaults */
            uint32_t ver = rec_user.h.ver, psz = ver == 1u ? PRESET_V1 : ver == 2u ? PRESET_V2 : sizeof(preset_t);
            uint32_t have = ((uint32_t)n - sizeof(rec_head_t)) / psz, k;
            const uint8_t *raw = (const uint8_t *)&rec_user + sizeof(rec_head_t);
            for (i = 0; i < have && i < USER_PER_REC; i++) {
                preset_t q;
                const uint8_t *e = raw + i * psz;
                memset(&q, 0, sizeof q);
                if (ver >= 3u)
                    memcpy(&q, e, sizeof q);
                else {
                    memcpy(q.name, e, NAME_LEN);
                    for (k = 0; k < NSP_V12; k++) {
                        q.p[k] = (int8_t)e[NAME_LEN + k];
                        q.f[k] = ver == 2u ? e[NAME_LEN + NSP_V12 + k] : 0u;
                    }
                    for (k = NSP_V12; k < NSP; k++)
                        q.p[k] = SP[k].def;
                }
                preset_check(&q);
                user[r * USER_PER_REC + i] = q;
            }
        }
    }
    cur_index = (uint8_t)preset_of_code(set.last_preset);
    cur = *preset_at(cur_index);
}
