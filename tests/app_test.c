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
static int32_t buf_ctl[2u * CTL];
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

static uint32_t offsetof_last_preset(void) { return (uint32_t)((uint8_t *)&set.last_preset - (uint8_t *)&set); }

static void power_cycle(void)                           /* the app's RAM gone, the flash kept */
{
    memset(&ui, 0, sizeof ui);
    memset((void *)&vox, 0, sizeof vox);
    memset(&snd, 0, sizeof snd);
    memset(&cur, 0, sizeof cur);
    memset(&set, 0, sizeof set);
    memset(user, 0, sizeof user);
    memset(ens_buf, 0, sizeof ens_buf);                 /* the pool is zeroed at power-on (main.c) */
    memset(flg_buf, 0, sizeof flg_buf);
    memset(dly_buf, 0, sizeof dly_buf);
    memset(&fxs, 0, sizeof fxs);
    memset(&flt, 0, sizeof flt);
    memset(&ccs, 0, sizeof ccs);                        /* (the CC out state and its queue: RAM, gone too) */
    memset((void *)&arp, 0, sizeof arp);                /* (the transport and the arp: stopped at power-on) */
    arp.note = -1;
    arp_on = chord_rec = 0;
    memset((void *)&ccq, 0, sizeof ccq);
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

static uint32_t fb_hash(uint32_t y0, uint32_t y1)       /* the pixels of rows y0..y1 */
{
    uint32_t h = 2166136261u, i;
    for (i = y0 * 240u; i < y1 * 240u; i++)
        h = (h ^ host_fb[i]) * 16777619u;
    return h;
}

static uint32_t theme_rows(uint32_t y0, uint32_t y1)    /* rows holding the theme colour */
{
    uint32_t x, y, n = 0;
    for (y = y0; y < y1; y++)
        for (x = 0; x < 240u; x++)
            if (host_fb[y * 240u + x] == T_THEME) {
                n++;
                break;
            }
    return n;
}

static int32_t column_y(uint32_t x, uint32_t y0, uint32_t y1)   /* the topmost theme pixel in a column */
{
    uint32_t y;
    for (y = y0; y < y1; y++)
        if (host_fb[y * 240u + x] == T_THEME)
            return (int32_t)y;
    return (int32_t)y1;
}

static int32_t curve_top(uint32_t y0, uint32_t y1)      /* the highest point of the drawing */
{
    uint32_t x;
    int32_t t = (int32_t)y1;
    for (x = 0; x < 240u; x++) {
        int32_t y = column_y(x, y0, y1);
        t = y < t ? y : t;
    }
    return t;
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
    check("HOME: KNOB 1 is 16', the drawbars follow (and the edited mark)", cur.p[P_R16] == FACTORY[0].p[P_R16] - 3 &&
                                                                         host_blits == 2u && preset_edited());

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
    check("SELECT forward: up to the last page", ui.page == NPAGES - 1u);
    tap(B_ENV);
    host_turn(EN_K1, 5);
    host_frame();
    check("ENVELOPE: KNOB 1 is ATTACK", cur.p[P_ATK] == FACTORY[0].p[P_ATK] + 5);
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

    {   /* the drawings: ENVELOPE's ADSR, FILTER's response (in the strip at y 162 .. 212, drawn in T_THEME) */
        uint32_t h0, h1;
        int32_t top0, top90, lo120, lo60;
        tap(B_ENV);
        host_frame();
        h0 = fb_hash(162, 212);
        check("ENVELOPE: the ADSR drawn", theme_rows(162, 212) > 20u);
        host_turn(EN_K4, 20);                                   /* RELEASE longer */
        host_frame();
        h1 = fb_hash(162, 212);
        check("ENVELOPE: a knob turned redraws its rows and the drawing (2 blits), the shape changes",
              host_blits == 2u && h1 != h0);
        host_frame();
        check("ENVELOPE: nothing changed, nothing drawn", host_blits == 0u);
        host_turn(EN_K4, -20);
        tap(B_SCL);
        cur.p[P_CUT] = 90;
        cur.p[P_RES] = 0;
        host_frame();
        top0 = curve_top(162, 212);
        cur.p[P_RES] = 90;
        host_frame();
        top90 = curve_top(162, 212);
        host_png("build/shots/filter_res.png");
        printf("  (the FILTER curve's peak: y %d at RESONANCE 0, y %d at 90; 0 dB is y %d)\n", top0, top90, 162 + 2 + 24 * 46 / 60);
        check("FILTER: RESONANCE 90 % draws a peak above 0 dB", top90 < top0 && top90 < 162 + 2 + 24 * 46 / 60);
        cur.p[P_RES] = 0;
        cur.p[P_CUT] = 120;
        host_frame();
        lo120 = column_y(CURVE_X0 + 210, 162, 212);
        cur.p[P_CUT] = 60;
        host_frame();
        lo60 = column_y(CURVE_X0 + 210, 162, 212);
        check("FILTER: a lower CUTOFF pulls the top end down", lo60 > lo120);
        cur.p[P_CUT] = FACTORY[0].p[P_CUT];
        cur.p[P_RES] = FACTORY[0].p[P_RES];
        tap(B_HOME);
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
            int8_t keep;
            int32_t x;
            if (!q)                                     /* an empty row (FX 3's last two) */
                continue;
            keep = *v;
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
    check("ARP tapped: on", ui.arp && arp_on && seqp.on && str_eq(ui.msg, "ARP ON"));
    tap(B_ARP);
    check("ARP tapped again: off", !ui.arp && !seqp.on);
    host_press(B_ARP, 1);
    host_frame();
    host_turn(EN_PRESET, 8);
    host_frame();
    check("ARP held + PRESETS: the tempo", set.bpm == 128 && cur_index == 0u);
    host_press(B_ARP, 0);
    host_frame();
    check("ARP held + PRESETS: no tap on release", !ui.arp);
    tap(B_PLAY);                                        /* the transport: MIDI clock out, the arp on or off */
    host_render(buf_ctl, CTL);
    check("PLAY (the arp off): the transport runs, says so", arp.run_pub && str_eq(ui.msg, "PLAY: CLOCK OUT RUNNING"));
    tap(B_PLAY);
    host_render(buf_ctl, CTL);
    check("PLAY again: stopped", !arp.run_pub && str_eq(ui.msg, "STOP: CLOCK OUT STOPPED"));
    tap(B_REC);
    check("REC tapped: recording a chord", ui.recording && str_eq(ui.msg, "CHORD: HOLD KEYS, THEN REC"));
    tap(B_REC);
    check("REC again with no keys: no chord, says why", !ui.recording && !chord_on() && str_eq(ui.msg, "CHORD: HOLD 2 OR MORE KEYS"));
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
        check("an older, shorter settings record: its fields, defaults after (GLOBAL 2 too)",
              set.g[G_VOL] == 33 && set.bpm == 120u && set.trs_midi == 1u && set.g2[G2_CCOUT] == 1 && set.g2[G2_ATD] == 50);
    }
    flash_ok = 0;
    tap(B_SAVE);
    tap(B_SAVE);
    check("no flash: SAVE fails visibly", str_eq(ui.msg, "SAVE FAILED"));
    flash_ok = 1;
    host_nor_erase();
    power_cycle();
}

/* ---------------------------------------------------------------- presets --- */
static int32_t peak(const int32_t *b, uint32_t frames);   /* (sound helpers, below) */
static void nrpn_in(uint32_t num, uint32_t v14, int lsb);
static void voice(int r16, int r8, int r4, int atk, int rel);
static void silence(void);
static void presets(void)
{
    static int32_t pb[2u * 16384u];
    uint32_t i, k, good = 1, names = 1;
    host_nor_erase();
    power_cycle();
    for (i = 0; i < NFACT; i++) {                       /* the factory set: valid values, distinct short names */
        preset_t p = FACTORY[i];
        preset_check(&p);
        good &= !memcmp(&p, &FACTORY[i], sizeof p);
        names &= str_len(FACTORY[i].name) >= 1u && str_len(FACTORY[i].name) <= NAME_EDIT;
        for (k = 0; k < i; k++)
            names &= !str_eq(FACTORY[i].name, FACTORY[k].name);
    }
    check("16 factory sounds, every value in range", NFACT == 16u && good);
    check("factory names: 1..10 characters, all different", names);
    host_turn(EN_PRESET, 15);
    host_frame();
    check("PRESETS: F16 ECHOES after F1 .. F15", cur_index == 15u && str_eq(cur.name, "ECHOES"));
    host_turn(EN_PRESET, 1);
    host_frame();
    check("PRESETS: then U01", cur_index == NFACT && !str_eq(ui.msg, ""));
    {   /* every factory sound plays a chord at a sane level, bounded, no blow-up */
        int ok = 1;
        for (i = 0; i < NFACT; i++) {
            int32_t pk;
            silence();
            preset_load(i);
            fm1_in.notes = 1u << 7 | 1u << 11 | 1u << 14;   /* C4 E4 G4 */
            host_render(pb, 16384);                     /* the first 370 ms (PLUCK has no sustain) */
            pk = peak(pb, 16384);
            if (pk < 1500 || pk > 32767) {
                printf("  (%s: peak %d)\n", FACTORY[i].name, pk);
                ok = 0;
            }
            fm1_in.notes = 0;
        }
        check("every factory sound plays a chord (peak 1 500 .. 32 767)", ok);
        silence();
    }

    /* the edited mark and revert; INIT */
    preset_load(2);                                     /* F3 VIOLINS */
    host_frame();
    check("a preset just loaded: not edited", !preset_edited());
    tap(B_ENV);
    host_turn(EN_K4, -10);
    host_frame();
    check("a knob turned: edited", preset_edited());
    host_press(B_HOME, 1);
    frames(40);
    host_press(B_HOME, 0);
    host_frame();
    check("HOME held: reverted to the stored F3", !preset_edited() && cur.p[P_REL] == FACTORY[2].p[P_REL] &&
                                                  ui.page == PG_HOME && str_eq(ui.msg, "REVERTED"));
    host_press(B_SAVE, 1);
    frames(40);
    host_press(B_SAVE, 0);
    host_frame();
    check("SAVE held: the INIT sound, shown as NEW, no SAVE screen",
          !ui.saving && cur_index == PRESET_NEW && !memcmp(cur.p, INIT_SOUND.p, sizeof cur.p) && str_eq(ui.msg, "INIT SOUND"));
    host_png("build/shots/init.png");
    tap(B_SAVE);
    check("SAVE tapped: the SAVE screen, at U01 from a new sound", ui.saving && ui.save_slot == 0u && ui.save_src == 0u);

    /* naming: KNOB 1 the cursor, KNOB 2 the letter, KNOB 3 a word, KNOB 4 clear */
    host_turn(EN_K4, 1);                                /* clear */
    host_frame();
    check("SAVE: KNOB 4 clears the name", str_eq(ui.nm, "          "));
    host_turn(EN_K2, 1);                                /* ' ' -> A (a frame between turns: they add up otherwise) */
    host_frame();
    host_turn(EN_K1, 1);
    host_frame();
    host_turn(EN_K2, 2);                                /* ' ' -> B */
    host_frame();
    host_turn(EN_K1, 1);
    host_frame();
    host_turn(EN_K2, -1);                               /* ' ' -> '.' (wraps backwards) */
    host_frame();
    check("SAVE: KNOB 1 moves, KNOB 2 picks letters (A, B, and . wrapping back)", ui.nm[0] == 'A' && ui.nm[1] == 'B' && ui.nm[2] == '.');
    host_turn(EN_PRESET, 4);                            /* U05 */
    host_frame();
    tap(B_SAVE);
    check("SAVE: stored in U05 as AB., the sound now from U05", !ui.saving && str_eq(user[4].name, "AB.") &&
                                                              cur_index == NFACT + 4u && str_eq(cur.name, "AB."));
    tap(B_SAVE);
    host_turn(EN_K3, 1);                                /* the first word */
    host_frame();
    check("SAVE: KNOB 3 puts in a word", str_eq(ui.nm, "STRINGS   ") && ui.nm_cur == 0u);
    tap(B_SAVE);
    check("SAVE: the word stored, trimmed", str_eq(user[4].name, "STRINGS"));
    tap(B_SAVE);
    host_turn(EN_K4, 1);
    host_frame();
    tap(B_SAVE);
    check("SAVE with an empty name: NO NAME", str_eq(user[4].name, "NO NAME"));

    /* ALGORITHM picks what to store: a factory sound into a slot, or INIT to clear it; the sound playing stays */
    {
        preset_t before = cur;
        tap(B_SAVE);
        host_turn(EN_PRESET, 3);                        /* U08 */
        host_turn(EN_ALGO, 3);                          /* F3 VIOLINS */
        host_frame();
        check("SAVE: ALGORITHM picks F3, the name follows", ui.save_src == 3u && str_eq(ui.nm, "VIOLINS   "));
        tap(B_SAVE);
        check("restore: U08 = F3 VIOLINS, the sound playing unchanged",
              !memcmp(user[7].p, FACTORY[2].p, sizeof user[7].p) && str_eq(user[7].name, "VIOLINS") &&
              !memcmp(&cur, &before, sizeof cur) && str_eq(ui.msg, "U08 = VIOLINS"));
        tap(B_SAVE);
        host_turn(EN_PRESET, 3);
        host_turn(EN_ALGO, 100);                        /* past the factory sounds: INIT */
        host_frame();
        check("SAVE: ALGORITHM at the end is INIT", ui.save_src == SRC_INIT);
        str_cpy(user[NUSER - 1].name, "WWWWWWWWWWW", NAME_LEN);   /* the screen at its widest */
        host_turn(EN_PRESET, 40);
        host_frame();
        host_turn(EN_K4, 1);
        host_frame();
        for (k = 0; k < NAME_EDIT; k++) {
            host_turn(EN_K2, 23);                       /* W */
            host_frame();
            host_turn(EN_K1, 1);
            host_frame();
        }
        msg("SAVE CANCELLED");
        host_frame();
        host_png("build/shots/save_name.png");
        check("SAVE: ten Ws typed", str_eq(ui.nm, "WWWWWWWWWW"));
        lint("SAVE: naming, INIT, the longest names");
        tap(B_HOME);                                    /* cancel; again: INIT into U08 */
        check("SAVE cancelled by another button", !ui.saving && str_eq(ui.msg, "SAVE CANCELLED"));
        tap(B_SAVE);
        host_turn(EN_PRESET, 3);                        /* from U05: U08 */
        host_frame();
        host_turn(EN_ALGO, 100);
        host_frame();
        tap(B_SAVE);
        check("restore: INIT clears U08", !memcmp(user[7].p, INIT_SOUND.p, sizeof user[7].p) &&
                                          str_eq(user[7].name, "INIT") && str_eq(ui.msg, "U08 CLEARED"));
    }
    power_cycle();
    check("power-on: U05 and U08 as they were stored, the sound from U05",
          str_eq(user[4].name, "NO NAME") && !memcmp(user[7].p, INIT_SOUND.p, sizeof user[7].p) && cur_index == NFACT + 4u);

    {   /* a version 1 settings record (8 factory sounds, a plain index): U02 was index 9 */
        memset(&rec_set, 0, sizeof rec_set);
        rec_set.h.ver = 1;
        rec_set.h.size = sizeof set;
        memcpy(rec_set.s, &set, sizeof set);
        rec_set.s[offsetof_last_preset()] = 9;
        store_save(REC_SETTINGS, APP_ID, &rec_set, sizeof rec_set);
        power_cycle();
        check("a version 1 settings record: its U02 is still U02", cur_index == NFACT + 1u);
        rec_set.h.ver = 1;
        rec_set.s[offsetof_last_preset()] = 5;
        store_save(REC_SETTINGS, APP_ID, &rec_set, sizeof rec_set);
        power_cycle();
        check("a version 1 settings record: its F6 is still F6", cur_index == 5u);
    }
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

static double rms_l(const int32_t *b, uint32_t frames)
{
    double sum = 0;
    uint32_t i;
    for (i = 0; i < frames; i++)
        sum += (double)b[2u * i] * b[2u * i];
    return sqrt(sum / frames);
}

static double corr_lr(const int32_t *b, uint32_t frames)   /* the correlation of left and right, -1 .. 1 */
{
    double sl = 0, sr = 0, slr = 0;
    uint32_t i;
    for (i = 0; i < frames; i++) {
        sl += (double)b[2u * i] * b[2u * i];
        sr += (double)b[2u * i + 1u] * b[2u * i + 1u];
        slr += (double)b[2u * i] * b[2u * i + 1u];
    }
    return slr / sqrt(sl * sr + 1e-30);
}

static double period_ratio(const int32_t *b, uint32_t frames)   /* the longest period over the shortest (left) */
{
    uint32_t i, last = 0, lo = 0xFFFFFFFFu, hi = 0, n = 0;
    for (i = 1; i < frames; i++)
        if (b[2u * (i - 1u)] < 0 && b[2u * i] >= 0) {
            if (last && n > 2u) {
                uint32_t p = i - last;
                lo = p < lo ? p : lo;
                hi = p > hi ? p : hi;
            }
            last = i;
            n++;
        }
    return lo && hi ? (double)hi / lo : 0.0;
}

static int32_t peak_ch(const int32_t *b, uint32_t frames, uint32_t ch)   /* one channel's peak */
{
    int32_t m = 0;
    uint32_t i;
    for (i = 0; i < frames; i++)
        m = abs(b[2u * i + ch]) > m ? abs(b[2u * i + ch]) : m;
    return m;
}

static uint32_t tail_time(const int32_t *b, uint32_t frames, uint32_t from)   /* frames until 40 dB under the peak */
{
    int32_t pk = peak(b + 2u * from, 2048), lim = pk / 100;
    uint32_t i, last = from;
    for (i = from; i < frames; i++)
        if (abs(b[2u * i]) > lim || abs(b[2u * i + 1u]) > lim)
            last = i;
    return last - from;
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
    cur.p[P_ENS] = 0;                                   /* the effects off: the oscillators alone */
    cur.p[P_VIB] = 0;                                   /* the LFO quiet */
    cur.p[P_PWM] = 0;
    cur.p[P_LDLY] = 0;
    cur.p[P_FLGA] = 0;
    cur.p[P_DMIX] = 0;
    cur.p[P_RVMIX] = 0;
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

    /* the effects: ensemble (stereo, pitch kept, depth 0 still), flanger (bounded, its tail ends), delay (on time, feedback,
     * exact silence, a stop) */
    {
        double c1, c2, c3, rms_off, rms_ens;
        uint32_t k, same = 1;
        silence();
        voice(0, 100, 50, 0, 0);
        fm1_in.notes = 1u << 7;                                     /* C4 */
        host_render(buf, ONE_S);
        rms_off = rms_l(buf + 2u * 8192u, 32768);
        cur.p[P_ENS] = 2;                                           /* II, ENS DEPTH 60 %, ENS RATE ~0.6 / 6 Hz */
        cur.p[P_ENSD] = 60;
        cur.p[P_ENSR] = 52;
        snd_update();
        host_render(buf, ONE_S);
        rms_ens = rms_l(buf + 2u * 8192u, 32768);
        c2 = corr_lr(buf + 2u * 8192u, 32768);
        printf("  (ensemble II: L/R correlation %.3f, level %.2f x off)\n", c2, rms_ens / rms_off);
        check("ensemble II: stereo (L and R differ, correlation < 0.95)", c2 < 0.95 && c2 > 0.0);
        check("ensemble II: the level kept (0.7 .. 1.3 x off)", rms_ens / rms_off > 0.7 && rms_ens / rms_off < 1.3);
        {   /* (zero crossings do not count a chorus's pitch: the fundamental stands well over 3 % either side) */
            double at = tone_db(buf + 2u * 8192u, 32768, 261.63), lo = tone_db(buf + 2u * 8192u, 32768, 261.63 * 0.97),
                   hi = tone_db(buf + 2u * 8192u, 32768, 261.63 * 1.03);
            printf("  (ensemble II at C4: %.1f dB over -3 %%, %.1f dB over +3 %%)\n", at - lo, at - hi);
            /* (a chorus wobbles the pitch ~+-1.6 %: its sidebands reach +-3 %; the fundamental leads them clearly) */
            check("ensemble II: the pitch kept (C4 > 10 dB over +-3 %)", at - lo > 10.0 && at - hi > 10.0);
        }
        cur.p[P_ENSD] = 0;                                          /* no sweep: three still taps, mono */
        snd_update();
        host_render(buf, 4096);
        host_render(buf, 4096);
        for (k = 0; k < 4096u; k++)
            same &= buf[2u * k] == buf[2u * k + 1u];
        check("ensemble at ENS DEPTH 0: no movement, left = right", same && peak(buf, 4096) > 1000);
        cur.p[P_ENSD] = 60;
        cur.p[P_ENS] = 1;
        snd_update();
        host_render(buf, ONE_S);
        c1 = corr_lr(buf + 2u * 8192u, 32768);
        cur.p[P_ENS] = 3;
        snd_update();
        host_render(buf, ONE_S);
        c3 = corr_lr(buf + 2u * 8192u, 32768);
        printf("  (L/R correlation: I %.3f, II %.3f, III %.3f)\n", c1, c2, c3);
        check("ensemble III deeper than I (less correlated)", c3 < c1);
        fm1_in.notes = 0;
        host_render(buf, ONE_S);
        check("ensemble: the tail ends in exact silence", peak(buf + 2u * (ONE_S - 2048u), 2048) == 0);

        cur.p[P_ENS] = 4;                                           /* FLANGE: the flanger alone, >= 60 % */
        cur.p[P_FLGA] = 0;
        snd_update();
        fm1_in.notes = 1u << 7;
        host_render(buf, ONE_S);
        check("FLANGE: the flanger runs (stereo, opposite sweeps)", corr_lr(buf + 2u * 8192u, 32768) < 0.99);
        cur.p[P_FLGA] = 100;
        cur.p[P_RES] = 90;
        snd_update();
        voice(100, 100, 100, 0, 0);
        cur.p[P_ENS] = 4;
        cur.p[P_FLGA] = 100;
        snd_update();
        fm1_in.notes = (1u << 27) - 1u;
        host_render(buf, ONE_S);
        check("FLANGER 100 %, all keys: bounded", peak(buf, ONE_S) <= 32767 && peak(buf, ONE_S) > 1000);
        fm1_in.notes = 0;
        host_render(buf, ONE_S);
        check("FLANGER 100 %: the tail ends in exact silence", peak(buf + 2u * (ONE_S - 2048u), 2048) == 0);
        silence();
        host_render(buf, ONE_S);                                    /* (the delay's quiet pass after the stop) */
        host_render(buf, 2048);
    }
    {   /* ping-pong: the first repeat on the left, the next on the right; DLY SYNC from the tempo, and from MIDI clock */
        static int32_t pp[2u * 3u * FS];
        uint32_t d = pv_ms(K_DLY, 64) * (FS / 100u) / 10u;
        int32_t l1, r1, l2, r2;
        voice(0, 100, 0, 0, 0);
        silence();
        host_render(buf, ONE_S);
        host_render(buf, 2048);
        cur.p[P_DMIX] = 100;
        cur.p[P_DFB] = 70;
        cur.p[P_DTIME] = 64;
        cur.p[P_DTONE] = 100;
        cur.p[P_DMODE] = 1;
        snd_update();
        fm1_in.notes = 1u << 7;
        host_render(pp, 1024);
        fm1_in.notes = 0;
        host_render(pp + 2u * 1024u, 3u * d);
        l1 = peak_ch(pp + 2u * d, 1024, 0);
        r1 = peak_ch(pp + 2u * d, 1024, 1);
        l2 = peak_ch(pp + 2u * 2u * d, 1024, 0);
        r2 = peak_ch(pp + 2u * 2u * d, 1024, 1);
        printf("  (ping-pong: first repeat L %d R %d, second L %d R %d)\n", l1, r1, l2, r2);
        check("PING-PONG: the first repeat on the left only", l1 > 1000 && r1 == 0);
        check("PING-PONG: the second on the right only", r2 > 1000 && l2 == 0);
        cur.p[P_DMODE] = 0;
        cur.p[P_DSYNC] = 6;                               /* 1/8 at 120 BPM: 11 025 samples */
        set.bpm = 120;
        snd_update();
        check("DLY SYNC 1/8 at 120 BPM: 11 025 samples", fxp.dly_len == 11025);
        set.bpm = 90;
        snd_update();
        check("DLY SYNC follows the tempo (1/8 at 90 BPM: 14 700)", fxp.dly_len == 14700);
        {
            uint32_t t;
            for (t = 0; t < 30u; t++) {                   /* MIDI clock in, a tick every 1 088 samples */
                host_midi(0xF8, 0, 0);
                host_render(buf, 1088);
            }
            printf("  (DLY SYNC 1/8 under MIDI clock: %d samples, 12 ticks are %d)\n", fxs.dly_len_ext, 12 * 1088);
            check("DLY SYNC under MIDI clock: its tempo (1/8 = 12 ticks)", abs(fxs.dly_len_ext - 12 * 1088) < 12 * 8);
            host_render(buf, ONE_S);                      /* (the clock gone) */
        }
        cur.p[P_DSYNC] = 0;
        set.bpm = 120;
        voice(0, 100, 0, 0, 0);
        silence();
        host_render(buf, ONE_S);                          /* (the delay's quiet pass after the stop) */
        host_render(buf, 2048);
    }
    {   /* FX 3: FX pressed a third time; its two rows, two empty; what the reader says */
        tap(B_FX);
        tap(B_FX);
        tap(B_FX);
        check("FX three times: FX 3 (delay mode and sync)", ui.page == PG_FX3);
        host_turn(EN_K1, 1);
        host_turn(EN_K2, 6);
        host_turn(EN_K3, 35);
        host_turn(EN_K4, 20);
        host_frame();
        host_png("build/shots/fx3.png");
        check("FX 3: KNOB 1 DLY MODE, 2 DLY SYNC, 3 REVERB, 4 REV SIZE",
              cur.p[P_DMODE] == 1 && cur.p[P_DSYNC] == 6 && cur.p[P_RVMIX] == 35 && cur.p[P_RVSIZE] == 70);
        lint("FX 3");
        tap(B_FX);
        check("FX again: back to FX 1", ui.page == PG_FX1);
        cur.p[P_DMODE] = 0;
        cur.p[P_DSYNC] = 0;
        cur.p[P_RVMIX] = 0;
        cur.p[P_RVSIZE] = 50;
        tap(B_HOME);
    }
    {   /* the reverb: a tail after a burst, longer for a larger REV SIZE, stereo, exact silence, a stop, no stale tail */
        static int32_t rv[2u * 6u * FS];
        uint32_t t20, t80;
        double c;
        voice(0, 100, 0, 0, 0);
        silence();
        host_render(buf, ONE_S);
        host_render(buf, 2048);
        cur.p[P_RVMIX] = 60;
        cur.p[P_RVSIZE] = 20;
        snd_update();
        fm1_in.notes = 1u << 7;
        host_render(rv, 2048);
        fm1_in.notes = 0;
        host_render(rv + 2u * 2048u, 6u * FS / CTL * CTL - 2048u);
        t20 = tail_time(rv, 6u * FS / CTL * CTL, 4096u);
        c = corr_lr(rv + 2u * 8192u, 8192);
        printf("  (reverb: REV SIZE 20 falls 40 dB in %.2f s; L/R correlation of the tail %.2f)\n", t20 / (double)FS, c);
        check("REVERB: a tail after the note", t20 > FS / 10u);
        check("REVERB: the tail is stereo (L/R correlation < 0.8)", c < 0.8);
        check("REVERB: the tail ends in exact silence", peak(rv + 2u * (6u * FS / CTL * CTL - 4096u), 4096) == 0);
        cur.p[P_RVSIZE] = 80;
        snd_update();
        fm1_in.notes = 1u << 7;
        host_render(rv, 2048);
        fm1_in.notes = 0;
        host_render(rv + 2u * 2048u, 6u * FS / CTL * CTL - 2048u);
        t80 = tail_time(rv, 6u * FS / CTL * CTL, 4096u);
        printf("  (reverb: REV SIZE 80 falls 40 dB in %.2f s)\n", t80 / (double)FS);
        check("REV SIZE 80 decays longer than 20 (x 2 at least)", t80 > 2u * t20);
        fm1_in.notes = 1u << 7;                           /* a stop: silent at once */
        host_render(buf, 4096);
        fm1_in.notes = 0;
        host_render(buf, 2048);
        app_audio_stop();
        host_render(buf, 2048);
        check("app_audio_stop: the reverb silent at once", peak(buf, 2048) == 0);
        host_render(buf, 8192);                           /* (its quiet pass) */
        cur.p[P_RVSIZE] = 50;
        snd_update();
        fm1_in.notes = 1u << 7;                           /* REVERB to 0, then on again much later: no old tail */
        host_render(buf, 4096);
        fm1_in.notes = 0;
        host_render(buf, 2048);
        cur.p[P_RVMIX] = 0;
        snd_update();
        host_render(rv, 6u * FS / CTL * CTL);
        host_render(rv, 6u * FS / CTL * CTL);
        host_render(rv, ONE_S);                           /* 13 s: past its tail */
        cur.p[P_RVMIX] = 60;
        snd_update();
        host_render(buf, 4096);
        check("REVERB off past its tail, then on: no old sound in it", peak(buf, 4096) == 0 && fxs.rv_tail > 0u);
        cur.p[P_RVMIX] = 0;                               /* notes played while REVERB is 0 go nowhere near it: just
                                                           * before it stops running, they would be frozen in its lines */
        snd_update();
        host_render(rv, 6u * FS / CTL * CTL);
        host_render(rv, 5u * FS / CTL * CTL);
        fm1_in.notes = 1u << 7;
        host_render(buf, 8192);
        fm1_in.notes = 0;
        host_render(rv, 2u * FS / CTL * CTL);             /* (past the 12 s: it stopped) */
        cur.p[P_RVMIX] = 60;
        snd_update();
        host_render(buf, 4096);
        check("notes played with REVERB at 0 leave nothing in it for later", peak(buf, 4096) == 0);
        cur.p[P_RVMIX] = 0;
        snd_update();
        silence();
        host_render(buf, ONE_S);
        host_render(buf, 2048);
        nrpn_in(91, 8192, 1);                             /* 14-bit: NRPN 0:91 (REVERB) */
        check("REVERB answers NRPN 0:91 (50 + a fine part)", cur.p[P_RVMIX] == 50 && cur.f[P_RVMIX] <= 2u);
        cur.p[P_RVMIX] = 0, cur.f[P_RVMIX] = 0;
        snd_update();
        voice(0, 100, 0, 0, 0);
    }
    {   /* the delay: a short burst, then its echoes */
        static int32_t big[2u * 3u * FS];
        uint32_t d = pv_ms(K_DLY, 64) * (FS / 100u) / 10u, k, first = 0, burst_end;
        int32_t p1, p2;
        voice(0, 100, 0, 0, 0);
        cur.p[P_DMIX] = 100;
        cur.p[P_DFB] = 0;
        cur.p[P_DTIME] = 64;                                        /* ~102 ms */
        cur.p[P_DTONE] = 100;                                       /* (open: the echoes keep their level) */
        snd_update();
        fm1_in.notes = 1u << 7;
        host_render(big, 1024);
        fm1_in.notes = 0;
        host_render(big + 2u * 1024u, 3u * FS / CTL * CTL - 1024u);
        for (burst_end = 1024; burst_end < d; burst_end++) {   /* the note's end: 256 silent samples in a row (not a
                                                                * zero crossing in its release) */
            uint32_t z = 0;
            while (z < 256u && big[2u * (burst_end + z)] == 0 && big[2u * (burst_end + z) + 1u] == 0)
                z++;
            if (z == 256u)
                break;
        }
        for (k = burst_end; k < 3u * FS / CTL * CTL && !first; k++)
            if (big[2u * k] != 0)
                first = k;
        printf("  (delay %u samples: the burst ended at %u, the first echo at %u)\n", d, burst_end, first);
        check("delay: the echo arrives DLY TIME after the note (+-1 ms)", first > d - 45u && first < d + 45u);
        check("delay FEEDBACK 0: one echo, then silence", peak(big + 2u * (2u * d + 2048u), 4096) == 0);
        silence();
        host_render(buf, ONE_S);
        host_render(buf, 2048);
        cur.p[P_DFB] = 50;
        snd_update();
        fm1_in.notes = 1u << 7;
        host_render(big, 1024);
        fm1_in.notes = 0;
        host_render(big + 2u * 1024u, 3u * FS / CTL * CTL - 1024u);
        p1 = peak(big + 2u * d, 1024);
        p2 = peak(big + 2u * 2u * d, 1024);
        printf("  (FEEDBACK 50 %%: first echo %d, second %d)\n", p1, p2);
        check("delay FEEDBACK 50 %: a second echo at half the level (+-20 %)", p2 > p1 * 4 / 10 && p2 < p1 * 6 / 10);
        check("delay FEEDBACK 50 %: the tail ends in exact silence within 3 s",
              peak(big + 2u * (3u * FS / CTL * CTL - 4096u), 4096) == 0);
        cur.p[P_DFB] = 90;
        snd_update();
        fm1_in.notes = 1u << 7;
        host_render(buf, 2048);
        fm1_in.notes = 0;
        host_render(buf, 8192);
        app_audio_stop();
        host_render(buf, 4096);
        check("app_audio_stop: the delay's tail silent at once", peak(buf, 4096) == 0);
        silence();
        cur.p[P_DMIX] = 0;
        snd_update();
        fm1_in.notes = 1u << 7;
        host_render(buf, ONE_S);
        host_render(buf, 1024);
        fm1_in.notes = 0;
        host_render(buf, ONE_S);
        check("DLY MIX 0: no echo", peak(buf + 2u * 4096u, ONE_S - 4096u) == 0);
        voice(0, 100, 0, 0, 0);
        silence();
    }

    /* the LFO: vibrato (depth in the audio, rate), its fade-in, PWM; MIDI: pitch bend, the mod wheel, resets, the CC map */
    {
        int32_t cmin = 0, cmax = 0, last = 0, flips = 0;
        uint32_t b, k, rate_mhz, pmin = 0xFFFFFFFFu, pmax = 0;
        double pr;
        silence();
        voice(0, 100, 0, 0, 0);
        cur.p[P_VIB] = 100;
        cur.p[P_LRATE] = 98;                                        /* ~5 Hz */
        snd_update();
        rate_mhz = pv_ms(K_HZ, 98);
        fm1_in.notes = 1u;                                          /* F3 */
        host_render(buf, 4096);
        for (b = 0; b < FS / CTL; b++) {                            /* a second, block by block */
            host_render(buf + 2u * b * CTL, CTL);
            cmin = vox.cents_pub < cmin ? vox.cents_pub : cmin;
            cmax = vox.cents_pub > cmax ? vox.cents_pub : cmax;
            if (b && (vox.cents_pub >= 0) != (last >= 0))
                flips++;
            last = vox.cents_pub;
        }
        printf("  (VIBRATO 100 %%: %d .. %+d cents, %d crossings in 1 s at %.2f Hz)\n", cmin, cmax, flips, rate_mhz / 1000.0);
        check("VIBRATO 100 %: +-50 cents", cmin <= -48 && cmin >= -50 && cmax >= 48 && cmax <= 50);
        check("LFO RATE: the vibrato's rate (+-10 %)", fabs(flips / 2.0 - rate_mhz / 1000.0) < rate_mhz / 10000.0 + 0.5);
        pr = period_ratio(buf, FS / CTL * CTL);
        printf("  (the longest period over the shortest: %.4f, +-50 cents is 1.0595)\n", pr);
        check("VIBRATO 100 %: heard (period ratio 1.04 .. 1.08)", pr > 1.04 && pr < 1.08);
        check("VIBRATO: centred (F3 on average within 0.5 Hz)", fabs(pitch(buf, FS / CTL * CTL) - 174.61) < 0.5);
        fm1_in.notes = 0;
        silence();
        host_render(buf, ONE_S);                                    /* (the envelope at rest: a fresh attack) */

        cur.p[P_LDLY] = 25;                                         /* DELAY 1 s */
        snd_update();
        fm1_in.notes = 1u;
        cmax = 0;
        for (b = 0; b < (FS / 5u) / CTL; b++) {                     /* the first 200 ms */
            host_render(buf, CTL);
            cmax = abs(vox.cents_pub) > cmax ? abs(vox.cents_pub) : cmax;
        }
        check("DELAY 1 s: little vibrato in the first 200 ms (<= 25 %)", cmax <= 13);
        host_render(buf, ONE_S);
        cmax = 0;
        for (b = 0; b < (FS / 4u) / CTL; b++) {
            host_render(buf, CTL);
            cmax = abs(vox.cents_pub) > cmax ? abs(vox.cents_pub) : cmax;
        }
        check("DELAY 1 s: full vibrato after it", cmax >= 48);
        fm1_in.notes = 0;
        cur.p[P_LDLY] = 0;
        cur.p[P_VIB] = 0;
        snd_update();
        silence();

        cur.p[P_SHAPE] = 66;                                        /* PWM on a square: the width swept */
        cur.p[P_PWM] = 100;
        cur.p[P_LRATE] = 60;
        snd_update();
        fm1_in.notes = 1u << 7;
        for (b = 0; b < (2u * FS) / CTL; b++) {
            host_render(buf, CTL);
            pmin = vox.pw_pub < pmin ? vox.pw_pub : pmin;
            pmax = vox.pw_pub > pmax ? vox.pw_pub : pmax;
        }
        printf("  (PWM 100 %%: the width %.1f .. %.1f %%)\n", pmin / 42949672.96, pmax / 42949672.96);
        check("PWM 100 %: the width swept from ~10 % to ~90 %", pmin < 0.15 * 4294967296.0 && pmax > 0.85 * 4294967296.0);
        fm1_in.notes = 0;
        silence();
        {   /* PWM on a saw does nothing (a saw has no width) */
            static int32_t a[2u * 8192u];
            cur.p[P_SHAPE] = 0;
            cur.p[P_PWM] = 0;
            snd_update();
            fm1_in.notes = 1u << 7;
            host_render(a, 8192);
            silence();
            cur.p[P_PWM] = 100;
            snd_update();
            fm1_in.notes = 1u << 7;
            host_render(buf, 8192);
            check("PWM on a saw: no change", !memcmp(a, buf, sizeof a));
            fm1_in.notes = 0;
            cur.p[P_PWM] = 0;
            snd_update();
            silence();
        }

        host_midi(0xE0, 127, 127);                                  /* bend all the way up: +2 semitones */
        fm1_in.notes = 1u;                                          /* F3 -> G3 */
        host_render(buf, ONE_S);
        check("pitch bend up: +2 semitones (F3 -> G3 196.0 Hz)", fabs(pitch(buf, ONE_S) - 196.0) < 0.5);
        host_midi(0xE0, 0, 0);
        host_render(buf, ONE_S);
        check("pitch bend down: -2 semitones (F3 -> D#3 155.6 Hz)", fabs(pitch(buf, ONE_S) - 155.56) < 0.5);
        host_midi(0xE0, 0, 64);
        host_render(buf, ONE_S);
        check("pitch bend centred: F3 again", fabs(pitch(buf, ONE_S) - 174.61) < 0.5);
        host_midi(0xB0, 1, 127);                                    /* the mod wheel, VIBRATO 0 */
        cmax = 0;
        for (b = 0; b < FS / CTL; b++) {
            host_render(buf, CTL);
            cmax = abs(vox.cents_pub) > cmax ? abs(vox.cents_pub) : cmax;
        }
        check("the mod wheel up: vibrato (+-49 cents) with VIBRATO at 0", cmax >= 47 && cmax <= 50);
        host_midi(0xE0, 127, 127);
        host_midi(0xB0, 121, 0);                                    /* reset all controllers */
        host_render(buf, 1024);
        check("CC121: bend and mod wheel back at rest", vox.bend == 0 && vox.modw == 0 && vox.cents_pub == 0);
        host_midi(0xE0, 127, 127);
        host_render(buf, 64);
        app_audio_stop();
        host_render(buf, 64);
        check("app_audio_stop: the bend forgotten", vox.bend == 0);
        fm1_in.notes = 0;
        silence();

        host_midi(0xB0, 74, 0);                                     /* the CC map, applied by the main loop */
        host_midi(0xB0, 71, 64);
        host_midi(0xB0, 20, 0);
        host_midi(0xB0, 13, 127);
        host_render(buf, CTL);
        host_frame();
        check("CC74 0 / CC71 64 / CC20 0 / CC13 127: CUTOFF 0, RESONANCE 50 %, ENV AMOUNT -100 %, 8' 100 %",
              cur.p[P_CUT] == 0 && cur.p[P_RES] == 50 && cur.p[P_FENV] == -100 && cur.p[P_R8] == 100 && snd.f_cut == 0);
        host_midi(0xB0, 74, 127);
        host_render(buf, CTL);
        host_frame();
        check("CC74 127: CUTOFF all the way up", cur.p[P_CUT] == 127);
        set.g[G_MIDICH] = 2;
        snd_update();
        host_midi(0xB0, 74, 10);                                    /* channel 1: not ours */
        host_render(buf, CTL);
        host_frame();
        check("a CC on another channel: ignored", cur.p[P_CUT] == 127);
        host_midi(0xB1, 74, 10);
        host_render(buf, CTL);
        host_frame();
        check("a CC on ours: applied", cur.p[P_CUT] == 10);
        set.g[G_MIDICH] = 0;
        voice(0, 100, 0, 0, 0);
        for (k = 0; k < 4u; k++)
            host_frame();
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

/* ---------------------------------------------------------------- the arp, chord memory, MIDI out --- */
static int32_t seq_notes[64];
static uint32_t seq_at[64], seq_n, seq_on_blocks;

static void arp_watch(uint32_t blocks)                 /* run, noting each arp note as it starts (and when) */
{
    static int32_t b[2u * CTL];
    static uint32_t clock;
    int32_t last = -2;
    uint32_t i;
    seq_n = seq_on_blocks = 0;
    for (i = 0; i < blocks; i++, clock++) {
        host_render(b, CTL);
        if (arp.note_pub >= 0)
            seq_on_blocks++;
        if (arp.note_pub >= 0 && (arp.note_pub != last || arp.gate_q8 + (CTL << 8) > 0x7FFFFFFFu) && seq_n < 64u &&
            (last < 0 || arp.note_pub != last)) {
            seq_notes[seq_n] = arp.note_pub;
            seq_at[seq_n++] = i;
        }
        last = arp.note_pub;
    }
}

static int seq_is(const int32_t *want, uint32_t n)     /* the first n notes started, as given */
{
    uint32_t i;
    if (seq_n < n)
        return 0;
    for (i = 0; i < n; i++)
        if (seq_notes[i] != want[i])
            return 0;
    return 1;
}

static double seq_step(void)                           /* the mean time between note starts, samples */
{
    return seq_n > 2u ? (double)(seq_at[seq_n - 1u] - seq_at[0]) * CTL / (seq_n - 1u) : 0.0;
}

static int out_take(uint32_t st, uint32_t note)        /* the next MIDI out packet is st/note? */
{
    uint32_t pkt;
    if (!host_midi_out(&pkt))
        return 0;
    return ((pkt >> 8) & 0xFFu) == st && ((pkt >> 16) & 0x7Fu) == note;
}

static void out_flush(void)
{
    uint32_t pkt;
    while (host_midi_out(&pkt))
        ;
}

static void arp_mode(int mode, int rate, int oct, int gate)
{
    set.g[G_ARPM] = (int8_t)mode;
    set.g[G_ARPR] = (int8_t)rate;
    set.g[G_ARPO] = (int8_t)oct;
    set.g[G_ARPG] = (int8_t)gate;
    snd_update();
}

static void sequencer(void)
{
    static const int32_t UP[] = {60, 64, 67, 60, 64, 67}, UP2[] = {60, 64, 67, 72, 76, 79, 60},
                         DOWN[] = {67, 64, 60, 67, 64, 60}, UPDN[] = {60, 64, 67, 64, 60, 64, 67},
                         PLAYED[] = {67, 60, 64, 67, 60, 64};
    const uint32_t CEG = 1u << 7 | 1u << 11 | 1u << 14;   /* C4 E4 G4 */
    uint32_t i, k, one;
    host_nor_erase();
    power_cycle();
    voice(0, 100, 0, 0, 0);
    set.bpm = 120;
    arp_mode(0, 3, 1, 50);                                /* UP, 1/16, 1 octave, gate 50 % */
    tap(B_ARP);
    fm1_in.notes = CEG;
    arp_watch(1400);                                      /* ~1 s */
    printf("  (UP: %d %d %d %d %d %d, a step every %.1f samples, gate %.2f)\n", seq_notes[0], seq_notes[1], seq_notes[2],
           seq_notes[3], seq_notes[4], seq_notes[5], seq_step(), (double)seq_on_blocks / 1400);
    check("arp UP: C E G C E G, the first at once", seq_is(UP, 6) && seq_at[0] == 0u);
    check("arp 1/16 at 120 BPM: a step every 5 512.5 samples (+-32)", fabs(seq_step() - 5512.5) < 32.0);
    check("arp GATE 50 %: on half the time (+-5 %)", fabs((double)seq_on_blocks / 1400 - 0.5) < 0.05);
    for (one = 1, k = 0; k < 200u; k++) {                 /* one note sounds at a time */
        host_render(buf_ctl, CTL);
        one &= (uint32_t)__builtin_popcount(vox.prev_gated[0]) + (uint32_t)__builtin_popcount(vox.prev_gated[1]) +
                   (uint32_t)__builtin_popcount(vox.prev_gated[2]) + (uint32_t)__builtin_popcount(vox.prev_gated[3]) <= 1u;
    }
    check("arp: one note sounds at a time", one);
    fm1_in.notes = 0;
    arp_watch(40);
    arp_mode(0, 3, 2, 50);
    fm1_in.notes = CEG;
    arp_watch(1400);
    check("arp 2 octaves: C E G C' E' G' C", seq_is(UP2, 7));
    fm1_in.notes = 0;
    arp_watch(40);
    arp_mode(1, 3, 1, 50);
    fm1_in.notes = CEG;
    arp_watch(1200);
    check("arp DOWN: G E C G E C", seq_is(DOWN, 6));
    fm1_in.notes = 0;
    arp_watch(40);
    arp_mode(2, 3, 1, 50);
    fm1_in.notes = CEG;
    arp_watch(1400);
    check("arp UP-DOWN: C E G E C E G (the ends once)", seq_is(UPDN, 7));
    fm1_in.notes = 0;
    arp_watch(40);
    arp_mode(4, 3, 1, 50);                                /* PLAYED: G, then C, then E */
    fm1_in.notes = 1u << 14;
    host_render(buf_ctl, CTL);
    fm1_in.notes |= 1u << 7;
    host_render(buf_ctl, CTL);
    fm1_in.notes |= 1u << 11;
    arp_watch(1200);
    check("arp PLAYED: in the order pressed (G C E)", seq_is(PLAYED, 6) || (seq_n >= 6u && seq_notes[1] == 60 &&
                                                                       seq_notes[2] == 64 && seq_notes[3] == 67));
    fm1_in.notes = 0;
    arp_watch(40);
    arp_mode(3, 3, 1, 50);                                /* RANDOM */
    fm1_in.notes = CEG;
    arp_watch(2800);
    {
        int ok = seq_n > 10u, seen[3] = {0, 0, 0};
        for (i = 0; i < seq_n; i++) {
            ok &= seq_notes[i] == 60 || seq_notes[i] == 64 || seq_notes[i] == 67;
            seen[seq_notes[i] == 60 ? 0 : seq_notes[i] == 64 ? 1 : 2] = 1;
        }
        check("arp RANDOM: only the held notes, all of them", ok && seen[0] && seen[1] && seen[2]);
    }
    arp_mode(0, 3, 1, 50);

    /* PLAY: the transport. MIDI Start and clock out (24 a quarter), the arp's steps on every 6th tick (1/16) */
    {
        uint32_t b, pkt, f8 = 0, fa = 0, fc = 0, ons = 0, offbeat = 0, first_on = 0xFFFFFFFFu, f8_at_start = 0;
        out_flush();
        tap(B_PLAY);
        for (b = 0; b < FS / CTL; b++) {                  /* a second */
            host_render(buf_ctl, CTL);
            while (host_midi_out(&pkt)) {
                uint32_t st = (pkt >> 8) & 0xFFu;
                if (st == 0xFAu)
                    fa = 1, f8_at_start = f8;
                else if (st == 0xF8u)
                    f8++;
                else if ((st & 0xF0u) == 0x90u && (pkt >> 24)) {
                    ons++;
                    offbeat += f8 % 6u != 1u;             /* a step right after ticks 0, 6, 12 ... (6n + 1 counted) */
                    if (first_on == 0xFFFFFFFFu)
                        first_on = b;
                }
            }
        }
        printf("  (PLAY at 120 BPM: MIDI Start %u, %u ticks in a second, %u arp steps, %u off the ticks)\n", fa, f8, ons, offbeat);
        check("PLAY: MIDI Start, then clock out at 24 a quarter (48 a second at 120 BPM)", fa && f8_at_start == 0u &&
                                                                                       f8 >= 47u && f8 <= 49u);
        check("PLAY: the arp from its top at once, every step on a tick (6 ticks a 1/16)", first_on <= 1u && ons >= 7u &&
                                                                                         offbeat == 0u);
        tap(B_HOME);
        host_frame();
        host_png("build/shots/arp_playing.png");
        tap(B_PLAY);
        f8 = 0;
        for (b = 0; b < 400u; b++) {
            host_render(buf_ctl, CTL);
            while (host_midi_out(&pkt)) {
                uint32_t st = (pkt >> 8) & 0xFFu;
                fc |= st == 0xFCu;
                f8 += st == 0xF8u;
            }
        }
        arp_watch(400);
        check("PLAY again: MIDI Stop, no more clock; the arp free-runs on", fc && f8 == 0u && seq_n > 0u && !arp.run_pub);
    }

    /* HOLD: the arp keeps going on a latched chord */
    tap(B_SEQ);
    fm1_in.notes = CEG;
    arp_watch(20);
    fm1_in.notes = 0;
    arp_watch(1400);
    check("HOLD + arp: the latched chord keeps arpeggiating", seq_is(UP, 3) || seq_n >= 6u);
    tap(B_SEQ);
    arp_watch(200);
    check("HOLD off: the arp stops (nothing held)", seq_n == 0u);

    /* MIDI clock: 24 a quarter takes over while it comes in; start, stop, continue */
    fm1_in.notes = CEG;
    tap(B_PLAY);                                          /* (the transport running: still nothing out while following) */
    host_render(buf_ctl, CTL);
    out_flush();
    host_midi(0xFA, 0, 0);                                /* start */
    seq_n = 0;
    {
        uint32_t t, starts = 0, first = 0, last = 0;
        int32_t prev = -2, firstnote = -1;
        for (t = 0; t < 72u; t++) {                       /* 3 quarters, a tick every 1 088 samples (~101 BPM) */
            host_midi(0xF8, 0, 0);
            for (k = 0; k < 34u; k++) {
                host_render(buf_ctl, CTL);
                if (arp.note_pub >= 0 && prev < 0) {
                    if (!starts)
                        first = t * 34u + k, firstnote = arp.note_pub;
                    last = t * 34u + k;
                    starts++;
                }
                prev = arp.note_pub;
            }
        }
        printf("  (MIDI clock: %u steps, one every %.0f samples, the first %d)\n", starts,
               starts > 1u ? (double)(last - first) * CTL / (starts - 1u) : 0.0, firstnote);
        check("MIDI clock: a 1/16 step every 6 ticks (6 528 samples)", starts == 12u &&
                                                                    fabs((double)(last - first) * CTL / 11.0 - 6528.0) < 40.0);
        check("MIDI start: the pattern from its top", firstnote == 60);
        check("MIDI clock shown (CLK)", arp.ext_pub == 1u);
        {
            uint32_t pkt, f8 = 0;
            while (host_midi_out(&pkt))
                f8 += ((pkt >> 8) & 0xFFu) == 0xF8u;
            check("following MIDI clock: no clock sent (no loop), the transport running or not", f8 == 0u);
        }
        tap(B_HOME);
        host_frame();
        host_png("build/shots/arp_clock.png");
    }
    tap(B_PLAY);                                          /* (PLAY under MIDI clock: the arp stops / continues) */
    host_render(buf_ctl, CTL);
    tap(B_PLAY);
    host_render(buf_ctl, CTL);
    host_midi(0xFC, 0, 0);                                /* stop */
    host_midi(0xF8, 0, 0);
    arp_watch(30);
    check("MIDI stop: silent", seq_n == 0u && arp.halt_pub);
    host_midi(0xFB, 0, 0);                                /* continue */
    for (k = 0; k < 12u; k++) {
        host_midi(0xF8, 0, 0);
        arp_watch(34);
    }
    check("MIDI continue: it runs again", !arp.halt_pub);
    arp_watch(800);                                       /* no more ticks: the internal clock after half a second */
    check("MIDI clock gone: the internal tempo again", arp.ext_pub == 0u && seq_n > 0u);
    fm1_in.notes = 0;
    arp_watch(40);
    tap(B_ARP);

    /* chord memory */
    tap(B_REC);
    fm1_in.notes = CEG;
    host_render(buf_ctl, CTL);
    host_frame();
    fm1_in.notes = 0;
    host_render(buf_ctl, CTL);
    host_frame();
    tap(B_REC);
    check("REC, C E G, REC: the chord is 0 4 7", chord_on() && set.chord_n[0] == 3u && set.chord[0][0] == 0 &&
                                                  set.chord[0][1] == 4 && set.chord[0][2] == 7 && str_eq(ui.msg, "CHORD SET: NOTES 3"));
    fm1_in.notes = 1u << 9;                               /* D4 alone */
    host_render(buf_ctl, CTL);
    check("chord memory: D4 plays D F# A", vox.gated_pub[1] == (1u << (62 - 32)) && vox.gated_pub[2] == (1u << (66 - 64) | 1u << (69 - 64)) &&
                                           !vox.gated_pub[0] && !vox.gated_pub[3]);
    fm1_in.notes = 0;
    host_render(buf_ctl, CTL);
    power_cycle();
    check("the chord kept over a power cycle", chord_on() && set.chord[0][2] == 7);
    out_flush();
    fm1_in.notes = 1u << 9;
    host_render(buf_ctl, CTL);
    check("MIDI out with the chord: D F# A on", out_take(0x90, 62) && out_take(0x90, 66) && out_take(0x90, 69));
    fm1_in.notes = 0;
    host_render(buf_ctl, CTL);
    check("MIDI out: their offs", out_take(0x80, 62) && out_take(0x80, 66) && out_take(0x80, 69));
    host_press(B_REC, 1);
    frames(40);
    host_press(B_REC, 0);
    host_frame();
    check("REC held: the chord cleared", !chord_on() && str_eq(ui.msg, "CHORD CLEARED"));

    /* MIDI out: the keys, channel, HOLD, no echo of MIDI in, the arp's notes, a stop */
    out_flush();
    fm1_in.notes = 1u;                                    /* F3 */
    host_render(buf_ctl, CTL);
    check("MIDI out: a key's note-on (F3, channel 1, velocity 100)", out_take(0x90, 53));
    fm1_in.notes = 0;
    host_render(buf_ctl, CTL);
    check("MIDI out: its note-off", out_take(0x80, 53));
    host_midi(0x90, 60, 100);
    host_render(buf_ctl, CTL);
    {
        uint32_t pkt;
        check("MIDI in is not echoed to MIDI out", !host_midi_out(&pkt));
    }
    host_midi(0x80, 60, 0);
    set.g[G_MIDICH] = 3;
    snd_update();
    fm1_in.notes = 1u;
    host_render(buf_ctl, CTL);
    check("MIDI out on MIDI CH 3", out_take(0x92, 53));
    fm1_in.notes = 0;
    host_render(buf_ctl, CTL);
    out_flush();
    set.g[G_MIDICH] = 0;
    snd_update();
    tap(B_SEQ);                                           /* HOLD keeps it on */
    fm1_in.notes = 1u;
    host_render(buf_ctl, CTL);
    fm1_in.notes = 0;
    host_render(buf_ctl, CTL);
    host_render(buf_ctl, CTL);
    check("MIDI out with HOLD: on, no off after the key goes up", out_take(0x90, 53) && !out_take(0x80, 53));
    tap(B_SEQ);
    host_render(buf_ctl, CTL);
    check("MIDI out: HOLD off sends the off", out_take(0x80, 53));
    tap(B_ARP);
    out_flush();
    fm1_in.notes = CEG;
    arp_watch(400);
    {
        uint32_t pkt, ons = 0, offs = 0;
        while (host_midi_out(&pkt)) {
            ons += ((pkt >> 8) & 0xF0u) == 0x90u;
            offs += ((pkt >> 8) & 0xF0u) == 0x80u;
        }
        check("MIDI out with the arp: its notes, on and off (not the chord)", ons >= 2u && ons <= 3u && offs >= 1u);
    }
    app_audio_stop();
    host_render(buf_ctl, CTL);
    check("app_audio_stop: every MIDI out note gets its off", !(arp.out[0] | arp.out[1] | arp.out[2] | arp.out[3]));
    fm1_in.notes = 0;
    tap(B_ARP);
    out_flush();

    /* the screens at their widest: the ARP page stopped at 240 BPM, HOME's flags */
    tap(B_ARP);
    set.bpm = 240;
    tap(B_PLAY);
    host_render(buf_ctl, CTL);
    host_press(B_ARP, 1);
    frames(40);
    host_press(B_ARP, 0);
    frames(90);                                           /* (the message gone: the footer shows) */
    host_png("build/shots/arp_page.png");
    lint("the ARP page, stopped at 240 BPM");
    tap(B_SEQ);
    tap(B_REC);
    msg("CHORD: HOLD 2 OR MORE KEYS");
    tap(B_HOME);
    host_frame();
    host_png("build/shots/home_flags.png");
    lint("HOME with every flag and a long message");
    tap(B_REC);
    tap(B_SEQ);
    tap(B_PLAY);
    host_render(buf_ctl, CTL);
    tap(B_ARP);
    host_nor_erase();
    power_cycle();
}

/* ---------------------------------------------------------------- the screen reader (fm1-reader) --- */
static uint32_t rd_pos;                                  /* how far host_sx_out has been read */
static char rd_text[16][200];
static uint8_t rd_flags[16], rd_cmd[16];
static uint32_t rd_n, rd_bad;                            /* frames read; texts breaking the wording rules */

static void rd_take(void)                                /* the frames sent since the last call */
{
    rd_n = 0;
    while (rd_pos < host_sx_out_len) {
        uint32_t a = rd_pos, b = a, k = 0, i;
        while (b < host_sx_out_len && host_sx_out[b] != 0xF7)
            b++;
        rd_pos = b + 1u;
        if (rd_n >= 16u || host_sx_out[a] != 0xF0 || b - a < 6u)
            continue;
        rd_cmd[rd_n] = host_sx_out[a + 4u];
        rd_flags[rd_n] = host_sx_out[a + 5u];
        for (i = a + 6u; i < b && k + 1u < sizeof rd_text[0]; i++)
            rd_text[rd_n][k++] = host_sx_out[i] ? (char)host_sx_out[i] : '|';   /* (the HELLO's 00 shown as |) */
        rd_text[rd_n][k] = 0;
        if (rd_cmd[rd_n] == 16u) {                       /* a SAY: ASCII, <= 160, none of the symbols RULES.md bars */
            const char *t = rd_text[rd_n];
            int bad = k > 160u || !k;
            for (i = 0; t[i]; i++)
                bad |= t[i] < 32 || t[i] > 126 || str_len("/|><*_=#[]{}~^@&$") != str_len("/|><*_=#[]{}~^@&$") ||
                       (t[i] == '/' || t[i] == '|' || t[i] == '>' || t[i] == '<' || t[i] == '*' || t[i] == '_' ||
                        t[i] == '=' || t[i] == '#' || t[i] == '[' || t[i] == ']' || t[i] == '{' || t[i] == '}' ||
                        t[i] == '~' || t[i] == '^' || t[i] == '@' || t[i] == '&' || t[i] == '$' || t[i] == '+');
            if (bad) {
                printf("  (against the rules: \"%s\")\n", t);
                rd_bad++;
            }
        }
        rd_n++;
    }
}

static int rd_has(const char *start, uint32_t flags)     /* a SAY beginning so, with those flags, was among them */
{
    uint32_t i;
    for (i = 0; i < rd_n; i++)
        if (rd_cmd[i] == 16u && rd_flags[i] == flags && !strncmp(rd_text[i], start, strlen(start)))
            return 1;
    return 0;
}

static void rd_frames(uint32_t n)                        /* frames, collecting what was said */
{
    static char all[16][200];
    static uint8_t af[16], ac[16];
    uint32_t got = 0, i;
    while (n--) {
        host_frame();
        rd_take();
        for (i = 0; i < rd_n && got < 16u; i++, got++) {
            memcpy(all[got], rd_text[i], sizeof all[0]);
            af[got] = rd_flags[i];
            ac[got] = rd_cmd[i];
        }
    }
    for (i = 0; i < got; i++) {
        memcpy(rd_text[i], all[i], sizeof all[0]);
        rd_flags[i] = af[i];
        rd_cmd[i] = ac[i];
    }
    rd_n = got;
}

static void rd_hello(void)
{
    static const uint8_t HELLO[] = {0x7D, 0x53, 0x52, 0x01, 0x01};
    host_sysex_in(HELLO, sizeof HELLO);
}

static void reader(void)
{
    static const uint8_t BYE[] = {0x7D, 0x53, 0x52, 0x02}, WHERE[] = {0x7D, 0x53, 0x52, 0x03},
                         UPDATE[] = {0x00, 0x59, 0x11, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t ANSWER[] = {0xF0, 0x7D, 0x53, 0x52, 0x01, 0x01, 'S', 'O', 'L', 'I', 'N', 'A', 'E', 'S',
                                     'Q', 'U', 'E', ' ', '0', '.', '1', 0x00, 0xF7};
    uint32_t i, says;
    host_nor_erase();
    power_cycle();
    host_sx_out_len = rd_pos = 0;
    host_turn(EN_K1, 3);
    tap(B_ENV);
    frames(20);
    check("reader: silent until a HELLO (nothing sent)", host_sx_out_len == 0u);
    host_sysex_in(UPDATE, sizeof UPDATE);                 /* the update protocol's frame: not the app's */
    host_frame();
    check("reader: another SysEx frame is left for the update entry", host_sx_ready && host_sx_out_len == 0u);
    host_sx_ready = 0;
    tap(B_HOME);
    rd_hello();
    host_frame();
    check("reader: HELLO answered, byte for byte (F0 7D 53 52 01 01 SOLINAESQUE 0.1 00 F7)",
          host_sx_out_len >= sizeof ANSWER && !memcmp(host_sx_out, ANSWER, sizeof ANSWER) && !host_sx_ready);
    rd_frames(10);
    printf("  (HELLO, then: \"%s\" ...)\n", rd_n > 1u ? rd_text[1] : "");
    check("reader: a new reader hears the whole screen (HOME: the preset, edited, the drawbars, the mode)",
          rd_has("home. factory 1, classic, edited. 16 foot 60 percent", SR_NOW) && strstr(rd_text[1], "retrigger") != 0);

    tap(B_ENV);
    rd_frames(20);
    check("reader: a page: its name and four values", rd_has("envelope page. attack", SR_NOW));
    host_turn(EN_K1, 2);
    rd_frames(2);
    check("reader: a knob: nothing while it is still moving", rd_n == 0u);
    rd_frames(10);
    printf("  (knob: \"%s\")\n", rd_n ? rd_text[0] : "");
    check("reader: a knob: name and value once it settles", rd_n == 1u && rd_has("attack ", SR_NOW) &&
                                                          strstr(rd_text[0], "milliseconds") != 0);
    for (says = 0, i = 0; i < 70u; i++) {                 /* turning for 1.1 s: said every ~half second meanwhile */
        host_turn(EN_K1, (i & 1u) ? 1 : -1);
        rd_frames(1);
        says += rd_n;
    }
    printf("  (turning for 1.1 s: said %u times)\n", says);
    check("reader: a knob turned for 1.1 s: said every ~half second meanwhile (2)", says == 2u);
    rd_frames(20);
    rd_hello();
    tap(B_SEQ);
    rd_frames(2);
    check("reader: a message: \"hold on\"", rd_has("hold on", SR_NOW));
    tap(B_SEQ);
    rd_frames(20);
    tap(B_HOME);
    rd_frames(30);
    host_turn(EN_PRESET, 2);
    rd_frames(20);
    check("reader: a preset: \"factory 3, violins\"", rd_has("factory 3, violins", SR_NOW));
    host_turn(EN_ALGO, 1);
    rd_frames(5);
    check("reader: ALGORITHM: \"ensemble 3\"", rd_has("ensemble 3", SR_NOW));

    rd_hello();
    tap(B_SAVE);                                          /* SAVE: the screen, then each edit */
    rd_frames(30);
    check("reader: SAVE: the slot, the source, the name, how to edit", rd_has("save to user 1, now holding init.", SR_NOW));
    host_turn(EN_K4, 1);
    rd_frames(12);
    check("reader: SAVE: KNOB 4 -> \"name empty\"", rd_has("name empty", SR_NOW));
    host_turn(EN_K2, 3);
    rd_frames(12);
    check("reader: SAVE: a letter -> \"position 1, letter C\"", rd_has("position 1, letter C", SR_NOW));
    host_turn(EN_ALGO, 3);
    rd_frames(12);
    check("reader: SAVE: ALGORITHM -> \"from factory 3, violins. name violins\"", rd_has("from factory 3, violins. name violins", SR_NOW));
    flash_ok = 0;
    tap(B_SAVE);
    rd_frames(4);
    check("reader: SAVE FAILED: a warning (NOW + KEEP)", rd_has("save failed", SR_NOW | SR_KEEP));
    flash_ok = 1;
    rd_frames(30);

    for (i = 0; i < NPAGES; i++) {                        /* WHERE on every page: the whole page, within the rules */
        rd_hello();
        ui.page = (uint8_t)i;
        rd_frames(30);
        host_sysex_in(WHERE, sizeof WHERE);
        rd_frames(4);
        if (!rd_has(i == PG_FX1 ? "effects 1" : i == PG_FX2 ? "effects 2" : i == PG_ARP ? "arpeggiator page" : "", SR_NOW)) {
            printf("  (WHERE on page %u: nothing)\n", i);
            rd_bad++;
        }
    }
    tap(B_HOME);
    rd_frames(30);
    check("reader: every text ASCII, <= 160 characters, none of the symbols RULES.md bars", rd_bad == 0u);

    host_sx_room = 0;                                     /* the USB queue full: dropped, nothing waits */
    {
        uint32_t d0 = sr.dropped, len0 = host_sx_out_len;
        host_turn(EN_K1, 1);
        frames(20);
        check("reader: the USB queue full: the message dropped, nothing sent, no wait", host_sx_out_len == len0 && sr.dropped > d0);
    }
    host_sx_room = 64;
    rd_frames(20);
    host_sysex_in(BYE, sizeof BYE);
    rd_frames(2);
    host_turn(EN_K1, 1);
    rd_frames(20);
    check("reader: BYE: silent again", rd_n == 0u && !sr_on());
    rd_hello();
    rd_frames(3);
    check("reader: HELLO after BYE: the whole screen again", rd_has("home.", SR_NOW));
    frames(5000u / 16u + 10u);                            /* no HELLO for 5 s: gone */
    check("reader: no HELLO for 5 s: silent", !sr_on());
    host_sx_out_len = rd_pos = 0;
    power_cycle();
}

/* ---------------------------------------------------------------- MIDI control, velocity, aftertouch --- */
static void cc_in(uint32_t cc, uint32_t v)               /* a CC in, taken by the ISR, applied by the main loop */
{
    host_midi(0xB0, cc, v);
    host_render(buf_ctl, CTL);
    host_frame();
}

static int out_next(uint32_t *st, uint32_t *d1, uint32_t *d2)
{
    uint32_t pkt;
    if (!host_midi_out(&pkt))
        return 0;
    *st = (pkt >> 8) & 0xFFu;
    *d1 = (pkt >> 16) & 0x7Fu;
    *d2 = (pkt >> 24) & 0x7Fu;
    return 1;
}

static int sysex_reply(uint32_t cmd, const uint8_t **body, uint32_t *n)   /* the last frame sent, if it is ours */
{
    uint32_t a = 0, b, last = 0xFFFFFFFFu;
    for (b = 0; b < host_sx_out_len; b++)
        if (host_sx_out[b] == 0xF0)
            a = b, last = b;
    if (last == 0xFFFFFFFFu)
        return 0;
    for (b = a; b < host_sx_out_len && host_sx_out[b] != 0xF7; b++)
        ;
    if (host_sx_out[a + 1u] != 0x7D || host_sx_out[a + 2u] != 0x53 || host_sx_out[a + 3u] != 0x4C ||
        host_sx_out[a + 4u] != cmd)
        return 0;
    *body = host_sx_out + a + 5u;
    *n = b - (a + 5u);
    return 1;
}

static int32_t peak_of_note(uint32_t note, uint32_t vel)  /* a MIDI note's level, settled */
{
    static int32_t b[2u * 8192u];
    int32_t pk;
    host_midi(0x90, note, vel);
    host_render(b, 8192);
    pk = peak(b + 2u * 4096u, 4096);
    host_midi(0x80, note, 0);
    host_render(b, 4096);
    return pk;
}

static void midi_control(void)
{
    uint32_t k, ok, st, d1, d2;
    host_nor_erase();
    power_cycle();
    /* every control of the CC map, at both ends of its range */
    for (ok = 1, k = 0; k < NCC; k++) {
        if (CC_TGT[k] == CT_PLAY)
            continue;
        cc_in(CC_NUM[k], 0);
        ok &= cc_get(k) == 0;
        cc_in(CC_NUM[k], 127);
        if (cc_get(k) != 127) {
            printf("  (CC %u: %d at 127)\n", CC_NUM[k], cc_get(k));
            ok = 0;
        }
    }
    check("every CC of the map (44) sets its control across its range", ok && NCC == 44u);
    check("CC 74 / 7 / 106 / 112: cutoff, volume, tempo, aftertouch mode reach their ends",
          cur.p[P_CUT] == 127 && set.g[G_VOL] == 100 && set.bpm == 240u && set.g2[G2_AT] == 3);
    cc_in(109, 127);                                      /* PLAY by CC */
    host_render(buf_ctl, CTL);
    check("CC 109 at 64 and up: the transport runs", arp.run_pub == 1u);
    cc_in(109, 0);
    host_render(buf_ctl, CTL);
    check("CC 109 below 64: stopped", arp.run_pub == 0u);
    host_nor_erase();
    power_cycle();
    out_flush();

    /* MIDI out: a knob sends its CC, MIDI in is not echoed, CC OUT off sends nothing, a preset change only its PC */
    tap(B_EDIT);
    out_flush();
    host_turn(EN_K1, -1);
    host_frame();
    host_render(buf_ctl, CTL);
    check("a knob turned sends its CC (16': CC 12, its value)", out_next(&st, &d1, &d2) && st == 0xB0u && d1 == 12u &&
                                                              d2 == (uint32_t)cc_get(cc_slot[12]) && !out_next(&st, &d1, &d2));
    cc_in(74, 50);
    host_render(buf_ctl, CTL);
    check("a CC received is not sent back", !out_next(&st, &d1, &d2));
    set.g2[G2_CCOUT] = 0;
    host_turn(EN_K1, -1);
    host_frame();
    host_render(buf_ctl, CTL);
    check("CC OUT off: nothing sent", !out_next(&st, &d1, &d2));
    set.g2[G2_CCOUT] = 1;
    tap(B_HOME);
    host_render(buf_ctl, CTL);
    out_flush();
    host_turn(EN_PRESET, 1);
    host_frame();
    host_render(buf_ctl, CTL);
    check("a preset change sends its Program Change only (F2: 1)", out_next(&st, &d1, &d2) && st == 0xC0u && d1 == 1u &&
                                                                 !out_next(&st, &d1, &d2));
    host_midi(0xC0, 2, 0);                                /* Program Change in */
    host_render(buf_ctl, CTL);
    host_frame();
    host_render(buf_ctl, CTL);
    check("Program Change 2 in: F3, not sent back", cur_index == 2u && !out_next(&st, &d1, &d2));
    host_midi(0xC0, 20, 0);
    host_render(buf_ctl, CTL);
    host_frame();
    check("Program Change 20: U05", cur_index == NFACT + 4u);
    host_midi(0xC0, 100, 0);
    host_render(buf_ctl, CTL);
    host_frame();
    check("Program Change past the last preset: ignored", cur_index == NFACT + 4u);
    out_flush();

    /* the SysEx dump */
    {
        static const uint8_t GET[] = {0x7D, 0x53, 0x4C, 0x01}, GETSLOT[] = {0x7D, 0x53, 0x4C, 0x03, 4};
        uint8_t f[8 + SL_BODY + 2];
        const uint8_t *body;
        uint32_t n, len;
        preset_t p;
        preset_load(5);
        host_frame();
        host_sx_out_len = 0;
        host_sysex_in(GET, sizeof GET);
        host_frame();
        host_frame();
        check("SysEx: get the current sound -> 02 and its body (111 bytes)", sysex_reply(2, &body, &n) && n == SL_BODY && n == 111u);
        check("SysEx: the body as specified (format 3, the name, 32 parameters)", body[0] == 3u && !memcmp(body + 1, "BRIGHT", 6) &&
                                                                                body[13] == 32u);
        check("SysEx: a parameter as two 7-bit bytes (16' 40: 168 = 1, 40)", body[14] == 1u && body[15] == 40u);
        check("SysEx: it decodes back to the sound", !sl_decode(body, n, &p) && !memcmp(p.p, cur.p, sizeof p.p) &&
                                                    str_eq(p.name, cur.name));
        p = FACTORY[8];                                   /* a sound in: BRASS, renamed */
        str_cpy(p.name, "FROM DAW", NAME_LEN);
        f[0] = 0x7D, f[1] = 0x53, f[2] = 0x4C, f[3] = 0x02;
        len = 4u + sl_encode(&p, f + 4);
        host_sysex_in(f, len);
        host_frame();
        check("SysEx: a sound in becomes the current sound (NEW)", cur_index == PRESET_NEW && str_eq(cur.name, "FROM DAW") &&
                                                                  !memcmp(cur.p, FACTORY[8].p, sizeof cur.p) &&
                                                                  str_eq(ui.msg, "SOUND RECEIVED"));
        f[len - 1u] ^= 1u;                                /* a bad checksum */
        host_sysex_in(f, len);
        host_frame();
        check("SysEx: a bad checksum changes nothing, says so", str_eq(cur.name, "FROM DAW") && str_eq(ui.msg, "SYSEX SOUND REJECTED"));
        f[3] = 0x04;                                      /* into user slot 8 */
        f[4] = 7;
        len = 5u + sl_encode(&p, f + 5);
        host_sysex_in(f, len);
        host_frame();
        check("SysEx: a slot in is stored (U08)", str_eq(user[7].name, "FROM DAW") && str_eq(ui.msg, "RECEIVED U08"));
        power_cycle();
        check("SysEx: the slot kept over a power cycle", str_eq(user[7].name, "FROM DAW") && !memcmp(user[7].p, FACTORY[8].p, sizeof p.p));
        host_sx_out_len = 0;
        host_sysex_in(GETSLOT, sizeof GETSLOT);
        host_frame();
        host_frame();
        check("SysEx: get slot 4 -> 04 4 and its body", sysex_reply(4, &body, &n) && body[0] == 4u &&
                                                        !sl_decode(body + 1, n - 1u, &p) && str_eq(p.name, user[4].name));
        host_sx_room = 0;                                 /* no room: the reply waits, then goes */
        host_sx_out_len = 0;
        host_sysex_in(GET, sizeof GET);
        frames(10);
        check("SysEx: no room in the queue: the reply waits", host_sx_out_len == 0u && sl.pending);
        host_sx_room = 64;
        frames(2);
        check("SysEx: room again: the reply goes", host_sx_out_len > 0u && !sl.pending);
        host_sx_room = 0;
        host_sysex_in(GET, sizeof GET);
        frames(80);
        check("SysEx: nobody reads it for a second: given up", !sl.pending);
        host_sx_room = 64;
        host_sx_out_len = 0;
    }

    /* velocity: OFF, LEVEL, FILTER; the panel plays at 100 */
    host_nor_erase();
    power_cycle();
    voice(0, 100, 0, 0, 0);
    {
        int32_t a, b;
        int32_t i1, i2;
        set.g2[G2_VEL] = 0;
        snd_update();
        a = peak_of_note(60, 127);
        b = peak_of_note(60, 32);
        check("VELOCITY OFF: hard and soft notes alike (an organ, within 2 %)", abs(a - b) * 50 < a && a > 1000);
        set.g2[G2_VEL] = 1;
        snd_update();
        a = peak_of_note(60, 127);
        b = peak_of_note(60, 32);
        printf("  (VELOCITY LEVEL: 127 -> %d, 32 -> %d, %.2f x)\n", a, b, (double)a / b);
        check("VELOCITY LEVEL: 127 is 4 x 32 (+-10 %)", fabs((double)a / b - 4.0) < 0.4);
        set.g2[G2_VEL] = 2;
        snd_update();
        cur.p[P_CUT] = 60;
        snd_update();
        host_midi(0x90, 60, 127);
        host_render(buf_ctl, CTL);
        i1 = flt.idx_pub;
        host_midi(0x80, 60, 0);
        host_midi(0x90, 60, 30);
        host_render(buf_ctl, CTL);
        i2 = flt.idx_pub;
        host_midi(0x80, 60, 0);
        printf("  (VELOCITY FILTER: 127 -> %.1f, 30 -> %.1f cutoff steps)\n", i1 / 256.0, i2 / 256.0);
        check("VELOCITY FILTER: harder notes open the filter (127 vs 30: ~23 steps)", i1 - i2 > 20 * 256 && i1 - i2 < 26 * 256);
        fm1_in.notes = 1u;
        host_render(buf_ctl, CTL);
        fm1_in.notes = 0;
        check("the panel keys play at velocity 100 (the filter as set)", vox.last_vel == 100u && flt.idx_pub == 60 * 256);
        set.g2[G2_VEL] = 0;
        snd_update();
        silence();
    }
    /* aftertouch: vibrato, cutoff; channel and polyphonic; it goes with the note; OFF */
    {
        int32_t cmax = 0;
        uint32_t b;
        set.g2[G2_AT] = 1;                                /* VIBRATO, AT DEPTH 50 %: +-25 cents at full */
        set.g2[G2_ATD] = 50;
        snd_update();
        host_midi(0x90, 60, 100);
        host_midi(0xD0, 127, 0);
        for (b = 0; b < FS / CTL; b++) {
            host_render(buf_ctl, CTL);
            cmax = abs(vox.cents_pub) > cmax ? abs(vox.cents_pub) : cmax;
        }
        check("AFTERTOUCH VIBRATO, channel pressure 127, depth 50 %: +-25 cents", cmax >= 23 && cmax <= 25);
        host_midi(0xD0, 0, 0);
        set.g2[G2_AT] = 2;                                /* CUTOFF: +32 steps at full */
        snd_update();
        cur.p[P_CUT] = 40;
        snd_update();
        host_render(buf, 4096);
        host_midi(0xA0, 60, 127);                         /* polyphonic, on the held note */
        host_render(buf, 4096);
        check("AFTERTOUCH CUTOFF, poly pressure 127 on the held note: +32 steps (127/128 of it)", abs(flt.idx_pub - 72 * 256) <= 256);
        host_midi(0x80, 60, 0);                           /* the note goes: its pressure too */
        host_render(buf, 4096);
        check("poly pressure goes with its note", vox.pat_max == 0u && flt.idx_pub < 41 * 256);
        set.g2[G2_AT] = 0;
        snd_update();
        host_midi(0x90, 60, 100);
        host_midi(0xD0, 127, 0);
        host_render(buf, 4096);
        check("AFTERTOUCH OFF: pressure does nothing", flt.idx_pub == 40 * 256 && vox.cents_pub == 0);
        host_midi(0xD0, 0, 0);
        host_midi(0x80, 60, 0);
        silence();
    }
    /* GLOBAL 2: GLO twice, stored on leaving */
    tap(B_GLO);
    tap(B_GLO);
    check("GLO twice: GLOBAL 2 (MIDI)", ui.page == PG_MIDI);
    host_turn(EN_K1, 3);
    host_frame();
    host_png("build/shots/global2.png");
    tap(B_HOME);
    power_cycle();
    check("GLOBAL 2 stored: VELOCITY BOTH after a power cycle", set.g2[G2_VEL] == 3);
    host_nor_erase();
    power_cycle();
}

/* ---------------------------------------------------------------- 14-bit NRPN --- */
static void nrpn_in(uint32_t num, uint32_t v14, int lsb)  /* NRPN 0:num, data entry MSB (and LSB) */
{
    host_midi(0xB0, 99, 0);
    host_midi(0xB0, 98, num);
    host_midi(0xB0, 6, v14 >> 7);
    if (lsb)
        host_midi(0xB0, 38, v14 & 127u);
    host_render(buf_ctl, CTL);
    host_frame();
}

static void nrpn(void)
{
    uint32_t st, d1, d2;
    host_nor_erase();
    power_cycle();
    voice(0, 100, 0, 0, 0);
    out_flush();
    host_frame();                                         /* (what voice() changed goes out first) */
    host_render(buf_ctl, CTL);
    host_render(buf_ctl, CTL);
    out_flush();
    nrpn_in(74, 8192, 1);                                 /* CUTOFF, half way */
    host_render(buf_ctl, CTL);
    {
        int got = out_next(&st, &d1, &d2);
        if (got)
            printf("  (sent after the NRPN: %02X %u %u)\n", st, d1, d2);
        check("an NRPN received is not sent back", !got);
    }
    printf("  (NRPN 0:74 = 8192: CUTOFF %d + %u/128)\n", cur.p[P_CUT], cur.f[P_CUT]);
    check("NRPN 0:74 half scale: CUTOFF between steps (63 + ~64/128)", cur.p[P_CUT] == 63 && cur.f[P_CUT] >= 63u &&
                                                                    cur.f[P_CUT] <= 66u);
    fm1_in.notes = 1u;
    host_render(buf, 1024);
    check("the filter takes the fine part (cutoff index 63.5, not a step)", abs(flt.idx_pub - (63 * 256 + cur.f[P_CUT] * 2)) <= 2);
    fm1_in.notes = 0;
    host_render(buf, 1024);
    nrpn_in(74, 16383, 1);
    check("NRPN full scale: the top exactly (127, no fine part)", cur.p[P_CUT] == 127 && cur.f[P_CUT] == 0u);
    nrpn_in(74, 0, 1);
    check("NRPN zero: the bottom", cur.p[P_CUT] == 0 && cur.f[P_CUT] == 0u);
    nrpn_in(73, 100u << 7, 0);                            /* ATTACK, data entry MSB only */
    check("NRPN with the MSB only: taken (ATTACK ~99)", cur.p[P_ATK] >= 98 && cur.p[P_ATK] <= 100);
    host_midi(0xB0, 101, 0);                              /* an RPN (pitch bend range): its data entry is not ours */
    host_midi(0xB0, 100, 0);
    host_midi(0xB0, 6, 12);
    host_render(buf_ctl, CTL);
    host_frame();
    check("data entry for an RPN: ignored", cur.p[P_ATK] >= 98);
    {   /* every step of every 14-bit parameter goes out as NRPN and comes back exactly (200-step ones too) */
        uint32_t h, bad = 0;
        for (h = 0; h < NHIRES; h++) {
            uint32_t i = HIRES[h];
            int32_t x;
            int8_t keep = cur.p[i];
            for (x = SP[i].lo; x <= SP[i].hi; x++) {
                cur.p[i] = (int8_t)x, cur.f[i] = 0;
                hr_set(h, hr_get(h));
                bad += cur.p[i] != x || cur.f[i] != 0u;
            }
            cur.p[i] = keep, cur.f[i] = 0;
        }
        check("every step of the 14-bit parameters round-trips over NRPN exactly", bad == 0u);
    }
    nrpn_in(20, 0, 1);                                    /* ENV AMOUNT: -100 at 0, +100 at 16383 */
    check("NRPN on a bipolar parameter (ENV AMOUNT 0 -> -100)", cur.p[P_FENV] == -100 && cur.f[P_FENV] == 0u);
    out_flush();

    {   /* the fine part reaches the engine: between the steps */
        int32_t a0, a1, ah;
        uint32_t m0, m1, mh;
        cur.p[P_ATK] = 80, cur.f[P_ATK] = 0;
        snd_update();
        a0 = snd.atk_inc;
        cur.p[P_ATK] = 81;
        snd_update();
        a1 = snd.atk_inc;
        cur.p[P_ATK] = 80, cur.f[P_ATK] = 64;
        snd_update();
        ah = snd.atk_inc;
        check("ATTACK 80 + 64/128: the attack between steps 80 and 81", ah < a0 && ah > a1);
        cur.p[P_SUS] = 50, cur.f[P_SUS] = 64;
        snd_update();
        check("SUSTAIN 50 + 64/128: 50.5 %", snd.sus > ENV_ONE / 2 && snd.sus < ENV_ONE / 2 + ENV_ONE / 100);
        cur.p[P_DMIX] = 20, cur.f[P_DMIX] = 0;
        snd_update();
        m0 = (uint32_t)fxp.dly_mix;
        cur.f[P_DMIX] = 64;
        snd_update();
        mh = (uint32_t)fxp.dly_mix;
        cur.p[P_DMIX] = 21, cur.f[P_DMIX] = 0;
        snd_update();
        m1 = (uint32_t)fxp.dly_mix;
        check("DLY MIX 20 + 64/128: the mix between 20 and 21 %", mh > m0 && mh < m1);
        {
            char b[24];
            pv_text_f(b, sizeof b, &SP[P_ATK], 80, 64);
            printf("  (ATTACK 80 shows %u ms, 80 + 64/128 shows \"%s\", 81 shows %u ms)\n", pv_ms(K_ATK, 80), b, pv_ms(K_ATK, 81));
            check("the page shows the value between the steps", atoi(b) > (int)pv_ms(K_ATK, 80) && atoi(b) < (int)pv_ms(K_ATK, 81));
        }
        voice(0, 100, 0, 0, 0);
    }

    /* out: a knob turned on a 14-bit parameter sends NRPN 0:<CC>, 99 98 6 38, and no CC */
    tap(B_SCL);
    host_render(buf_ctl, CTL);                            /* (the queue drained first) */
    host_render(buf_ctl, CTL);
    out_flush();
    host_turn(EN_K1, -1);                                 /* CUTOFF 127 -> 126 */
    host_frame();
    host_render(buf_ctl, CTL);
    {
        uint32_t v = hr_get(0), ok = 1;
        ok &= out_next(&st, &d1, &d2) && st == 0xB0u && d1 == 99u && d2 == 0u;
        ok &= out_next(&st, &d1, &d2) && d1 == 98u && d2 == 74u;
        ok &= out_next(&st, &d1, &d2) && d1 == 6u && d2 == v >> 7;
        ok &= out_next(&st, &d1, &d2) && d1 == 38u && d2 == (v & 127u);
        check("CUTOFF turned: NRPN 0:74 out (99 0, 98 74, 6 MSB, 38 LSB), no CC 74", ok && !out_next(&st, &d1, &d2));
    }
    tap(B_HOME);

    /* stored: the fine part over SAVE and a power cycle; version 1 records still read */
    nrpn_in(74, 8192, 1);
    tap(B_SAVE);
    host_turn(EN_PRESET, 2);                              /* U03 */
    host_frame();
    tap(B_SAVE);
    power_cycle();
    check("the fine part kept: SAVE, power cycle (U03 CUTOFF 63 + ~64/128)", cur_index == NFACT + 2u && cur.p[P_CUT] == 63 &&
                                                                           cur.f[P_CUT] >= 63u);
    {   /* a version 1 user record: 16 presets of 40 bytes (no fine parts) */
        static uint8_t raw[4 + 16 * 40];
        uint32_t i;
        memset(raw, 0, sizeof raw);
        raw[0] = 1, raw[1] = 0;                           /* ver 1 */
        raw[2] = (uint8_t)(640 & 0xFF), raw[3] = (uint8_t)(640 >> 8);
        for (i = 0; i < 16u; i++) {
            preset_t q = FACTORY[i];
            memcpy(raw + 4 + i * 40, &q, 40);
        }
        memcpy(raw + 4 + 3 * 40, "OLD ONE", 8);
        store_save(REC_USER0, APP_ID, raw, sizeof raw);
        power_cycle();
        check("a version 1 user record: its presets read, fine parts 0", str_eq(user[3].name, "OLD ONE") &&
                                                                        user[3].p[P_CUT] == FACTORY[3].p[P_CUT] &&
                                                                        user[3].f[P_CUT] == 0u && str_eq(user[5].name, "BRIGHT"));
    }
    {   /* a version 2 user record: 16 presets of 68 bytes (28 parameters and 28 fine parts) */
        static uint8_t raw[4 + 16 * 68];
        uint32_t i;
        memset(raw, 0, sizeof raw);
        raw[0] = 2, raw[1] = 0;
        raw[2] = (uint8_t)((16 * 68) & 0xFF), raw[3] = (uint8_t)((16 * 68) >> 8);
        for (i = 0; i < 16u; i++) {
            memcpy(raw + 4 + i * 68, FACTORY[i].name, NAME_LEN);
            memcpy(raw + 4 + i * 68 + NAME_LEN, FACTORY[i].p, 28);
        }
        memcpy(raw + 4 + 2 * 68, "V2 SOUND", 9);
        raw[4 + 2 * 68 + NAME_LEN + 28 + P_CUT] = 55;     /* a fine part */
        store_save(REC_USER0, APP_ID, raw, sizeof raw);
        power_cycle();
        check("a version 2 user record: names, parameters, fine parts read; REV SIZE at its default",
              str_eq(user[2].name, "V2 SOUND") && user[2].f[P_CUT] == 55u && user[2].p[P_CUT] == FACTORY[2].p[P_CUT] &&
              user[2].p[P_RVSIZE] == SP[P_RVSIZE].def && user[2].p[P_RVMIX] == 0);
    }
    {   /* SysEx: format 2 carries the fine parts; format 1 is still taken */
        uint8_t f[8 + SL_BODY + 2], b1[SL_BODY1];
        preset_t p = FACTORY[0], q;
        uint32_t len, i, sum = 0;
        p.f[P_CUT] = 77;
        len = sl_encode(&p, f);
        check("SysEx format 2: the fine parts there and back", len == SL_BODY && !sl_decode(f, len, &q) && q.f[P_CUT] == 77u);
        memcpy(b1, f, SL_BODY1 - 1u);                     /* format 1: 28 parameters, no fine parts, its own checksum */
        b1[0] = 1;
        b1[1 + NAME_LEN] = 28;
        for (i = 0; i + 1u < SL_BODY1; i++)
            sum += b1[i];
        b1[SL_BODY1 - 1u] = (uint8_t)(sum & 0x7Fu);
        check("SysEx format 1: still taken (fine parts 0, REV SIZE at its default)", !sl_decode(b1, SL_BODY1, &q) &&
              q.f[P_CUT] == 0u && q.p[P_CUT] == FACTORY[0].p[P_CUT] && q.p[P_RVSIZE] == SP[P_RVSIZE].def);
    }
    host_nor_erase();
    power_cycle();
}

int main(void)
{
    if (system("mkdir -p build/shots"))
        return 1;
    screens();
    controls();
    persistence();
    presets();
    sequencer();
    reader();
    midi_control();
    nrpn();
    sound();
    puts(failed ? "APP TEST FAILED" : "app test passed");
    return failed ? 1 : 0;
}
