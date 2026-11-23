/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M-Vave-OP1 contributors
 * Copyright (C) 2026 R. Luke DuBois */
/* Host test of Solinaesque (firmware/app/) on the host platform (tests/host_platform.h): the screens (HOME, the eight
 * pages, SAVE: drawn, lazy, redrawn after the platform drew over them, the layout lint at every value), the controls
 * of docs/SPEC.md's table, the LEDs, the records in flash (a user preset, the settings, power cycles, another app's
 * data, damaged and older records, no flash) and the sound (the registers' pitches, phase-locked dividers, the shared
 * envelope in both trigger modes, the release into exact silence, HOLD, MIDI with its channel, the pedal, all notes
 * off, a lost stream, app_audio_stop, many notes at once). Screens in build/shots/. Prove each new check can fail
 * (break the code once, watch it go red).
 *   cc -O1 -Ibuild/gen -Ifirmware/src -Ifirmware/app -Itests -o build/host/app_test tests/app_test.c -lm
 *   build/host/app_test */
#include "host_platform.h"
#include <math.h>

static int failed;
static void check(const char *what, int ok)
{
    printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
    failed += !ok;
}

static void frames(uint32_t n)
{
    while (n--)
        host_frame();
}

static void tap(uint32_t label)
{
    host_press(label, 1);
    host_frame();
    host_press(label, 0);
    host_frame();
}

static void power_cycle(void)                           /* the app's RAM gone, the flash kept */
{
    memset(&ui, 0, sizeof ui);
    memset((void *)&vox, 0, sizeof vox);
    memset(&snd, 0, sizeof snd);
    memset(&cur, 0, sizeof cur);
    memset(&set, 0, sizeof set);
    memset(user, 0, sizeof user);
    cur_index = 0;
    fm1_in.notes = fm1_in.buttons = 0;
    host_boot();
    host_frame();
}

/* ---------------------------------------------------------------- screen --- */
static uint32_t ink_rows(uint32_t y0, uint32_t y1)   /* rows in y0..y1 with anything but the background */
{
    uint32_t x, y, n = 0;
    for (y = y0; y < y1; y++)
        for (x = 0; x < 240u; x++)
            if (host_fb[y * 240u + x] != T_BG) {
                n++;
                break;
            }
    return n;
}

static void lint(const char *what)
{
    char b[96];
    snprintf(b, sizeof b, "layout: %s: nothing cut or outside", what);
    check(b, host_clipped == 0u);
    if (host_clipped)
        printf("  (%u, the last: %s)\n", host_clipped, host_clip_what);
    host_clipped = 0;
}

static void screens(void)
{
    static const struct { uint32_t button; uint8_t page; const char *shot; } OPEN[] = {
        {B_EDIT, PG_STR, "build/shots/strings.png"}, {B_SCL, PG_FLT, "build/shots/filter.png"},
        {B_ENV, PG_ENV, "build/shots/envelope.png"}, {B_LFO, PG_LFO, "build/shots/lfo.png"},
        {B_FX, PG_FX1, "build/shots/fx1.png"},       {B_FX, PG_FX2, "build/shots/fx2.png"},
        {B_GLO, PG_GLO, "build/shots/global.png"},
    };
    uint32_t i, p, r, ok;
    host_boot();
    host_frame();
    check("boot: the AMBER palette (warm, dark)", ui.page == PG_HOME && T_BG == UI_PALETTES[PAL_AMBER].bg);
    check("boot: the name and the preset drawn", ink_rows(6, 66) > 30u);
    {   /* the large face is sparse (tools/gen_aa_font.py L_CHARS): a character it lacks draws as a space */
        const char *c = APP_NAME;
        int all = 1;
        for (; *c; c++)
            all &= glyph_at(&AF_L, (uint8_t)*c) >= 0;
        check("boot: every character of APP_NAME is in the large font", all);
    }
    check("boot: the drawbars drawn", ink_rows(74, 182) > 80u);
    host_png("build/shots/home.png");
    host_frame();
    check("lazy: nothing changed, nothing sent to the panel", host_blits == 0u);
    host_turn(EN_K1, -3);
    host_frame();
    check("HOME: KNOB 1 is 16', the drawbars follow", cur.p[P_R16] == 67 && host_blits == 1u);

    for (i = 0; i < sizeof OPEN / sizeof OPEN[0]; i++) {
        char b[64];
        tap(OPEN[i].button);
        snprintf(b, sizeof b, "%s opens %s", B_NAME[OPEN[i].button], PAGE_NAME[OPEN[i].page]);
        check(b, ui.page == OPEN[i].page && ink_rows(32, 210) > 60u);
        host_png(OPEN[i].shot);
    }
    tap(B_FX);
    check("FX on FX 2: back to FX 1", ui.page == PG_FX1);
    host_press(B_ARP, 1);
    frames(40);                                     /* > 500 ms */
    check("ARP held: the ARP page", ui.page == PG_ARP);
    host_png("build/shots/arp.png");
    host_press(B_ARP, 0);
    host_frame();
    check("ARP held and let go: not a tap (arp stays off)", !ui.arp);
    tap(B_HOME);
    check("HOME: back home", ui.page == PG_HOME);
    host_turn(EN_SELECT, 1);
    host_frame();
    check("SELECT from HOME: the first page", ui.page == PG_STR);
    host_turn(EN_SELECT, -1);
    host_frame();
    check("SELECT back: stays on the first page", ui.page == PG_STR);
    host_turn(EN_SELECT, 20);
    host_frame();
    check("SELECT forward: up to the last page", ui.page == PG_GLO);
    tap(B_ENV);
    host_turn(EN_K1, 5);
    host_frame();
    check("ENVELOPE: KNOB 1 is ATTACK", cur.p[P_ATK] == SP[P_ATK].def + 5);
    {   /* the values read as a person would write them */
        char b[24];
        int good = 1;
        pv_text(b, sizeof b, &SP[P_LRATE], 0);
        good &= str_eq(b, "0.05 Hz");
        pv_text(b, sizeof b, &SP[P_LRATE], 127);
        good &= str_eq(b, "20.0 Hz");
        pv_text(b, sizeof b, &SP[P_REL], 127);
        good &= str_eq(b, "9.99 s");             /* (the integer curve lands on 9 994 ms) */
        pv_text(b, sizeof b, &SP[P_CUT], 0);
        good &= str_eq(b, "20 Hz");
        pv_text(b, sizeof b, &GP[G_TUNE], 12);
        good &= str_eq(b, "+12 ct");
        pv_text(b, sizeof b, &GP[G_MIDICH], 0);
        good &= str_eq(b, "OMNI");
        check("values: 0.05 Hz, 20.0 Hz, 9.99 s, 20 Hz, +12 ct, OMNI", good);
    }

    memset(host_fb, 0, sizeof host_fb);             /* the platform drew over the screen (UBOOT countdown) */
    app_redraw = 1;
    host_frame();
    check("app_redraw: the whole screen drawn again", ink_rows(0, 30) > 10u && !app_redraw);

    /* every page at every value of every row: the layout lint stays 0 */
    ok = 1;
    for (p = 0; p < NPAGES; p++) {
        ui.page = (uint8_t)p;
        for (r = 0; r < 4u; r++) {
            int8_t *v;
            const param_t *q = row_param(p, r, &v);
            int8_t keep = *v;
            int32_t x;
            for (x = q->lo; x <= q->hi; x++) {
                *v = (int8_t)x;
                host_frame();
                if (host_clipped && ok) {
                    printf("  page %s row %u value %d: %s\n", PAGE_NAME[p], r, x, host_clip_what);
                    ok = 0;
                }
            }
            *v = keep;
        }
    }
    host_clipped = 0;
    check("layout: every page at every value: nothing cut", ok);

    /* HOME at its widest: the longest name, every flag, every note, a long message */
    str_cpy(cur.name, "WWWWWWWWWWW", NAME_LEN);
    cur_index = NPRESETS - 1;
    set.oct = -2;
    set.g[G_TRIG] = 1;
    cur.p[P_ENS] = 4;
    ui.hold = ui.arp = 1;
    for (i = 0; i < 4u; i++)
        vox.gated_pub[i] = 0xFFFFFFFFu;
    msg("CHORD MEMORY: SOON");
    audio_cpu_q8 = 255;                                 /* CPU 100 %, shedding */
    tap(B_HOME);
    host_png("build/shots/home_widest.png");
    lint("HOME at its widest");
    audio_cpu_q8 = 0;
    host_press(B_SAVE, 1);
    host_frame();
    host_press(B_SAVE, 0);
    str_cpy(user[NUSER - 1].name, "WWWWWWWWWWW", NAME_LEN);
    host_turn(EN_PRESET, 40);
    msg("SAVE CANCELLED");
    host_frame();
    host_png("build/shots/save.png");
    check("SAVE: the SAVE screen, PRESETS picks the slot", ui.saving && ui.save_slot == NUSER - 1);
    lint("SAVE at its widest");
    tap(B_HOME);
    check("SAVE: another button cancels", !ui.saving);
    host_frame();
    power_cycle();
}

/* ---------------------------------------------------------------- controls --- */
static void controls(void)
{
    host_nor_erase();
    power_cycle();
    tap(B_EDIT);
    check("LED: the page's button lit, HOME dark", host_led[panel.btn[B_EDIT]] && !host_led[panel.btn[B_HOME]]);
    {   /* the keys' LEDs show the gated notes the audio ISR publishes: render between the frames */
        static int32_t b[2u * 64u];
        fm1_in.notes = 1u;
        host_render(b, 64);
        frames(2);
        check("LED: a held key lit (id 14), the others dark", host_led[14] && !host_led[15]);
        fm1_in.notes = 0;
        host_render(b, 64);
        frames(2);
    }
    check("LED: the released key dark", !host_led[14]);

    host_turn(EN_PRESET, 1);
    host_frame();
    check("PRESETS: F2 CELLO loaded", cur_index == 1u && str_eq(cur.name, "CELLO") && cur.p[P_R16] == 100);
    host_turn(EN_PRESET, -5);
    host_frame();
    check("PRESETS: stops at F1", cur_index == 0u);
    host_turn(EN_ALGO, 1);
    host_frame();
    check("ALGORITHM: the ensemble type, shown", cur.p[P_ENS] == 3 && str_eq(ui.msg, "ENSEMBLE III"));
    host_turn(EN_ALGO, 10);
    host_frame();
    check("ALGORITHM: stops at FLANGE", cur.p[P_ENS] == 4);

    tap(B_OCTUP);
    tap(B_OCTUP);
    tap(B_OCTUP);
    check("OCT+: up to +2, the keys follow", set.oct == 2 && snd.key_base == 53u + 24u && host_led[panel.btn[B_OCTUP]]);
    tap(B_OCTDN);
    tap(B_OCTDN);
    check("OCT-: back to 0", set.oct == 0 && snd.key_base == 53u && !host_led[panel.btn[B_OCTUP]]);

    tap(B_SEQ);
    check("SEQ: HOLD on, its LED lit", ui.hold && vox.hold && host_led[panel.btn[B_SEQ]]);
    tap(B_SEQ);
    check("SEQ again: HOLD off", !ui.hold && !vox.hold);
    tap(B_ARP);
    check("ARP tapped: on, says SOON", ui.arp && str_eq(ui.msg, "ARP ON: SOON"));
    tap(B_ARP);
    check("ARP tapped again: off", !ui.arp);
    host_press(B_ARP, 1);
    host_frame();
    host_turn(EN_PRESET, 8);
    host_frame();
    check("ARP held + PRESETS: the tempo", set.bpm == 128 && cur_index == 0u);
    host_press(B_ARP, 0);
    host_frame();
    check("ARP held + PRESETS: no tap on release", !ui.arp);
    tap(B_PLAY);
    check("PLAY: says SOON", str_eq(ui.msg, "PLAY: SOON"));
    tap(B_REC);
    check("REC: says SOON", str_eq(ui.msg, "CHORD MEMORY: SOON"));
    tap(B_HOME);
}

/* ---------------------------------------------------------------- flash --- */
static void persistence(void)
{
    host_nor_erase();
    power_cycle();
    check("a fresh unit: F1 CLASSIC, the defaults", cur_index == 0u && str_eq(cur.name, "CLASSIC") &&
                                                       set.g[G_VOL] == GP[G_VOL].def && set.bpm == 120u);
    tap(B_HOME);
    host_turn(EN_K2, -30);                          /* 8' 100 -> 70 */
    host_frame();
    host_press(B_SAVE, 1);
    host_frame();
    host_press(B_SAVE, 0);
    host_turn(EN_PRESET, 2);                        /* U03 */
    host_frame();
    host_erases = 0;
    tap(B_SAVE);
    check("SAVE twice: stored in U03, SAVED shown", !ui.saving && host_erases > 0u && cur_index == NFACT + 2u &&
                                                    str_eq(ui.msg, "SAVED U03"));
    host_png("build/shots/saved.png");
    power_cycle();
    check("power-on: U03 back, as it was saved", cur_index == NFACT + 2u && cur.p[P_R8] == 70 &&
                                                 str_eq(cur.name, "CLASSIC") && str_eq(user[2].name, "CLASSIC"));
    check("power-on: the other user slots INIT", str_eq(user[0].name, "INIT") && str_eq(user[31].name, "INIT"));

    tap(B_GLO);
    host_turn(EN_K4, -30);                          /* VOLUME 80 -> 50 */
    host_frame();
    host_erases = 0;
    tap(B_HOME);
    check("GLOBAL changed and left: the settings saved", host_erases > 0u && str_eq(ui.msg, "SETTINGS SAVED"));
    host_erases = 0;
    tap(B_GLO);
    tap(B_HOME);
    check("GLOBAL left unchanged: nothing written", host_erases == 0u);
    power_cycle();
    check("power-on: VOLUME back", set.g[G_VOL] == 50);

    {   /* the unit ran another firmware since: its records in the same slots must read as empty */
        uint8_t other[64];
        memset(other, 0x55, sizeof other);
        host_nor_erase();
        store_save(REC_SETTINGS, 0x12345678u, other, sizeof other);
        store_save(REC_USER0, 0x12345678u, other, sizeof other);
        power_cycle();
        check("another app's records: ignored (the defaults)",
              cur_index == 0u && set.g[G_VOL] == GP[G_VOL].def && str_eq(user[0].name, "INIT"));
    }
    {   /* records of ours with values out of range (a bug, a damaged copy that passed its CRC): checked */
        uint32_t i;
        memset(&rec_set, 0, sizeof rec_set);
        rec_set.h.ver = DATA_VER;
        rec_set.h.size = sizeof set;
        memset(rec_set.s, 0x7F, sizeof rec_set.s);
        store_save(REC_SETTINGS, APP_ID, &rec_set, sizeof rec_set);
        memset(&rec_user, 0, sizeof rec_user);
        rec_user.h.ver = DATA_VER;
        rec_user.h.size = sizeof rec_user.p;
        for (i = 0; i < USER_PER_REC; i++) {
            memset(&rec_user.p[i], 0x80, sizeof(preset_t));   /* -128 everywhere, no 0 at the name's end */
        }
        store_save(REC_USER0, APP_ID, &rec_user, sizeof rec_user);
        power_cycle();
        check("stored settings out of range: clamped or reset",
              set.g[G_VOL] == 100 && set.g[G_TUNE] == 100 && set.g[G_MIDICH] == 16 && set.last_preset == 0u &&
              set.oct == 0 && set.bpm == 127u && set.chord_n[0] == 0u);
        check("stored presets out of range: clamped, the name made printable",
              user[0].p[P_R16] == 0 && user[0].p[P_FENV] == -100 && user[0].p[P_ENS] == 0 &&
              user[0].name[0] == '?' && user[0].name[NAME_LEN - 1] == 0);
    }
    {   /* an older, shorter settings record (only the first fields): the rest at their defaults */
        host_nor_erase();
        memset(&rec_set, 0, sizeof rec_set);
        rec_set.h.ver = DATA_VER;
        rec_set.h.size = NG;                        /* only g[] */
        rec_set.s[G_VOL] = 33;
        store_save(REC_SETTINGS, APP_ID, &rec_set, sizeof(rec_head_t) + NG);
        power_cycle();
        check("an older, shorter settings record: its fields, defaults after",
              set.g[G_VOL] == 33 && set.bpm == 120u && set.trs_midi == 1u);
    }
    flash_ok = 0;
    tap(B_SAVE);
    tap(B_SAVE);
    check("no flash: SAVE fails visibly", str_eq(ui.msg, "SAVE FAILED"));
    flash_ok = 1;
    host_nor_erase();
    power_cycle();
}

/* ---------------------------------------------------------------- sound --- */
#define SECS 1u
static int32_t buf[2u * FS * SECS];
#define ONE_S (FS / CTL * CTL)

static double pitch(const int32_t *b, uint32_t frames)   /* rising zero crossings per second, left channel */
{
    uint32_t i, n = 0, first = 0, last = 0;
    for (i = 1; i < frames; i++)
        if (b[2u * (i - 1u)] < 0 && b[2u * i] >= 0) {
            if (!n)
                first = i;
            last = i;
            n++;
        }
    return n > 1u ? (double)(n - 1u) * FS / (double)(last - first) : 0.0;
}

static int32_t peak(const int32_t *b, uint32_t frames)
{
    int32_t m = 0;
    uint32_t i;
    for (i = 0; i < 2u * frames; i++)
        m = abs(b[i]) > m ? abs(b[i]) : m;
    return m;
}

/* the level of a tone at f Hz (Goertzel, Hann window), dB relative to an arbitrary reference: compare two */
static double tone_db(const int32_t *b, uint32_t frames, double f)
{
    double w = 2.0 * M_PI * f / FS, c = 2.0 * cos(w), s1 = 0, s2 = 0, s0, re, im;
    uint32_t i;
    for (i = 0; i < frames; i++) {
        double win = 0.5 - 0.5 * cos(2.0 * M_PI * i / (frames - 1));
        s0 = b[2u * i] * win + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    re = s1 - s2 * cos(w);
    im = s2 * sin(w);
    return 10.0 * log10(re * re + im * im + 1e-30);
}

static double flat_share(const int32_t *b, uint32_t frames)   /* samples within 15 % of the peak level */
{
    int32_t pk = peak(b, frames);
    uint32_t i, n = 0;
    for (i = 0; i < frames; i++)
        n += abs(b[2u * i]) * 100 > pk * 85;
    return (double)n / frames;
}

static double pos_share(const int32_t *b, uint32_t frames, double *dc)   /* share above zero; the mean / peak */
{
    int32_t pk = peak(b, frames);
    uint32_t i, n = 0;
    double sum = 0;
    for (i = 0; i < frames; i++) {
        n += b[2u * i] > 0;
        sum += b[2u * i];
    }
    *dc = sum / frames / (pk ? pk : 1);
    return (double)n / frames;
}

static int same_lr(const int32_t *b, uint32_t frames)
{
    uint32_t i;
    for (i = 0; i < frames; i++)
        if (b[2u * i] != b[2u * i + 1u])
            return 0;
    return 1;
}

/* a test sound: the registers, attack and release indexes; sustain 100 %, retrigger */
static void voice(int r16, int r8, int r4, int atk, int rel)
{
    cur.p[P_R16] = (int8_t)r16;
    cur.p[P_R8] = (int8_t)r8;
    cur.p[P_R4] = (int8_t)r4;
    cur.p[P_ATK] = (int8_t)atk;
    cur.p[P_DEC] = 0;
    cur.p[P_SUS] = 100;
    cur.p[P_REL] = (int8_t)rel;
    cur.p[P_SHAPE] = 0;
    cur.p[P_CUT] = 127;                                 /* the filter open, flat */
    cur.p[P_RES] = 0;
    cur.p[P_FENV] = 0;
    cur.p[P_KTRK] = 0;
    set.g[G_TRIG] = 0;
    snd_update();
}

static void silence(void)                               /* every note gone, the phases back at 0 */
{
    fm1_in.notes = 0;
    app_audio_stop();
    host_render(buf, 64);
    memset(vox.phase, 0, sizeof vox.phase);
}

static void sound(void)
{
    double f;
    int32_t pk, lvl;
    uint32_t i;
    host_nor_erase();
    power_cycle();
    host_render(buf, 1024);
    check("silence with nothing held", peak(buf, 1024) == 0);

    voice(0, 100, 0, 0, 0);
    fm1_in.notes = 1u;                                  /* key 0: F3, 174.61 Hz */
    host_render(buf, ONE_S);
    f = pitch(buf, ONE_S);
    check("8': key F3 at 174.6 Hz (+-0.5)", fabs(f - 174.61) < 0.5);
    lvl = REG_AMP * snd.gain / 256;
    pk = peak(buf + 2u * 4096u, ONE_S - 4096u);
    check("8': the level is the register's x VOLUME x MASTER (+-10 %)", abs(pk - lvl) * 10 < lvl);
    check("mono: left = right", same_lr(buf, ONE_S));
    voice(100, 0, 0, 0, 0);
    host_render(buf, ONE_S);
    check("16': an octave down (87.3 Hz)", fabs(pitch(buf, ONE_S) - 87.31) < 0.5);
    voice(0, 0, 100, 0, 0);
    host_render(buf, ONE_S);
    check("4': an octave up (349.2 Hz)", fabs(pitch(buf, ONE_S) - 349.23) < 0.5);
    silence();

    voice(0, 100, 0, 0, 0);                             /* the key gate: no click at a note's start */
    fm1_in.notes = 1u;
    host_render(buf, 1024);
    check("a key's start ramps in over the key gate (no click)", peak(buf, 32) < lvl / 4 && peak(buf + 2u * 512u, 512) > lvl / 2);
    silence();

    /* the dividers: C4 and C5 phase-locked, whenever C5 comes in (two runs, the same samples once both are open) */
    {
        static int32_t a[2u * 2048u];
        voice(0, 100, 0, 0, 0);
        fm1_in.notes = 1u << 7;                          /* C4 */
        host_render(buf, 1024);
        fm1_in.notes |= 1u << 19;                        /* C5 at 1024 */
        host_render(buf, 4096);
        memcpy(a, buf + 2u * 2048u, sizeof a);
        silence();
        fm1_in.notes = 1u << 7;
        host_render(buf, 2048);
        fm1_in.notes |= 1u << 19;                        /* C5 at 2048 */
        host_render(buf, 3072);                          /* both runs end at 5120: compare 4096 .. 5120 */
        check("divide-down: C5 phase-locked to C4, wherever it starts",
              !memcmp(a + 2u * 1024u, buf + 2u * 2048u, 2u * 1024u * 4u) && peak(a, 2048) > 0);
        silence();
    }

    /* the shared envelope: the release keeps the notes sounding, then exact silence */
    voice(0, 100, 0, 0, 40);                            /* release ~ 49 ms */
    fm1_in.notes = 1u | 1u << 4;
    host_render(buf, 4096);
    fm1_in.notes = 0;
    host_render(buf, 1024);
    check("keys up: the notes go on through the shared release", peak(buf + 2u * 256u, 64) > lvl / 4);
    host_render(buf, ONE_S);
    check("release: ends in exact silence", peak(buf + 2u * (ONE_S - 1024u), 1024) == 0 && vox.stage == ENV_IDLE);
    silence();
    {   /* the time a release shows is the time it takes: sounding at a quarter of it, gone by 1.5x */
        uint32_t ms = pv_ms(K_REL, 70), q = ms * (FS / 1000u) / 4u / CTL * CTL;
        voice(0, 100, 0, 0, 70);
        fm1_in.notes = 1u;
        host_render(buf, 4096);
        fm1_in.notes = 0;
        host_render(buf, q);
        check("RELEASE 330 ms: still sounding at a quarter", peak(buf + 2u * (q - 512u), 512) > 0);
        host_render(buf, 5u * q);                       /* to 1.5x */
        check("RELEASE 330 ms: exact silence by 1.5x", vox.stage == ENV_IDLE && peak(buf + 2u * (5u * q - 512u), 512) == 0);
        silence();
    }

    voice(0, 100, 0, 80, 0);                            /* attack ~ 300 ms, sustain 50 % */
    cur.p[P_SUS] = 50;
    snd_update();
    fm1_in.notes = 1u;
    host_render(buf, 2048);
    check("attack: rises slowly", peak(buf, 1024) < lvl / 4 && vox.stage == ENV_ATK);
    host_render(buf, ONE_S);
    check("decay: settles on the sustain", vox.stage == ENV_DEC && vox.env == snd.sus && snd.sus == ENV_ONE / 2);
    fm1_in.notes |= 1u << 4;
    host_render(buf, 64);
    check("RETRIG: a new key restarts the attack (from where it is)",
          vox.stage == ENV_ATK && vox.env > ENV_ONE / 2 && vox.env < ENV_ONE * 3 / 5);
    silence();
    voice(0, 100, 0, 80, 0);
    cur.p[P_SUS] = 50;
    set.g[G_TRIG] = 1;
    snd_update();
    fm1_in.notes = 1u;
    host_render(buf, ONE_S);
    fm1_in.notes |= 1u << 4;
    host_render(buf, 64);
    check("LEGATO: a new key joins without a new attack", vox.stage == ENV_DEC);
    set.g[G_TRIG] = 0;
    silence();

    /* HOLD */
    voice(0, 100, 0, 0, 0);
    tap(B_SEQ);
    fm1_in.notes = 1u;
    host_render(buf, 1024);
    fm1_in.notes = 0;
    host_render(buf, 2048);
    check("HOLD: the key keeps sounding after it is up", peak(buf + 2u * 1024u, 1024) > lvl / 2);
    fm1_in.notes = 1u << 12;                            /* a new key after all were up: replaces F3 */
    host_render(buf, 1024);
    fm1_in.notes = 0;
    host_render(buf, ONE_S);
    check("HOLD: a new key replaces the held one", fabs(pitch(buf, ONE_S) - 349.23) < 0.5);
    tap(B_SEQ);
    host_render(buf, ONE_S);
    check("HOLD off: released, silent", peak(buf + 2u * (ONE_S - 1024u), 1024) == 0);
    silence();

    /* MIDI */
    host_midi(0x90, 69, 100);                           /* A4 over USB-MIDI, OMNI */
    host_render(buf, ONE_S);
    check("MIDI A4: 440 Hz (+-1)", fabs(pitch(buf, ONE_S) - 440.0) < 1.0);
    host_midi(0x80, 60, 0);                             /* another note's off */
    host_render(buf, 2048);
    check("note-off of another note: A4 goes on", peak(buf + 2u * 1024u, 1024) > lvl / 2);
    host_midi(0x90, 69, 0);                             /* note-on velocity 0 = off */
    host_render(buf, 4096);
    check("note-on velocity 0: off", peak(buf + 2u * 2048u, 2048) == 0);
    host_midi(0xB0, 64, 127);                           /* pedal down */
    host_midi(0x90, 69, 100);
    host_render(buf, 1024);
    host_midi(0x80, 69, 0);
    host_render(buf, 4096);
    check("sustain pedal: the note goes on after its off", peak(buf + 2u * 2048u, 2048) > lvl / 2);
    host_midi(0xB0, 64, 0);
    host_render(buf, 4096);
    check("pedal up: released", peak(buf + 2u * 2048u, 2048) == 0);
    host_midi(0x90, 60, 100);
    host_midi(0x90, 64, 100);
    host_render(buf, 1024);
    host_midi(0xB0, 123, 0);
    host_render(buf, 4096);
    check("all notes off (CC123): silent", peak(buf + 2u * 2048u, 2048) == 0);

    tap(B_GLO);                                         /* MIDI CH 2 */
    host_turn(EN_K3, 2);
    host_frame();
    tap(B_HOME);
    host_midi(0x90, 69, 100);                           /* channel 1: not ours */
    host_render(buf, 2048);
    check("MIDI CH 2: channel 1 ignored", peak(buf, 2048) == 0);
    host_midi(0x91, 69, 100);
    host_render(buf, 2048);
    check("MIDI CH 2: channel 2 plays", peak(buf + 2u * 1024u, 1024) > lvl / 2);
    tap(B_GLO);
    host_turn(EN_K3, -1);                               /* CH 1: the CH 2 note must not hang */
    host_frame();
    host_render(buf, 4096);
    check("MIDI channel changed: its notes released", peak(buf + 2u * 2048u, 2048) == 0);
    host_turn(EN_K3, -1);
    host_frame();
    tap(B_HOME);
    host_midi(0x90, 57, 100);
    host_render(buf, 1024);
    midi_in_overflow = 1;                               /* a broken stream: events lost */
    host_render(buf, 4096);
    check("MIDI ring overflow: every note released", peak(buf + 2u * 2048u, 2048) == 0 && !midi_in_overflow);

    fm1_in.notes = 1u;
    host_midi(0x90, 64, 100);
    host_render(buf, 1024);
    fm1_in.notes = 0;
    app_audio_stop();
    host_render(buf, 1024);
    check("app_audio_stop: silent at once, the MIDI note forgotten", peak(buf, 1024) == 0);

    /* band-limited saws: a high note's folded harmonics far down (the naive ramp folds its 7th at -17 dB) */
    {
        double fund, alias7, alias9, f0 = 3951.07;          /* B7, 8' */
        silence();
        voice(0, 100, 0, 0, 0);
        host_midi(0x90, 107, 100);
        host_render(buf, ONE_S);
        fund = tone_db(buf + 2u * 8192u, 32768, f0);
        alias7 = tone_db(buf + 2u * 8192u, 32768, FS - 7.0 * f0);   /* 27 658 Hz folded to 16 442 Hz */
        alias9 = tone_db(buf + 2u * 8192u, 32768, 9.0 * f0 - FS);   /* 35 560 Hz folded to 8 540 Hz */
        printf("  (B7: 7th folded %.1f dB, 9th folded %.1f dB under the fundamental)\n", alias7 - fund, alias9 - fund);
        /* measured: naive -16.9 / -19.1 dB, PolyBLEP -29.8 / -44.5 dB (a 2-sample BLEP is weakest near Nyquist) */
        check("PolyBLEP: B7's 7th folded to 16.4 kHz > 25 dB down", alias7 - fund < -25.0);
        check("PolyBLEP: B7's 9th folded to 8.5 kHz > 35 dB down", alias9 - fund < -35.0);
        host_midi(0x80, 107, 0);
        silence();
    }
    /* SHAPE: the saw ramps, the square sits at two levels, the pulse narrows; no DC; the pitch stays */
    {
        static const struct { int shape; const char *text; } T[] = {
            {0, "SAW"}, {20, "SAW+SQR"}, {50, "SQUARE"}, {66, "SQUARE"}, {67, "PULSE 49%"}, {99, "PULSE 10%"}};
        uint32_t k;
        int good = 1;
        double flat_saw, flat_sq, pos, dc;
        char b[24];
        for (k = 0; k < sizeof T / sizeof T[0]; k++) {
            pv_text(b, sizeof b, &SP[P_SHAPE], T[k].shape);
            good &= str_eq(b, T[k].text);
        }
        check("SHAPE values: SAW, SAW+SQR, SQUARE, PULSE 49% .. 10%", good);
        voice(0, 100, 0, 0, 0);
        fm1_in.notes = 1u << 7;                                     /* C4 */
        host_render(buf, ONE_S);
        flat_saw = flat_share(buf + 2u * 4096u, 32768);
        cur.p[P_SHAPE] = 66;
        snd_update();
        host_render(buf, ONE_S);
        flat_sq = flat_share(buf + 2u * 4096u, 32768);
        printf("  (samples near the peaks: saw %.2f, square %.2f)\n", flat_saw, flat_sq);
        check("SHAPE 66: a square (most samples at its two levels)", flat_sq > 0.85 && flat_saw < 0.3);
        check("SHAPE 66: the pitch stays (C4 261.6 Hz)", fabs(pitch(buf, ONE_S) - 261.63) < 0.5);
        cur.p[P_SHAPE] = 99;
        snd_update();
        host_render(buf, ONE_S);
        pos = pos_share(buf + 2u * 4096u, 32768, &dc);
        printf("  (pulse 10 %%: %.3f of the samples above zero, DC %.4f of the peak)\n", pos, dc);
        check("SHAPE 99: a 10 % pulse", fabs(pos - 0.10) < 0.03 || fabs(pos - 0.90) < 0.03);
        check("SHAPE 99: no DC (< 1 % of the peak)", fabs(dc) < 0.01);
        fm1_in.notes = 0;
        cur.p[P_SHAPE] = 0;
        snd_update();
        silence();
    }

    /* the shared filter: a 12 dB/oct low-pass, resonance, the envelope's sweep, key tracking, stable at the extremes */
    {
        const double f0 = 65.41;                                    /* C2, 8': harmonics every 65.4 Hz */
        double h2o, h32o, h64o, h2, h32, h64, h15o, h15r;
        silence();
        voice(0, 100, 0, 0, 0);
        host_midi(0x90, 36, 100);
        host_render(buf, ONE_S);
        h2o = tone_db(buf + 2u * 8192u, 32768, 2 * f0);
        h15o = tone_db(buf + 2u * 8192u, 32768, 15 * f0);
        h32o = tone_db(buf + 2u * 8192u, 32768, 32 * f0);
        h64o = tone_db(buf + 2u * 8192u, 32768, 64 * f0);
        cur.p[P_CUT] = 74;                                          /* ~990 Hz */
        snd_update();
        host_render(buf, ONE_S);
        h2 = tone_db(buf + 2u * 8192u, 32768, 2 * f0);
        h32 = tone_db(buf + 2u * 8192u, 32768, 32 * f0);
        h64 = tone_db(buf + 2u * 8192u, 32768, 64 * f0);
        printf("  (CUTOFF 990 Hz on C2: 131 Hz %+.1f dB, 2.1 kHz %+.1f dB, 4.2 kHz %+.1f dB)\n", h2 - h2o, h32 - h32o, h64 - h64o);
        check("filter: below the cutoff untouched (131 Hz within 1.5 dB)", fabs(h2 - h2o) < 1.5);
        check("filter: an octave above the cutoff down > 8 dB", h32 - h32o < -8.0);
        check("filter: two octaves above down > 20 dB (12 dB/oct)", h64 - h64o < -20.0);
        cur.p[P_RES] = 90;
        snd_update();
        host_render(buf, ONE_S);
        h15r = tone_db(buf + 2u * 8192u, 32768, 15 * f0);
        cur.p[P_RES] = 0;
        snd_update();
        host_render(buf, ONE_S);
        printf("  (RESONANCE 90 %%: the harmonic at the cutoff %+.1f dB over RESONANCE 0)\n", h15r - tone_db(buf + 2u * 8192u, 32768, 15 * f0));
        check("filter: RESONANCE 90 % lifts the cutoff's harmonic > 10 dB", h15r - tone_db(buf + 2u * 8192u, 32768, 15 * f0) > 10.0);
        (void)h15o;
        host_midi(0x80, 36, 0);
        silence();

        voice(0, 100, 0, 80, 0);                                    /* attack ~300 ms, ENV AMOUNT +100 % */
        cur.p[P_CUT] = 30;
        cur.p[P_FENV] = 100;
        snd_update();
        fm1_in.notes = 1u;
        host_render(buf, 64);
        check("ENV AMOUNT +100 %: the sweep starts at CUTOFF", abs(flt.idx_pub - 30 * 256) < 3 * 256);
        host_render(buf, ONE_S);
        check("ENV AMOUNT +100 %: at full level the cutoff is all the way up", flt.idx_pub == 127 * 256);
        cur.p[P_CUT] = 127;
        cur.p[P_FENV] = -100;
        snd_update();
        host_render(buf, 64);
        check("ENV AMOUNT -100 %: the envelope closes it", flt.idx_pub == 0);
        silence();

        voice(0, 100, 0, 0, 0);                                     /* KEY TRACK 100 %: a semitone, 1.1 steps */
        cur.p[P_CUT] = 60;
        cur.p[P_KTRK] = 100;
        snd_update();
        host_midi(0x90, 60, 100);
        host_render(buf, 256);
        check("KEY TRACK 100 %: C4 keeps CUTOFF", abs(flt.idx_pub - 60 * 256) < 64);
        host_midi(0x90, 84, 100);                                   /* C6 joins: the highest note leads */
        host_render(buf, 256);
        printf("  (KEY TRACK: C6 opens it %.2f steps)\n", flt.idx_pub / 256.0 - 60);
        check("KEY TRACK 100 %: C6 two octaves up (+26.3 steps)", fabs(flt.idx_pub / 256.0 - 60 - 26.34) < 0.3);
        host_midi(0xB0, 123, 0);
        silence();

        voice(100, 100, 100, 0, 30);                                /* the extremes: every key, RESONANCE 100 % */
        cur.p[P_RES] = 100;
        snd_update();
        fm1_in.notes = (1u << 27) - 1u;
        host_render(buf, ONE_S);
        check("RESONANCE 100 %, all keys: bounded, no blow-up", peak(buf, ONE_S) <= 32767 && peak(buf, ONE_S) > 1000 &&
                                                                 flt.ic1 == flt.ic1);
        fm1_in.notes = 0;
        host_render(buf, ONE_S);
        check("RESONANCE 100 %: the release ends in exact silence, the filter at rest",
              peak(buf + 2u * (ONE_S - 1024u), 1024) == 0 && flt.ic1 == 0.0f && flt.ic2 == 0.0f);
        voice(0, 100, 0, 0, 0);
        silence();
    }

    /* the ceiling: held notes first, releasing ones dropped past it; the load shedding moves it */
    silence();
    voice(0, 100, 0, 0, 0);
    vox.cap = CAP_MIN;
    fm1_in.notes = (1u << 10) - 1u;                     /* 10 keys held, the cap at 8 */
    host_render(buf, 256);
    check("cap 8, 10 held: 8 rendered, the 2 highest wait silent",
          vox.sounding_pub == 8u && vox.gate[53 + 7] == GATE_ONE && vox.gate[53 + 8] == 0 && vox.gate[53 + 9] == 0);
    fm1_in.notes &= ~1u;                                /* key 0 up: releasing, past the cap behind 8 held */
    host_render(buf, CTL);
    check("cap 8: a releasing note gives way to the held ones", vox.gate[53] == 0 && vox.sounding_pub == 8u &&
                                                                vox.gate[53 + 8] == GATE_STEP);
    silence();
    vox.cap = MAX_SOUNDING;
    audio_cpu_q8 = 240;                                 /* ~94 %: shed */
    frames(5);
    check("load > 80 %: the cap falls fast (2 a frame)", vox.cap == MAX_SOUNDING - 10u);
    frames(20);
    check("load > 80 %: down to CAP_MIN, not below", vox.cap == CAP_MIN);
    audio_cpu_q8 = 100;                                 /* ~39 % */
    frames(17);
    check("load < 60 %: the cap climbs back slowly (1 per 250 ms)", vox.cap == CAP_MIN + 1u);
    frames(700);                                        /* (MAX_SOUNDING - CAP_MIN steps of 250 ms) */
    check("load < 60 %: back up to MAX_SOUNDING", vox.cap == MAX_SOUNDING);
    audio_cpu_q8 = 180;                                 /* 70 %: between, nothing moves */
    vox.cap = 20;
    frames(100);
    check("load between 60 and 80 %: the cap stays", vox.cap == 20u);
    tap(B_HOME);
    host_frame();
    audio_cpu_q8 = 60;
    host_frame();
    check("HOME: a new CPU reading redraws the status line only", host_blits == 1u);
    audio_cpu_q8 = 0;
    vox.cap = MAX_SOUNDING;

    /* every key and every MIDI note at once, all registers: the ceiling holds, no wrap-around */
    voice(100, 100, 100, 0, 0);
    fm1_in.notes = (1u << 27) - 1u;
    for (i = 0; i < 128u; i++) {                        /* (16 a block: the input ring holds 64 events) */
        host_midi(0x90, i, 100);
        if ((i & 15u) == 15u)
            host_render(buf, CTL);
    }
    host_render(buf, ONE_S);
    check("all 128 notes: at most MAX_SOUNDING rendered", vox.sounding_pub == MAX_SOUNDING);
    check("all 128 notes: within full scale", peak(buf, ONE_S) <= 32767 && peak(buf, ONE_S) > 10000);
    host_midi(0xB0, 123, 0);
    silence();

    set.g[G_VOL] = 0;                                   /* VOLUME 0 */
    snd_update();
    fm1_in.notes = 1u;
    host_render(buf, 4096);
    check("VOLUME 0: silent", peak(buf, 4096) == 0);
    set.g[G_VOL] = GP[G_VOL].def;
    set.g[G_TUNE] = 100;                                /* +100 cents: F#3 */
    snd_update();
    voice(0, 100, 0, 0, 0);
    host_render(buf, ONE_S);
    check("TUNE +100 ct: a semitone up (185.0 Hz)", fabs(pitch(buf, ONE_S) - 185.0) < 0.5);
    set.g[G_TUNE] = 0;
    snd_update();
    silence();
    {
        uint32_t pkt;
        while (host_midi_out(&pkt))
            ;
        midi_out(0x90, 60, 100);
        check("midi_out: a USB-MIDI note-on packet", host_midi_out(&pkt) && pkt == (0x09u | 0x90u << 8 | 60u << 16 | 100u << 24));
    }
}

int main(void)
{
    if (system("mkdir -p build/shots"))
        return 1;
    screens();
    controls();
    persistence();
    sound();
    puts(failed ? "APP TEST FAILED" : "app test passed");
    return failed ? 1 : 0;
}
