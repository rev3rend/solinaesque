/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 R. Luke DuBois */
/* Solinaesque's data: the parameters (what each knob edits, its range and how it reads), the sound being played
 * (cur), the global settings (set), the 8 factory sounds and the 32 user presets, and their records in the store
 * (store.c, docs/SPEC.md "What is stored"). Main loop only: the sound engine (sound.c) never reads these directly,
 * it gets the derived numbers through snd_update(). Every stored value is checked against PARAMS on load. */

/* ------------------------------------------------------------ parameters --- */
/* a parameter's kind: how its stored int8 reads on the screen and what it means to the engine */
enum { K_PCT, K_BIP, K_ATK, K_DEC, K_REL, K_HZ, K_CUT, K_DLY, K_MS, K_SHAPE, K_TRIG, K_TUNE, K_CH, K_ARPM, K_ARPR,
       K_OCT };

typedef struct {
    const char *name;
    uint8_t kind;
    int8_t lo, hi, def;
    uint8_t live;                /* 1: the engine uses it already; 0: drawn with SOON (a later milestone) */
} param_t;

/* the sound's parameters (a preset): pages STRINGS, FILTER, ENVELOPE, LFO, FX 1, FX 2, then the ensemble type on
 * ALGORITHM. Stored by index: append only, never reorder (the store's records name them by position). */
enum { P_R16, P_R8, P_R4, P_SHAPE, P_CUT, P_RES, P_FENV, P_KTRK, P_ATK, P_DEC, P_SUS, P_REL, P_LRATE, P_VIB, P_PWM,
       P_LDLY, P_ENSD, P_ENSR, P_FLGA, P_FLGR, P_DTIME, P_DFB, P_DMIX, P_DTONE, P_ENS, NSP };
#define NSP_STORED 28            /* room in a stored preset for parameters added later */
static const param_t SP[NSP] = {
    {"16'", K_PCT, 0, 100, 70, 1},     {"8'", K_PCT, 0, 100, 100, 1},    {"4'", K_PCT, 0, 100, 40, 1},
    {"SHAPE", K_SHAPE, 0, 99, 0, 1},   {"CUTOFF", K_CUT, 0, 127, 127, 1}, {"RESONANCE", K_PCT, 0, 100, 0, 1},
    {"ENV AMOUNT", K_BIP, -100, 100, 0, 1}, {"KEY TRACK", K_PCT, 0, 100, 0, 1},
    {"ATTACK", K_ATK, 0, 127, 70, 1},  {"DECAY", K_DEC, 0, 127, 64, 1},  {"SUSTAIN", K_PCT, 0, 100, 100, 1},
    {"RELEASE", K_REL, 0, 127, 70, 1},
    {"RATE", K_HZ, 0, 127, 64, 0},     {"VIBRATO", K_PCT, 0, 100, 0, 0}, {"PWM", K_PCT, 0, 100, 0, 0},
    {"DELAY", K_MS, 0, 125, 0, 0},
    {"ENS DEPTH", K_PCT, 0, 100, 60, 1}, {"ENS RATE", K_HZ, 0, 127, 40, 1}, {"FLANGER", K_PCT, 0, 100, 0, 1},
    {"FLG RATE", K_HZ, 0, 127, 20, 1},
    {"DLY TIME", K_DLY, 0, 127, 90, 1}, {"FEEDBACK", K_PCT, 0, 95, 30, 1}, {"DLY MIX", K_PCT, 0, 100, 0, 1},
    {"DLY TONE", K_PCT, 0, 100, 60, 1},
    {"ENSEMBLE", K_PCT, 0, 4, 2, 1},
};
static const char *const ENS_NAME[5] = {"OFF", "I", "II", "III", "FLANGE"};

/* the global settings on the GLOBAL and ARP pages (stored in the settings record, not in a preset) */
enum { G_TRIG, G_TUNE, G_MIDICH, G_VOL, G_ARPM, G_ARPR, G_ARPO, G_ARPG, NG };
static const param_t GP[NG] = {
    {"TRIGGER", K_TRIG, 0, 1, 0, 1},   {"TUNE", K_TUNE, -100, 100, 0, 1}, {"MIDI CH", K_CH, 0, 16, 0, 1},
    {"VOLUME", K_PCT, 0, 100, 80, 1},
    {"MODE", K_ARPM, 0, 4, 0, 0},      {"RATE", K_ARPR, 0, 5, 3, 0},      {"OCTAVES", K_OCT, 1, 4, 1, 0},
    {"GATE", K_PCT, 10, 100, 50, 0},
};

/* pages: four parameters each, KNOB 1..4 top to bottom; 0..5 the sound's, 6..7 the settings' */
enum { PG_STR, PG_FLT, PG_ENV, PG_LFO, PG_FX1, PG_FX2, PG_ARP, PG_GLO, NPAGES, PG_HOME = NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"STRINGS", "FILTER", "ENVELOPE", "LFO", "FX 1", "FX 2", "ARP",
                                              "GLOBAL"};

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

static const char *const SHAPE_NAME[3] = {"SAW", "SAW+SQR", "SQUARE"};
static const char *const TRIG_NAME[2] = {"RETRIG", "LEGATO"};
static const char *const ARPM_NAME[5] = {"UP", "DOWN", "UP-DOWN", "RANDOM", "PLAYED"};
static const char *const ARPR_NAME[6] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};

static void cat(char *d, const char *s, unsigned max) { str_cpy(d + str_len(d), s, max - str_len(d)); }

/* the text a parameter's value shows: "100%", "250 ms", "1.20 s", "2.5 kHz", "RETRIG", "+12 ct" */
static void pv_text(char *b, unsigned max, const param_t *p, int32_t x)
{
    uint32_t v = pv_ms(p->kind, x);
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
} preset_t;

#define NFACT 8
#define NUSER 32
#define NPRESETS (NFACT + NUSER) /* PRESETS browses F1..F8 then U01..U32 */
/* the factory sounds: 16' 8' 4' shape | cutoff res env ktrk | A D S R | lfo: rate vib pwm delay |
 * ens depth rate, flanger amount rate | delay time fb mix tone | ensemble type (by ear in the emulator later) */
static const preset_t FACTORY[NFACT] = {
    {"CLASSIC", {70, 100, 40, 0, 127, 0, 0, 0, 70, 64, 100, 70, 64, 0, 0, 0, 60, 40, 0, 20, 90, 30, 0, 60, 2}},
    {"CELLO", {100, 60, 0, 0, 90, 10, 20, 30, 80, 64, 90, 75, 60, 12, 0, 50, 50, 36, 0, 20, 90, 30, 0, 60, 1}},
    {"VIOLINS", {0, 100, 70, 0, 120, 0, 0, 0, 72, 64, 100, 72, 66, 15, 0, 60, 70, 44, 0, 20, 90, 30, 10, 60, 2}},
    {"FULL ENS", {90, 100, 80, 10, 127, 0, 0, 0, 74, 64, 100, 80, 64, 0, 0, 0, 80, 40, 0, 20, 100, 35, 15, 55, 3}},
    {"DARK PAD", {100, 80, 0, 0, 70, 20, 10, 0, 95, 80, 85, 95, 50, 0, 20, 0, 70, 30, 0, 20, 110, 40, 20, 40, 3}},
    {"BRIGHT", {40, 100, 100, 30, 127, 15, 0, 40, 50, 64, 100, 60, 64, 0, 0, 0, 50, 50, 0, 20, 80, 25, 0, 70, 1}},
    {"SWELL", {80, 100, 50, 0, 100, 10, 40, 0, 110, 90, 100, 100, 64, 10, 0, 90, 60, 40, 0, 20, 100, 40, 20, 50, 2}},
    {"SPACE", {60, 100, 60, 75, 110, 10, 0, 0, 88, 64, 100, 110, 40, 0, 40, 0, 80, 30, 30, 10, 120, 70, 45, 45, 4}},
};

static preset_t cur;             /* the sound being played (edited by the pages; SAVE stores it) */
static uint8_t cur_index;        /* which preset it came from: 0..7 F1..F8, 8..39 U01..U32 */
static preset_t user[NUSER];     /* the user slots as they are in the store */

static void preset_init(preset_t *p)          /* INIT: every parameter at its default */
{
    uint32_t i;
    memset(p, 0, sizeof *p);
    str_cpy(p->name, "INIT", NAME_LEN);
    for (i = 0; i < NSP; i++)
        p->p[i] = SP[i].def;
}

static void preset_check(preset_t *p)         /* never trust what was stored: ranges, the name's characters */
{
    uint32_t i, end = 0;
    for (i = 0; i < NSP; i++)
        p->p[i] = clamp8(p->p[i], &SP[i]);
    for (i = NSP; i < NSP_STORED; i++)
        p->p[i] = 0;
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

static void preset_label(char *b, unsigned max, uint32_t index)   /* "F1", "U07" */
{
    char t[4];
    if (index < NFACT) {
        str_cpy(b, "F", max);
        fmt_int(t, (int32_t)index + 1);
    } else {
        str_cpy(b, index < NFACT + 9u ? "U0" : "U", max);
        fmt_int(t, (int32_t)(index - NFACT) + 1);
    }
    cat(b, t, max);
}

static const preset_t *preset_at(uint32_t index) { return index < NFACT ? &FACTORY[index] : &user[index - NFACT]; }

/* ------------------------------------------------------------ settings --- */
#define NCHORDS 8
#define CHORD_MAX 8
static struct {
    int8_t g[NG];                /* GP order */
    uint8_t last_preset;         /* loaded at power-on */
    int8_t oct;                  /* OCT- / OCT+: -2 .. +2 */
    uint8_t trs_midi;            /* TRS MIDI IN on (1) / off (0) */
    uint8_t bpm;                 /* the arp's internal tempo, 40 .. 240 */
    uint8_t chord_n[NCHORDS];    /* notes in each stored chord (0: none) */
    int8_t chord[NCHORDS][CHORD_MAX];   /* semitones above the chord's lowest note */
} set;

static void settings_init(void)
{
    uint32_t i;
    memset(&set, 0, sizeof set);
    for (i = 0; i < NG; i++)
        set.g[i] = GP[i].def;
    set.trs_midi = 1;
    set.bpm = 120;
}

static void settings_check(void)
{
    uint32_t i, k;
    for (i = 0; i < NG; i++)
        set.g[i] = clamp8(set.g[i], &GP[i]);
    if (set.last_preset >= NPRESETS)
        set.last_preset = 0;
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
#define DATA_VER 1u
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
    rec_set.h.ver = DATA_VER;
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
    }
    settings_check();
    for (i = 0; i < NUSER; i++)
        preset_init(&user[i]);
    for (r = 0; r < NUSER / USER_PER_REC; r++) {
        memset(&rec_user, 0, sizeof rec_user);
        n = store_load(REC_USER0 + r, APP_ID, &rec_user, sizeof rec_user);
        if (n >= (int32_t)sizeof(rec_head_t) && rec_user.h.ver >= 1u && rec_user.h.size >= sizeof(preset_t)) {
            uint32_t have = ((uint32_t)n - sizeof(rec_head_t)) / sizeof(preset_t);
            for (i = 0; i < have && i < USER_PER_REC; i++) {
                user[r * USER_PER_REC + i] = rec_user.p[i];
                preset_check(&user[r * USER_PER_REC + i]);
            }
        }
    }
    cur_index = set.last_preset;
    cur = *preset_at(cur_index);
}
