/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 R. Luke DuBois */
/* The MIDItoSpeech sidecar (IDMNYU/synthaccess, MIDItoSpeech/devices, a JSON file) for what Solinaesque SENDS over
 * USB-MIDI: every control the panel sends, with the simplest of MIDItoSpeech's own value modes that fits it, generated
 * from the firmware's tables so it never drifts from them:
 *   percent        a 0..100 % control sent as a 7-bit CC
 *   intmap         other linear ranges, with their unit (env amount, tune, tempo, LFO delay; the 14-bit percentages)
 *   enumsplit      named choices (shape, modes, note values, octaves): the words the FM-1's screen reader says
 *   offon          ARP and HOLD
 *   value          the exponential ones (cutoff, attack, decay, release, the rates, delay time): the raw MIDI value
 * The 14-bit filter / envelope / effect parameters are NRPN 0:<CC number> ("hires": "true", 0..16383). Program Change
 * names the presets. No transport: MIDItoSpeech does not need it (PLAY's CC 109, clock, start and stop left out).
 *   cc -O1 -Ibuild/gen -Ifirmware/src -Ifirmware/app -Itests -o build/host/gen_synthaccess tools/gen_synthaccess.c -lm
 *   build/host/gen_synthaccess > docs/synthaccess/Solinaesque.json   (tests/synthaccess_check.py checks it)
 *   build/host/gen_synthaccess --pairs   every control at every step: what the FM-1 sends, what it means */
#include "host_platform.h"

enum { M_PERCENT, M_INTMAP, M_ENUM, M_OFFON, M_VALUE };

static void put_str(const char *s)               /* a JSON string (the words are ASCII, no quotes or backslashes) */
{
    putchar('"');
    for (; *s; s++)
        putchar(*s == '"' || *s == '\\' ? '\'' : *s);
    putchar('"');
}

static const param_t *cc_param(uint32_t k)       /* a CC map entry's parameter (0: the panel's own states) */
{
    uint32_t t = CC_TGT[k];
    return t < CT_G ? &SP[t] : t < CT_G2 ? &GP[t - CT_G] : t < CT_TEMPO ? &GP2[t - CT_G2] : 0;
}

static int sidecar_cc(uint32_t k)                /* listed as a CC: sent as CC (not the 14-bit ones), not transport */
{
    uint32_t t = CC_TGT[k];
    return !(t < CT_G && hires(t)) && t != CT_PLAY;
}

/* the mode for a control; for intmap its output range and unit */
static int mode_of(uint32_t t, const param_t *p, int32_t *lo, int32_t *hi, const char **unit)
{
    *unit = 0;
    if (!p) {
        if (t == CT_TEMPO) {
            *lo = 40, *hi = 240, *unit = "beats per minute";
            return M_INTMAP;
        }
        return t == CT_OCT ? M_ENUM : M_OFFON;
    }
    *lo = p->lo, *hi = p->hi;
    switch (p->kind) {
    case K_PCT:
        if (t == P_ENS)
            return M_ENUM;
        *unit = "percent";
        return p->lo == 0 && p->hi == 100 && !(t < CT_G && hires(t)) ? M_PERCENT : M_INTMAP;
    case K_BIP: *unit = "percent"; return M_INTMAP;
    case K_TUNE: *unit = "cents"; return M_INTMAP;
    case K_MS: *lo = p->lo * 40, *hi = p->hi * 40, *unit = "milliseconds"; return M_INTMAP;
    case K_HZ: case K_ATK: case K_DEC: case K_REL: case K_CUT: case K_DLY: return M_VALUE;
    }
    return M_ENUM;                               /* the named kinds */
}

/* a CC value's words (cc_set: the firmware's own reading), spoken as the screen reader says them */
static void words_of(char *d, uint32_t t, uint32_t k, uint32_t v)
{
    char b[48];
    const param_t *p = cc_param(k);
    d[0] = 0;
    if (p) {
        int8_t *val = t < CT_G ? &cur.p[t] : t < CT_G2 ? &set.g[t - CT_G] : &set.g2[t - CT_G2], keep = *val;
        uint8_t keepf = t < CT_G ? cur.f[t] : 0;
        int32_t x;
        cc_set(k, (int32_t)v);
        x = *val;
        *val = keep;
        if (t < CT_G)
            cur.f[t] = keepf;
        if (t == P_ENS)
            str_cpy(b, ENS_NAME[x], sizeof b);
        else
            pv_text(b, sizeof b, p, x);
        if (p->kind == K_SHAPE && !strncmp(b, "PULSE", 5))
            b[5] = 0;                            /* (shape: the four names; the pulse's width left out) */
    } else {                                     /* CT_OCT */
        int32_t o = (int32_t)v * 5 / 128 - 2;
        fmt_int(b, o);
        if (o > 0) {
            char c[8] = "+";
            cat(c, b, sizeof c);
            str_cpy(b, c, sizeof b);
        }
    }
    spk(d, 48, b);
}

/* the label spoken: the screen's name, spelled out where a page gives it its meaning (MIDItoSpeech speaks the label
 * alone: "rate" could be the LFO's or the arp's, "delay" is the LFO's fade-in, not the echo) */
static void label_of(char *d, unsigned max, uint32_t t, const param_t *p)
{
    static const char *const CT[5] = {"tempo", "arpeggiator", "hold", "play", "octave shift"};
    static const struct { uint32_t t; const char *name; } FULL[] = {
        {P_LRATE, "L F O rate"},           {P_VIB, "vibrato"},              {P_PWM, "pulse width modulation"},
        {P_LDLY, "L F O delay"},           {P_ENS, "ensemble type"},        {CT_G + G_TRIG, "trigger mode"},
        {CT_G + G_ARPM, "arpeggiator mode"}, {CT_G + G_ARPR, "arpeggiator rate"}, {CT_G + G_ARPO, "arpeggiator octaves"},
        {CT_G + G_ARPG, "arpeggiator gate"}, {CT_G2 + G2_VEL, "velocity mode"}, {CT_G2 + G2_AT, "aftertouch mode"},
        {CT_G2 + G2_ATD, "aftertouch depth"}, {P_FENV, "filter envelope amount"}, {P_KTRK, "filter key tracking"},
        {P_RES, "filter resonance"},       {P_SUS, "sustain"},              {P_SHAPE, "oscillator shape"},
        {P_CUT, "filter cutoff"},          {P_DFB, "delay feedback"},       {P_FLGA, "flanger amount"},
    };
    uint32_t i;
    d[0] = 0;
    for (i = 0; i < sizeof FULL / sizeof FULL[0]; i++)
        if (FULL[i].t == t) {
            str_cpy(d, FULL[i].name, max);
            return;
        }
    if (p)
        spk(d, max, p->name);
    else
        str_cpy(d, CT[t - CT_TEMPO], max);
}

static void entry(const char *key, uint32_t t, uint32_t k, int hr, int last)
{
    const param_t *p = hr ? &SP[t] : cc_param(k);
    int32_t lo, hi;
    const char *unit;
    int m = mode_of(t, p, &lo, &hi, &unit);
    char label[64];
    label_of(label, sizeof label, t, p);
    printf("      \"%s\": {\n        \"label\": ", key);
    put_str(label);
    if (hr)
        printf(",\n        \"hires\": \"true\"");
    switch (m) {
    case M_PERCENT: printf(",\n        \"data\": \"percent\""); break;
    case M_OFFON: printf(",\n        \"data\": \"offon\""); break;
    case M_VALUE: printf(",\n        \"data\": \"value\""); break;
    case M_INTMAP:
        printf(",\n        \"data\": \"intmap\",\n        \"map\": [0, %u, %d, %d],\n        \"clamp\": \"true\"",
               hr ? 16383u : 127u, lo, hi);
        if (unit)
            printf(",\n        \"suffix\": \"%s\"", unit);
        break;
    case M_ENUM: {                               /* the words, and the first value of each */
        static char texts[128][48];
        uint32_t split[128], n = 0, v, i;
        char w[48];
        for (v = 0; v <= 127u; v++) {
            words_of(w, t, k, v);
            if (!n || strcmp(w, texts[n - 1u])) {
                str_cpy(texts[n], w, sizeof texts[0]);
                split[n++] = v;
            }
        }
        printf(",\n        \"data\": \"enumsplit\",\n        \"split\": [");
        for (i = 0; i < n; i++)
            printf("%s%u", i ? ", " : "", split[i]);
        printf("],\n        \"enum\": [");
        for (i = 0; i < n; i++) {
            printf("%s", i ? ", " : "");
            put_str(texts[i]);
        }
        printf("]");
        break;
    }
    }
    printf("\n      }%s\n", last ? "" : ",");
}

/* --pairs: every control at every step: "CC|NRPN <key> <sent> <mode> <what the firmware means>": words for the named
 * ones, the number in the entry's unit for percent / intmap, the words for value (the check only needs the raw value) */
static void pairs(void)
{
    static const char *const MODE[5] = {"percent", "intmap", "enum", "offon", "value"};
    uint32_t k, h;
    char w[48];
    for (k = 0; k < NCC; k++) {
        uint32_t t = CC_TGT[k];
        const param_t *p = cc_param(k);
        int32_t lo, hi, x;
        const char *unit;
        int m;
        if (!sidecar_cc(k))
            continue;
        m = mode_of(t, p, &lo, &hi, &unit);
        if (!p) {                                /* tempo, arp, hold, octave: every value it can send */
            uint32_t v;
            for (v = 0; v <= 127u; v++) {
                if (m == M_INTMAP)
                    printf("CC %u %u %s %d\n", CC_NUM[k], v, MODE[m], 40 + (int32_t)v * 200 / 127);
                else if (m == M_OFFON)
                    printf("CC %u %u %s %s\n", CC_NUM[k], v, MODE[m], v ? "on" : "off");
                else {
                    words_of(w, t, k, v);
                    printf("CC %u %u %s %s\n", CC_NUM[k], v, MODE[m], w);
                }
            }
            continue;
        }
        for (x = p->lo; x <= p->hi; x++) {
            int8_t *val = t < CT_G ? &cur.p[t] : t < CT_G2 ? &set.g[t - CT_G] : &set.g2[t - CT_G2], keep = *val;
            int32_t sent;
            *val = (int8_t)x;
            sent = cc_get(k);
            *val = keep;
            if (m == M_ENUM || m == M_VALUE) {
                words_of(w, t, k, (uint32_t)sent);
                printf("CC %u %d %s %s\n", CC_NUM[k], sent, MODE[m], w);
            } else
                printf("CC %u %d %s %d\n", CC_NUM[k], sent, MODE[m], p->kind == K_MS ? x * 40 : x);
        }
    }
    for (h = 0; h < NHIRES; h++) {
        uint32_t i = HIRES[h];
        int32_t lo, hi, x;
        const char *unit;
        int m = mode_of(i, &SP[i], &lo, &hi, &unit);
        for (x = SP[i].lo; x <= SP[i].hi; x++) {
            int8_t keep = cur.p[i];
            cur.p[i] = (int8_t)x, cur.f[i] = 0;
            printf("NRPN %u %u %s %d\n", CC_NUM_OF(i), hr_get(h), MODE[m], x);
            cur.p[i] = keep;
        }
    }
}

int main(int argc, char **argv)
{
    char b[32], d[48];
    uint32_t k, h, n_cc = 0, i;
    host_boot();
    if (argc > 1 && !strcmp(argv[1], "--pairs")) {
        pairs();
        return 0;
    }
    printf("{\n  \"device\": {\n");
    printf("    \"name\": \"Solinaesque\",\n    \"manufacturer\": \"R. Luke DuBois (custom firmware for the M-VAVE FM-1)\",\n");
    printf("    \"language\": \"en-US\",\n    \"version\": 1,\n");
    printf("    \"program_change\": {\n      \"0\": {\n        \"label\": \"preset\",\n        \"data\": \"enum\",\n"
           "        \"enum\": \"presets\"\n      }\n    },\n");
    printf("    \"CC\": {\n");
    for (k = 0; k < NCC; k++)
        n_cc += (uint32_t)sidecar_cc(k);
    for (k = 0, i = 0; k < NCC; k++) {
        char key[8];
        if (!sidecar_cc(k))
            continue;
        fmt_int(key, CC_NUM[k]);
        entry(key, CC_TGT[k], k, 0, ++i == n_cc);
    }
    printf("    },\n    \"NRPN\": {\n");
    for (h = 0; h < NHIRES; h++) {               /* NRPN 0:<CC number>: its key is MSB x 128 + LSB */
        char key[8];
        fmt_int(key, CC_NUM_OF(HIRES[h]));
        entry(key, HIRES[h], 0, 1, h + 1u == NHIRES);
    }
    printf("    },\n    \"presets\": [");
    for (i = 0; i < NPRESETS; i++) {             /* Program Change 0..47: F1..F16 by name, U01..U32 */
        preset_label(b, sizeof b, i);
        d[0] = 0;
        spk(d, sizeof d, b);
        if (i < NFACT) {
            cat(d, ", ", sizeof d);
            spk(d, sizeof d, FACTORY[i].name);
        }
        printf("%s", i ? ", " : "");
        put_str(d);
    }
    printf("]\n  }\n}\n");
    return 0;
}
