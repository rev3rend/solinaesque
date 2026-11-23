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
    check("SELECT forward: up to the last page", ui.page == PG_GLO);
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
    tap(B_PLAY);
    check("PLAY with the arp off: says so", str_eq(ui.msg, "PLAY: ARP IS OFF"));
    host_render(buf_ctl, CTL);
    tap(B_ARP);                                         /* the arp on afterwards: still running (no stale request) */
    host_render(buf_ctl, CTL);
    check("PLAY with the arp off leaves the transport alone", !arp.halt_pub);
    tap(B_ARP);
    host_render(buf_ctl, CTL);
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

/* ---------------------------------------------------------------- presets --- */
static int32_t peak(const int32_t *b, uint32_t frames);   /* (sound helpers, below) */
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
        for (burst_end = 1024; burst_end < d && big[2u * burst_end] != 0; burst_end++)
            ;
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

    /* PLAY: the transport */
    tap(B_PLAY);
    arp_watch(400);
    check("PLAY: the arp stops", seq_n == 0u && arp.halt_pub && str_eq(ui.msg, "ARP STOP"));
    tap(B_HOME);
    host_frame();
    host_png("build/shots/arp_stopped.png");
    tap(B_PLAY);
    arp_watch(400);
    check("PLAY again: it runs, from the first note at once", seq_n > 0u && seq_notes[0] == 60 && seq_at[0] <= 1u);

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
        tap(B_HOME);
        host_frame();
        host_png("build/shots/arp_clock.png");
    }
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

int main(void)
{
    if (system("mkdir -p build/shots"))
        return 1;
    screens();
    controls();
    persistence();
    presets();
    sequencer();
    sound();
    puts(failed ? "APP TEST FAILED" : "app test passed");
    return failed ? 1 : 0;
}
