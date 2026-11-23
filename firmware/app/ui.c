/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 R. Luke DuBois */
/* The panel (main loop): the controls table of docs/SPEC.md turned into actions, the LEDs, and the screens: HOME (the
 * name, the preset, four drawbars, a status line and the sounding notes), eight parameter pages (four rows, one per
 * knob) and SAVE. Lazy drawing: each screen area keeps a signature of everything it shows and is drawn only when it
 * changes, all of it when the page changes or the platform sets app_redraw. Warm and dark: the AMBER palette. */

#define PAL_AMBER 2u             /* tools/gen_ui_palettes.py PALETTES: GREY GREEN AMBER ... */
#define HOLD_MS 500u             /* a button held this long is a hold (ARP: its page; REC: clear) */
#define MSG_MS 1200u             /* a message stays on the bottom line this long */
#define NAME_EDIT 10u            /* the characters of a name SAVE edits (a preset holds up to 11) */
#define SRC_INIT (NFACT + 1u)    /* ui.save_src: INIT */

/* the name editor: KNOB 2 steps through these at the cursor, KNOB 3 through the words */
static const char NAME_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.";
static const char *const NAME_WORDS[] = {"STRINGS", "PAD", "CELLO", "VIOLA", "BASSES", "BRASS", "ORGAN", "CHOIR",
                                         "LEAD", "SWEEP", "DRONE", "ECHO", "SPACE", "MY SOUND"};
#define NWORDS (sizeof NAME_WORDS / sizeof NAME_WORDS[0])

static struct {
    uint8_t page;                /* PG_STR .. PG_GLO, or PG_HOME */
    uint8_t saving;              /* the SAVE screen is up */
    uint8_t save_slot;           /* 0..31: U01..U32 */
    uint8_t save_src;            /* what SAVE stores: 0 the current sound, 1..NFACT a factory sound, NFACT + 1 INIT */
    char nm[NAME_EDIT + 1];      /* the name being edited, padded with spaces */
    uint8_t nm_cur;              /* the cursor in it */
    uint8_t nm_word;             /* the word list's position (KNOB 3) */
    uint8_t hold, arp;           /* SEQ (HOLD), ARP on */
    uint8_t recording;           /* REC: a chord being recorded */
    uint32_t rec[4];             /* the notes held while it records */
    uint8_t set_dirty;           /* the settings changed since they were saved */
    uint32_t last_buttons;
    uint32_t down_ms[NB];        /* when each label went down (holds) */
    uint8_t held_fired[NB];      /* its hold already acted (no tap on release) */
    char msg[40];
    uint32_t msg_until;
    uint32_t sig[5];             /* the drawn areas' signatures */
    uint8_t drawn_page;          /* the page (or SAVE) the screen shows: a change draws everything */
    uint32_t frames;
} ui;

static void msg(const char *s)
{
    str_cpy(ui.msg, s, sizeof ui.msg);
    ui.msg_until = fm1_ms + MSG_MS;
}

static void msg2(const char *a, const char *b)
{
    str_cpy(ui.msg, a, sizeof ui.msg);
    cat(ui.msg, b, sizeof ui.msg);
    ui.msg_until = fm1_ms + MSG_MS;
}

static uint32_t fnv(uint32_t h, uint32_t v) { return (h ^ v) * 16777619u; }
static uint32_t fnv_s(uint32_t h, const char *s)
{
    while (*s)
        h = fnv(h, (uint8_t)*s++);
    return h;
}

/* a page row's parameter: its table entry and where its value lives */
static const param_t *row_param(uint32_t page, uint32_t row, int8_t **val)
{
    if (page < PG_ARP) {
        *val = &cur.p[page * 4u + row];
        return &SP[page * 4u + row];
    }
    if (page == PG_MIDI) {
        *val = &set.g2[row];
        return &GP2[row];
    }
    *val = &set.g[(page == PG_GLO ? 0u : 4u) + row];
    return &GP[(page == PG_GLO ? 0u : 4u) + row];
}

static void page_open(uint32_t page)
{
    if ((ui.page == PG_GLO || ui.page == PG_ARP || ui.page == PG_MIDI) && page != ui.page && ui.set_dirty) {
        ui.set_dirty = 0;                        /* leaving the settings' pages: keep what changed */
        msg(data_save_settings() ? "SAVE FAILED" : "SETTINGS SAVED");
    }
    ui.page = (uint8_t)page;
}

static void preset_load(uint32_t index)
{
    snd_epoch++;
    char b[8];
    cur_index = (uint8_t)index;
    cur = *preset_at(index);
    preset_label(b, sizeof b, index);
    msg2(b, index < NFACT ? " FACTORY" : " USER");
    snd_update();
}

/* ------------------------------------------------------------ drawing --- */
static void draw_bar_h(int32_t x, int32_t y, int32_t w, int32_t h, int32_t num, int32_t den, int bip)
{
    int32_t f;
    cv_rect(x, y, w, h, T_LINE);
    if (!bip) {
        f = den ? w * num / den : 0;
        if (f > 0)
            cv_rect(x, y, f, h, T_SEL);
    } else {                                     /* from the middle */
        int32_t mid = x + w / 2;
        f = (w / 2) * (num < 0 ? -num : num) / den;
        cv_rect(num < 0 ? mid - f : mid, y, f, h, T_SEL);
        cv_rect(mid, y - 1, 1, h + 2, T_MID);
    }
}

static void draw_home_top(void)                  /* the name, the preset */
{
    char b[24];
    cv_begin(240, 68, T_BG);
    cv_text_in(0, 6, 240, &AF_L, APP_NAME, T_THEME, T_BG);
    preset_label(b, sizeof b, cur_index);
    cat(b, "  ", sizeof b);
    cat(b, cur.name, sizeof b);
    if (preset_edited())                         /* turned since it was loaded: HOME held reverts */
        cat(b, " *", sizeof b);
    cv_text_in(0, 44, 240, &AF_M, b, T_TEXT, T_BG);
    cv_rect(16, 66, 208, 1, T_LINE);
    cv_blit(0, 0);
}

static void draw_home_bars(void)                 /* KNOB 1..4: 16', 8', 4', cutoff as drawbars */
{
    static const uint8_t P[4] = {P_R16, P_R8, P_R4, P_CUT};
    uint32_t i;
    cv_begin(240, 112, T_BG);
    for (i = 0; i < 4u; i++) {
        const param_t *p = &SP[P[i]];
        int32_t x = 18 + (int32_t)i * 54, h = 76, f = h * (cur.p[P[i]] - p->lo) / (p->hi - p->lo);
        char b[16];
        cv_rrect(x, 4, 42, h, 4, T_RAISE, T_BG);
        if (f > 0)
            cv_rrect(x, 4 + h - f, 42, f, 4, p->live ? T_THEME : T_DIM, T_BG);
        cv_text_in(x - 6, 84, 54, &AF_S, p->name, T_TEXT, T_BG);
        pv_text(b, sizeof b, p, cur.p[P[i]]);
        cv_text_in(x - 6, 98, 54, &AF_S, b, T_MID, T_BG);
    }
    cv_blit(0, 70);
}

static void status_line(char *b, unsigned max)   /* ensemble, octave, trigger mode (the flags: the bottom line) */
{
    char t[8];
    str_cpy(b, "ENS ", max);
    cat(b, ENS_NAME[cur.p[P_ENS]], max);
    cat(b, "  OCT", max);
    fmt_int(t, set.oct);
    cat(b, set.oct > 0 ? "+" : set.oct ? "" : " ", max);   /* "OCT+1", "OCT-2", "OCT 0" */
    cat(b, t, max);
    cat(b, "  ", max);
    cat(b, TRIG_NAME[set.g[G_TRIG]], max);
}

static uint32_t note_dots_sig(void)
{
    uint32_t h = 2166136261u, w;
    for (w = 0; w < 4u; w++)
        h = fnv(h, vox.gated_pub[w]);
    return h;
}

static const char *bottom_text(void)             /* a message while it lasts */
{
    return (int32_t)(ui.msg_until - fm1_ms) > 0 ? ui.msg : "";
}

static uint32_t cpu_pct5(void)                   /* the audio load in %, in steps of 5 (the screen redraws per step) */
{
    uint32_t p = audio_cpu_q8 * 100u / 256u;
    return p > 100u ? 100u : p / 5u * 5u;
}

static void cpu_text(char *b, unsigned max)
{
    char t[8];
    fmt_int(t, (int32_t)cpu_pct5());
    str_cpy(b, "CPU ", max);
    cat(b, t, max);
    cat(b, "%", max);
}

static int chord_on(void) { return set.chord_n[0] >= 2u; }

static void flags_text(char *b, unsigned max)    /* HOME's flags: HOLD ARP (CLK: MIDI clock) CHORD REC */
{
    b[0] = 0;
    if (ui.hold)
        cat(b, "HOLD ", max);
    if (ui.arp)
        cat(b, arp.ext_pub ? "ARP CLK " : arp.halt_pub ? "ARP STOP " : "ARP ", max);
    if (ui.recording)
        cat(b, "REC ", max);
    else if (chord_on())
        cat(b, "CHORD ", max);
    if (b[0])
        b[str_len(b) - 1u] = 0;
}

static void draw_home_status(void)
{
    char b[48];
    uint32_t n;
    int32_t x;
    cv_begin(240, 56, T_BG);
    cpu_text(b, sizeof b);                       /* the audio load, in the accent while notes are being shed */
    x = cv_text_r(232, 0, &AF_S, b, vox.cap < MAX_SOUNDING ? T_ACCENT : T_DIM, T_BG);
    status_line(b, sizeof b);
    cv_text_fit(8, 0, &AF_S, b, T_MID, T_BG, x - 8 - 8);
    for (n = 24; n < 120u; n++) {                /* C1 .. B8: a tick per note, lit while it is gated */
        int32_t x = 0 + (int32_t)(n - 24u) * 5 / 2;
        int on = (int)((vox.gated_pub[n >> 5] >> (n & 31u)) & 1u);
        cv_rect(x, on ? 18 : 22, 2, on ? 10 : 4, on ? T_ACCENT : (n % 12u ? T_LINE : T_DIM));
    }
    flags_text(b, sizeof b);
    x = cv_text_r(232, 36, &AF_S, b, T_ACCENT, T_BG);   /* the flags right, a message left of them */
    cv_text_fit(8, 36, &AF_S, bottom_text(), T_TEXT, T_BG, (b[0] ? x - 8 : 232) - 8);
    cv_blit(0, 184);
}

static void draw_page_head(void)
{
    char b[8];
    uint32_t r, live = 0;
    int8_t *v;
    cv_begin(240, 30, T_BG);
    cv_text(10, 6, &AF_M, PAGE_NAME[ui.page], T_THEME);
    for (r = 0; r < 4u; r++)
        live |= row_param(ui.page, r, &v)->live;
    fmt_int(b, ui.page + 1);
    cat(b, "/", sizeof b);
    fmt_int(b + str_len(b), NPAGES);
    cv_text_r(230, 8, &AF_S, b, T_MID, T_BG);
    if (!live)
        cv_text_r(190, 8, &AF_S, "SOON", T_ACCENT, T_BG);
    cv_rect(10, 28, 220, 1, T_LINE);
    cv_blit(0, 0);
}

static int page_drawn(void) { return ui.page == PG_ENV || ui.page == PG_FLT; }   /* the pages with a drawing */
#define ROW_H (page_drawn() ? 32 : 44)            /* a row: 44 px, or 32 to leave room for the drawing */

static void draw_page_rows(uint32_t r0)          /* two rows (r0, r0 + 1) */
{
    uint32_t r, page_live = 0;
    int8_t *v;
    const int32_t rh = ROW_H, bar = rh == 44 ? 28 : 24, bh = rh == 44 ? 6 : 4;
    for (r = 0; r < 4u; r++)
        page_live |= row_param(ui.page, r, &v)->live;
    cv_begin(240, 2u * (uint32_t)rh, T_BG);
    for (r = r0; r < r0 + 2u; r++) {
        const param_t *p = row_param(ui.page, r, &v);
        int32_t y = (int32_t)(r - r0) * rh, x;
        char b[20], k[8];
        str_cpy(k, "K", sizeof k);
        fmt_int(b, (int32_t)r + 1);
        cat(k, b, sizeof k);
        cv_text(10, y + 6, &AF_S, k, T_DIM);
        x = cv_text(34, y + 4, &AF_M, p->name, p->live ? T_TEXT : T_MID);
        if (!p->live && page_live)               /* one row of a working page not built yet (a whole page: its head) */
            cv_text(x + 8, y + 6, &AF_S, "SOON", T_ACCENT);
        pv_text(b, sizeof b, p, *v);
        cv_text_r(230, y + 4, &AF_M, b, p->live ? T_THEME : T_MID, T_BG);
        draw_bar_h(34, y + bar, 196, bh, *v - (p->kind == K_BIP || p->kind == K_TUNE ? 0 : p->lo),
                   p->kind == K_BIP || p->kind == K_TUNE ? p->hi : p->hi - p->lo,
                   p->kind == K_BIP || p->kind == K_TUNE);
    }
    cv_blit(0, 32u + r0 * (uint32_t)rh);
}

/* ------------------------------------------------------------ the drawings (ENVELOPE, FILTER) --- */
#define CURVE_Y 162u             /* the drawing's strip: 50 px under the four compact rows, over the footer */
#define CURVE_H 50
#define CURVE_X0 10
#define CURVE_W 220

/* ENVELOPE: the ADSR's shape. The widths follow the knobs' positions (the times span 1 ms .. 10 s: a width per
 * second would hide the short ones), the plateau's height is SUSTAIN, decay and release fall as the sound does:
 * exponentially, ~52 dB down at their time (sound.c), drawn in 8 segments. */
static void draw_env_curve(void)
{
    static const uint8_t EXPO[9] = {255, 121, 57, 27, 13, 6, 3, 1, 0};   /* 256 x e^(-0.75 i): 8 steps to ~0 */
    const int32_t top = 4, h = 42, sus = cur.p[P_SUS] * 256 / 100;
    int32_t x = CURVE_X0, aw = 6 + cur.p[P_ATK] * 50 / 127, dw = 6 + cur.p[P_DEC] * 50 / 127, sw = 40,
            rw = 6 + cur.p[P_REL] * 60 / 127, i, px, py;
#define LVL_Y(l) (top + h - (l) * h / 256)
    cv_begin(240, CURVE_H, T_BG);
    cv_rect(CURVE_X0, top + h, CURVE_W, 1, T_LINE);
    cv_line_t(x, LVL_Y(0), x + aw, LVL_Y(256), T_THEME, 2);
    x += aw;
    for (px = x, py = LVL_Y(256), i = 1; i <= 8; i++) {
        int32_t nx = x + dw * i / 8, ny = LVL_Y(sus + (256 - sus) * EXPO[i] / 256);
        cv_line_t(px, py, nx, ny, T_THEME, 2);
        px = nx;
        py = ny;
    }
    x += dw;
    cv_line_t(x, LVL_Y(sus), x + sw, LVL_Y(sus), T_THEME, 2);
    x += sw;
    for (px = x, py = LVL_Y(sus), i = 1; i <= 8; i++) {
        int32_t nx = x + rw * i / 8, ny = LVL_Y(sus * EXPO[i] / 256);
        cv_line_t(px, py, nx, ny, T_THEME, 2);
        px = nx;
        py = ny;
    }
#undef LVL_Y
    cv_blit(0, CURVE_Y);
}

/* log2(v) x 256 for v >= 1 (the leading bit and the next 8: within 0.09, enough to draw with) */
static int32_t log2_q8(uint32_t v)
{
    int32_t e = 31 - __builtin_clz(v);
    return e * 256 + (int32_t)(((v << (31 - e)) >> 23) & 0xFFu);
}

/* FILTER: the low-pass's response, |H|^2 = 1 / ((1 - r^2)^2 + k^2 r^2) with r = f / fc and k the damping, across
 * 20 Hz .. 16 kHz on CUTOFF's own log scale (a step: 1/13.2 octave), +24 dB at the top, -36 at the bottom, 0 dB
 * dotted. Integer only (the main loop runs without a float library): r^2 by repeated multiplication, a 32-bit
 * divide, log2 by the leading bit. The base CUTOFF (the envelope and key tracking move it while playing). */
static void draw_flt_curve(void)
{
    const int32_t top = 2, h = 46, cut = cur.p[P_CUT];
    const uint32_t k = 131072u - 128451u * (uint32_t)cur.p[P_RES] / 100u;   /* damping, Q16: 2 .. 0.04 */
    const uint64_t k2 = (uint64_t)k * k >> 16;
    int32_t x, px = -1, py = 0, y0 = top + 24 * h / 60;
    cv_begin(240, CURVE_H, T_BG);
    for (x = 0; x < CURVE_W; x += 4)
        cv_rect(CURVE_X0 + x, y0, 2, 1, T_LINE);
    cv_rect(CURVE_X0 + cut * (CURVE_W - 1) / 127, top, 1, h, T_DIM);
    for (x = 0; x < CURVE_W; x++) {
        int32_t u = x * 127 / (CURVE_W - 1) - cut, i, y;
        uint64_t r2 = 65536u, den;
        uint32_t step = u >= 0 ? 72809u : 58989u;           /* 1.054045^2 or its inverse, Q16: r^2 a step */
        for (i = 0; i < (u < 0 ? -u : u) && r2 < (1ull << 32); i++)
            r2 = r2 * step >> 16;
        den = ((r2 > 65536u ? r2 - 65536u : 65536u - r2) * (r2 > 65536u ? r2 - 65536u : 65536u - r2) >> 16) +
              (k2 * r2 >> 16);
        if (den >= 0xFFFFFFFFull || !den)
            y = top + h;
        else {
            /* 10 log10 = 3.0103 log2 ~ 3 + 3/256 (written so: (l - 4096) x 771 folds into a constant that reads as a
             * ROM address, 0xFFCFD000, and the build's check refuses it) */
            int32_t d = log2_q8(0xFFFFFFFFu / (uint32_t)den) - 16 * 256, db_q8 = d * 3 + (d * 3 >> 8);
            y = top + (24 * 256 - db_q8) * h / (60 * 256);
            y = y < top ? top : y > top + h ? top + h : y;
        }
        if (px >= 0)
            cv_line_t(CURVE_X0 + px, py, CURVE_X0 + x, y, T_THEME, 2);
        px = x;
        py = y;
    }
    cv_blit(0, CURVE_Y);
}

static uint32_t curve_sig(uint32_t h)            /* what the drawing shows */
{
    if (ui.page == PG_ENV)
        return fnv(fnv(fnv(fnv(h, (uint32_t)cur.p[P_ATK]), (uint32_t)cur.p[P_DEC]), (uint32_t)cur.p[P_SUS]),
                   (uint32_t)cur.p[P_REL]);
    return fnv(fnv(h, (uint32_t)cur.p[P_CUT]), (uint32_t)cur.p[P_RES]);
}

static void arp_foot(char *b, unsigned max)      /* the ARP page's footer: the clock and the transport */
{
    char t[8];
    if (arp.ext_pub)
        str_cpy(b, "MIDI CLOCK", max);
    else {
        fmt_int(t, set.bpm);
        str_cpy(b, "TEMPO ", max);
        cat(b, t, max);
        cat(b, " BPM", max);
    }
    cat(b, arp.halt_pub ? "   STOPPED" : arp.ext_pub ? "" : "   ARP + PRESETS", max);
}

static void draw_foot(void)
{
    const char *m = bottom_text();
    char b[48];
    if (ui.page == PG_ARP)
        arp_foot(b, sizeof b);
    else
        str_cpy(b, "SELECT: PAGE   HOME: BACK", sizeof b);
    cv_begin(240, 26, T_BG);
    cv_rect(10, 0, 220, 1, T_LINE);
    cv_text_fit(10, 8, &AF_S, m[0] ? m : b, m[0] || ui.page == PG_ARP ? T_TEXT : T_DIM, T_BG, 220);
    cv_blit(0, 214);
}

static const preset_t *save_source(void)          /* what SAVE would store */
{
    return !ui.save_src ? &cur : ui.save_src <= NFACT ? &FACTORY[ui.save_src - 1u] : &INIT_SOUND;
}

static void save_source_text(char *b, unsigned max)
{
    if (!ui.save_src) {
        str_cpy(b, "THE CURRENT SOUND", max);
        return;
    }
    if (ui.save_src > NFACT) {
        str_cpy(b, "INIT (CLEAR THE SLOT)", max);
        return;
    }
    preset_label(b, max, ui.save_src - 1u);
    cat(b, " ", max);
    cat(b, FACTORY[ui.save_src - 1u].name, max);
}

static void draw_save(void)
{
    char b[32];
    uint32_t i;
    cv_begin(240, 124, T_BG);
    cv_text_in(0, 2, 240, &AF_S, "SAVE TO", T_MID, T_BG);
    preset_label(b, sizeof b, NFACT + ui.save_slot);
    cv_text_in(0, 18, 240, &AF_L, b, T_ACCENT, T_BG);
    str_cpy(b, "NOW: ", sizeof b);
    cat(b, user[ui.save_slot].name, sizeof b);
    cv_text_in(0, 52, 240, &AF_S, b, T_MID, T_BG);
    str_cpy(b, "FROM: ", sizeof b);
    save_source_text(b + 6, sizeof b - 6);
    cv_text_in(0, 70, 240, &AF_S, b, T_TEXT, T_BG);
    for (i = 0; i < NAME_EDIT; i++) {            /* the name, a cell a character, the cursor under one */
        char c[2] = {ui.nm[i], 0};
        int32_t x = 40 + (int32_t)i * 16;        /* 10 cells of 16 px: room for a W */
        if (c[0] != ' ')
            cv_text_in(x, 92, 16, &AF_M, c, T_TEXT, T_BG);
        cv_rect(x + 2, 113, 12, i == ui.nm_cur ? 3 : 1, i == ui.nm_cur ? T_ACCENT : T_LINE);
    }
    cv_blit(0, 0);
    cv_begin(240, 116, T_BG);
    cv_text_in(0, 4, 240, &AF_S, "K1 CURSOR   K2 LETTER   K3 WORD", T_MID, T_BG);
    cv_text_in(0, 22, 240, &AF_S, "K4 CLEAR   PRESETS: SLOT", T_MID, T_BG);
    cv_text_in(0, 40, 240, &AF_S, "ALGORITHM: WHAT TO STORE", T_MID, T_BG);
    cv_text_in(0, 62, 240, &AF_S, "SAVE: STORE   OTHER BUTTONS: CANCEL", T_TEXT, T_BG);
    cv_text_fit(10, 92, &AF_S, bottom_text(), T_TEXT, T_BG, 220);
    cv_blit(0, 124);
}

/* the signatures: every value an area shows */
static uint32_t sig_preset(uint32_t h)
{
    uint32_t i;
    h = fnv(h, cur_index);
    h = fnv_s(h, cur.name);
    for (i = 0; i < NSP; i++)
        h = fnv(h, (uint32_t)cur.p[i]);
    return h;
}

static void ui_draw(void)
{
    uint32_t screen = ui.saving ? 0xFEu : ui.page, s[5] = {0, 0, 0, 0, 0}, i, all = app_redraw || screen != ui.drawn_page;
    uint32_t h0 = 2166136261u, m = fnv_s(h0, bottom_text());
    if (all) {
        app_redraw = 0;
        lcd_fill(0, 0, 240, 240, T_BG);
        ui.drawn_page = (uint8_t)screen;
    }
    if (ui.saving) {
        s[0] = fnv(fnv(fnv(fnv(fnv_s(fnv_s(h0, user[ui.save_slot].name), ui.nm), ui.save_slot), ui.save_src), ui.nm_cur), m);
        if (all || s[0] != ui.sig[0])
            draw_save();
        ui.sig[0] = s[0];
        return;
    }
    if (ui.page == PG_HOME) {
        s[0] = fnv(fnv_s(fnv(h0, cur_index), cur.name), (uint32_t)preset_edited());
        s[1] = sig_preset(h0);
        s[2] = fnv(fnv(fnv(fnv(fnv(note_dots_sig(), (uint32_t)set.oct), set.g[G_TRIG]), ui.hold), ui.arp), m);
        s[2] = fnv(fnv(fnv(fnv(s[2], ui.recording), (uint32_t)chord_on()), arp.ext_pub), arp.halt_pub);
        s[2] = fnv(fnv(fnv(s[2], (uint32_t)cur.p[P_ENS]), cpu_pct5()), vox.cap < MAX_SOUNDING);
        if (all || s[0] != ui.sig[0])
            draw_home_top();
        if (all || s[1] != ui.sig[1])
            draw_home_bars();
        if (all || s[2] != ui.sig[2])
            draw_home_status();
    } else {
        s[0] = fnv(h0, ui.page);
        for (i = 0; i < 2u; i++) {
            int8_t *v0, *v1;
            row_param(ui.page, 2u * i, &v0);
            row_param(ui.page, 2u * i + 1u, &v1);
            s[1u + i] = fnv(fnv(fnv(h0, ui.page), (uint32_t)*v0), (uint32_t)*v1);
        }
        s[3] = fnv(fnv(fnv(m, ui.page == PG_ARP ? set.bpm : 0u), arp.ext_pub), arp.halt_pub);
        if (all || s[0] != ui.sig[0])
            draw_page_head();
        if (all || s[1] != ui.sig[1])
            draw_page_rows(0);
        if (all || s[2] != ui.sig[2])
            draw_page_rows(2);
        if (all || s[3] != ui.sig[3])
            draw_foot();
        if (page_drawn()) {
            s[4] = curve_sig(fnv(h0, ui.page));
            if (all || s[4] != ui.sig[4]) {
                if (ui.page == PG_ENV)
                    draw_env_curve();
                else
                    draw_flt_curve();
            }
        }
    }
    for (i = 0; i < 5u; i++)
        ui.sig[i] = s[i];
}

/* ------------------------------------------------------------ input --- */
static void name_set(const char *src)              /* the editor's name from a preset's, padded */
{
    uint32_t i, end = 0;
    for (i = 0; i < NAME_EDIT; i++) {
        if (!end && !src[i])                     /* (never read past the terminator: ASan caught that once) */
            end = 1;
        ui.nm[i] = end ? ' ' : src[i];
    }
    ui.nm[NAME_EDIT] = 0;
}

static void save_open(void)
{
    ui.saving = 1;
    ui.save_slot = cur_index >= NFACT && cur_index < NPRESETS ? (uint8_t)(cur_index - NFACT) : 0u;
    ui.save_src = 0;
    ui.nm_cur = 0;
    ui.nm_word = NWORDS;                         /* (no word yet) */
    name_set(cur.name);
}

static void save_store(void)
{
    preset_t p = *save_source();
    int32_t r, k = NAME_EDIT;
    char b[32], l[8];
    while (k > 0 && ui.nm[k - 1] == ' ')         /* the name, trimmed (all spaces: NO NAME) */
        k--;
    memset(p.name, 0, sizeof p.name);
    memcpy(p.name, ui.nm, (uint32_t)k);
    if (!k)
        str_cpy(p.name, "NO NAME", NAME_LEN);
    user[ui.save_slot] = p;
    r = data_save_user(ui.save_slot);
    preset_label(l, sizeof l, NFACT + ui.save_slot);
    if (!ui.save_src) {                          /* the current sound: it now comes from that slot */
        cur = p;
        snd_epoch++;
        cur_index = (uint8_t)(NFACT + ui.save_slot);
        set.last_preset = preset_code(cur_index);
        r |= data_save_settings();
        ui.set_dirty = 0;
        str_cpy(b, "SAVED ", sizeof b);
        cat(b, l, sizeof b);
    } else {                                     /* a factory sound or INIT into the slot; the sound playing stays */
        str_cpy(b, l, sizeof b);
        cat(b, ui.save_src > NFACT ? " CLEARED" : " = ", sizeof b);
        if (ui.save_src <= NFACT)
            cat(b, p.name, sizeof b);
    }
    msg(r ? "SAVE FAILED" : b);
    ui.saving = 0;
}

static void save_encoders(void)                  /* the SAVE screen's knobs */
{
    int32_t s, k;
    s = panel_enc(EN_PRESET);
    if (s) {
        k = ui.save_slot + s;
        ui.save_slot = (uint8_t)(k < 0 ? 0 : k >= (int32_t)NUSER ? NUSER - 1 : k);
    }
    s = panel_enc(EN_ALGO);
    if (s) {
        k = ui.save_src + s;
        ui.save_src = (uint8_t)(k < 0 ? 0 : k > (int32_t)SRC_INIT ? SRC_INIT : k);
        name_set(save_source()->name);
        ui.nm_cur = 0;
    }
    s = panel_enc(EN_K1);
    if (s) {
        k = ui.nm_cur + s;
        ui.nm_cur = (uint8_t)(k < 0 ? 0 : k >= (int32_t)NAME_EDIT ? NAME_EDIT - 1 : k);
    }
    s = panel_enc(EN_K2);
    if (s) {
        const int32_t n = (int32_t)sizeof NAME_CHARS - 1;
        int32_t at = 0;
        while (at < n && NAME_CHARS[at] != ui.nm[ui.nm_cur])
            at++;
        at = ((at == n ? 0 : at) + s) % n;
        ui.nm[ui.nm_cur] = NAME_CHARS[at < 0 ? at + n : at];
    }
    s = panel_enc(EN_K3);
    if (s) {
        if (ui.nm_word >= NWORDS)                /* the first turn: the first word (or, backwards, the last) */
            k = s > 0 ? s - 1 : (int32_t)NWORDS + s;
        else
            k = (int32_t)ui.nm_word + s;
        k %= (int32_t)NWORDS;
        k = k < 0 ? k + (int32_t)NWORDS : k;
        ui.nm_word = (uint8_t)k;
        name_set(NAME_WORDS[k]);
        ui.nm_cur = 0;
    }
    if (panel_enc(EN_K4)) {
        name_set("");
        ui.nm_cur = 0;
    }
    panel_enc(EN_SELECT);                        /* (not used here: dropped) */
}

/* chord memory: REC tapped starts recording (the keys play plain meanwhile), tapped again keeps what was held as the
 * chord (semitones above its lowest note, up to 8 notes within 4 octaves); REC held clears it. Saved with the settings
 * at once (a deliberate act, not a knob turn). */
static void chord_rec_tap(void)
{
    uint32_t w, n = 0, lo = 128u, k;
    if (!ui.recording) {
        ui.recording = 1;
        ui.rec[0] = ui.rec[1] = ui.rec[2] = ui.rec[3] = 0;
        chord_rec = 1;
        snd_update();
        msg("CHORD: HOLD KEYS, THEN REC");
        return;
    }
    ui.recording = 0;
    chord_rec = 0;
    for (k = 0; k < 128u; k++)
        if ((ui.rec[k >> 5] >> (k & 31u)) & 1u) {
            if (lo == 128u)
                lo = k;
            if (k - lo <= 48u && n < CHORD_MAX)
                set.chord[0][n++] = (int8_t)(k - lo);
        }
    if (n >= 2u) {
        char t[4];
        for (w = n; w < CHORD_MAX; w++)
            set.chord[0][w] = 0;
        set.chord_n[0] = (uint8_t)n;
        fmt_int(t, (int32_t)n);
        if (data_save_settings())
            msg("SAVE FAILED");
        else
            msg2("CHORD SET: NOTES ", t);
    } else
        msg("CHORD: HOLD 2 OR MORE KEYS");
    snd_update();
}

static void chord_clear(void)
{
    ui.recording = 0;
    chord_rec = 0;
    set.chord_n[0] = 0;
    snd_update();
    msg(data_save_settings() ? "SAVE FAILED" : "CHORD CLEARED");
}

static void ui_buttons(void)
{
    uint32_t now = fm1_in.buttons, b;
    for (b = 0; b < NB; b++) {
        uint32_t m = panel_mask(b), down = now & m, was = ui.last_buttons & m;
        if (down && !was) {                      /* pressed */
            ui.down_ms[b] = fm1_ms;
            ui.held_fired[b] = 0;
            if (ui.saving) {
                if (b == B_SAVE)
                    save_store();
                else {
                    ui.saving = 0;
                    msg("SAVE CANCELLED");
                }
                ui.held_fired[b] = 1;            /* the press was the dialog's: no tap action */
                continue;
            }
            switch (b) {
            case B_HOME: page_open(PG_HOME); break;
            case B_EDIT: page_open(PG_STR); break;
            case B_SCL: page_open(PG_FLT); break;
            case B_ENV: page_open(PG_ENV); break;
            case B_LFO: page_open(PG_LFO); break;
            case B_GLO: page_open(ui.page == PG_GLO ? PG_MIDI : PG_GLO); break;   /* again: GLOBAL 2 (MIDI) */
            case B_FX:
                page_open(ui.page == PG_FX1 ? PG_FX2 : PG_FX1);   /* FX 1 first; again: FX 2, and back */
                break;
            case B_SEQ:
                ui.hold ^= 1u;
                vox.hold = ui.hold;
                msg(ui.hold ? "HOLD ON" : "HOLD OFF");
                break;
            case B_PLAY:                         /* the arp's transport (the ISR flips it); nothing with the arp off */
                if (ui.arp)
                    arp.play_req = 1;
                msg(!ui.arp ? "PLAY: ARP IS OFF" : arp.halt_pub ? "ARP PLAY" : "ARP STOP");
                break;
            case B_OCTDN:
            case B_OCTUP: {
                char t[8];
                int32_t o = set.oct + (b == B_OCTUP ? 1 : -1);
                set.oct = (int8_t)(o < -2 ? -2 : o > 2 ? 2 : o);
                snd_update();
                fmt_int(t, set.oct);
                msg2(set.oct > 0 ? "OCTAVE +" : "OCTAVE ", t);
                break;
            }
            }                                    /* (SAVE: a tap opens the SAVE screen, a hold loads INIT) */
        } else if (down && !ui.held_fired[b] && fm1_ms - ui.down_ms[b] >= HOLD_MS) {   /* held */
            ui.held_fired[b] = 1;
            if (b == B_ARP)
                page_open(PG_ARP);
            else if (b == B_REC)
                chord_clear();
            else if (b == B_HOME) {              /* revert: the stored preset again */
                if (cur_index < NPRESETS) {
                    cur = *preset_at(cur_index);
                    snd_epoch++;
                    snd_update();
                    msg("REVERTED");
                } else
                    msg("NEW SOUND: NOTHING TO REVERT");
            } else if (b == B_SAVE) {            /* INIT: a plain sound to start from */
                cur = INIT_SOUND;
                snd_epoch++;
                cur_index = PRESET_NEW;
                snd_update();
                msg("INIT SOUND");
            }
        } else if (!down && was && !ui.held_fired[b]) {   /* released before the hold: a tap */
            if (b == B_ARP) {
                ui.arp ^= 1u;
                arp_on = ui.arp;
                snd_update();
                msg(ui.arp ? "ARP ON" : "ARP OFF");
            } else if (b == B_REC)
                chord_rec_tap();
            else if (b == B_SAVE)
                save_open();
        }
    }
    ui.last_buttons = now;
}

static void ui_encoders(void)
{
    int32_t s, r;
    if (ui.saving) {
        save_encoders();
        return;
    }
    s = panel_enc(EN_SELECT);
    if (s && !ui.saving) {
        int32_t p = ui.page == PG_HOME ? (s > 0 ? -1 : NPAGES) : ui.page;
        p += s;
        p = p < 0 ? 0 : p >= NPAGES ? NPAGES - 1 : p;
        page_open((uint32_t)p);
    }
    s = panel_enc(EN_ALGO);
    if (s) {
        int32_t e = cur.p[P_ENS] + s;
        cur.p[P_ENS] = (int8_t)(e < 0 ? 0 : e > 4 ? 4 : e);
        msg2("ENSEMBLE ", ENS_NAME[cur.p[P_ENS]]);
        snd_update();                            /* (the engine reads it through fxp) */
    }
    s = panel_enc(EN_PRESET);
    if (s) {
        if (ui.saving) {
            int32_t k = ui.save_slot + s;
            ui.save_slot = (uint8_t)(k < 0 ? 0 : k >= (int32_t)NUSER ? NUSER - 1 : k);
        } else if (panel_held(B_ARP)) {          /* ARP held: the tempo */
            char t[8];
            int32_t bpm = set.bpm + s;
            set.bpm = (uint8_t)(bpm < 40 ? 40 : bpm > 240 ? 240 : bpm);
            ui.held_fired[B_ARP] = 1;            /* not a tap */
            ui.set_dirty = 1;
            fmt_int(t, set.bpm);
            msg2("TEMPO ", t);
        } else {
            int32_t k = cur_index + s;
            preset_load((uint32_t)(k < 0 ? 0 : k >= (int32_t)NPRESETS ? NPRESETS - 1 : k));
        }
    }
    for (r = 0; r < 4; r++) {
        s = panel_enc(EN_K1 + (uint32_t)r);
        if (!s || ui.saving)
            continue;
        if (ui.page == PG_HOME) {
            static const uint8_t P[4] = {P_R16, P_R8, P_R4, P_CUT};
            cur.p[P[r]] = clamp8(cur.p[P[r]] + s, &SP[P[r]]);
        } else {
            int8_t *v;
            const param_t *p = row_param(ui.page, (uint32_t)r, &v);
            int32_t old = *v;
            *v = clamp8(*v + s, p);
            if (ui.page >= PG_ARP && *v != old) {
                ui.set_dirty = 1;
                if (ui.page == PG_GLO && r == G_MIDICH)
                    vox.midi_off_req = 1;        /* notes of the old channel must not hang */
            }
        }
        snd_update();
    }
}

static void ui_master(void)                      /* the MASTER pot as the output level, 0 .. 256 */
{
    static int32_t avg = -1;
    int32_t v, q;
    if (ui.frames & 3u)
        return;
    v = fm1_adc_read(FM1_ADC_MASTER);
    if (v < 0)
        return;                                  /* a timeout: keep the last */
    avg = avg < 0 ? v << 4 : avg + v - (avg >> 4);   /* x16, smoothed over ~16 reads */
    q = (avg >> 4) * 257 >> 10;                  /* 0..1023 -> 0..256 */
    if (q - master_q8 > 2 || master_q8 - q > 2 || q == 0 || q == 256) {   /* a dead band: no chatter */
        master_q8 = q > 256 ? 256 : q;
        snd_update();
    }
}

static void ui_leds(void)
{
    static const uint8_t PAGE_BTN[NPAGES] = {B_EDIT, B_SCL, B_ENV, B_LFO, B_FX, B_FX, B_ARP, B_GLO, B_GLO};
    uint32_t b, k, blink = (fm1_ms / 250u) & 1u, kb = snd.key_base;
    for (b = 0; b < NB; b++) {
        int on = 0;
        if (ui.page < NPAGES && PAGE_BTN[ui.page] == b)
            on = 1;
        if ((b == B_PLAY && ui.arp && !arp.halt_pub) || (b == B_REC && (ui.recording ? blink : chord_on())))
            on = 1;
        if ((b == B_HOME && ui.page == PG_HOME) || (b == B_SEQ && ui.hold) || (b == B_ARP && ui.arp) ||
            (b == B_SAVE && ui.saving && blink) || (b == B_OCTDN && set.oct < 0) || (b == B_OCTUP && set.oct > 0))
            on = 1;
        if (panel_held(b))
            on = 1;
        fm1_led_key(panel.btn[b], on);
    }
    for (k = 0; k < 27u; k++) {
        uint32_t n = kb + k;
        fm1_led_key(14u + k, n < 128u && ((vox.gated_pub[n >> 5] >> (n & 31u)) & 1u));
    }
}

static void ui_init(void)
{
    palette_set(PAL_AMBER);
    ui.page = PG_HOME;
    ui.drawn_page = 0xFF;
}

/* ------------------------------------------------------------ MIDI control: the CC map, Program Change --- */
/* a mapped control's value now, 0..127 (what it would send) */
static int32_t cc_get(uint32_t k)
{
    uint32_t t = CC_TGT[k];
    const param_t *p = t < CT_G ? &SP[t] : t < CT_G2 ? &GP[t - CT_G] : t < CT_TEMPO ? &GP2[t - CT_G2] : 0;
    int32_t v;
    if (p) {
        v = t < CT_G ? cur.p[t] : t < CT_G2 ? set.g[t - CT_G] : set.g2[t - CT_G2];
        return ((v - p->lo) * 127 + (p->hi - p->lo) / 2) / (p->hi - p->lo);
    }
    switch (t) {
    case CT_TEMPO: return ((int32_t)set.bpm - 40) * 127 / 200;
    case CT_ARP: return ui.arp ? 127 : 0;
    case CT_HOLD: return ui.hold ? 127 : 0;
    case CT_PLAY: return ui.arp && !arp.halt_pub ? 127 : 0;
    case CT_OCT: return (set.oct + 2) * 127 / 4;
    }
    return 0;
}

/* a mapped control set from MIDI (cc_take) */
static void cc_set(uint32_t k, int32_t v)
{
    uint32_t t = CC_TGT[k];
    const param_t *p = t < CT_G ? &SP[t] : t < CT_G2 ? &GP[t - CT_G] : t < CT_TEMPO ? &GP2[t - CT_G2] : 0;
    if (p) {
        int8_t x = clamp8(p->lo + (v * (p->hi - p->lo) + 63) / 127, p);
        if (t < CT_G)
            cur.p[t] = x;
        else if (t < CT_G2) {
            ui.set_dirty |= set.g[t - CT_G] != x;
            set.g[t - CT_G] = x;
        } else {
            ui.set_dirty |= set.g2[t - CT_G2] != x;
            set.g2[t - CT_G2] = x;
        }
        return;
    }
    switch (t) {
    case CT_TEMPO: set.bpm = (uint8_t)(40 + v * 200 / 127); ui.set_dirty = 1; break;
    case CT_ARP:
        ui.arp = v >= 64;
        arp_on = ui.arp;
        break;
    case CT_HOLD:
        ui.hold = v >= 64;
        vox.hold = ui.hold;
        break;
    case CT_PLAY:                                /* run (>= 64) or stop, as PLAY; nothing with the arp off */
        if (ui.arp && (v >= 64) == arp.halt_pub)
            arp.play_req = 1;
        break;
    case CT_OCT: set.oct = (int8_t)(v * 5 / 128 - 2); break;
    }
}

static struct {
    uint8_t seen[NCC], pc_seen;  /* the ISR's counters as taken */
    int8_t out[NCC];             /* each control's value as last sent (or received: no echo), -1 not yet */
    uint32_t epoch;
    uint8_t index, ready;
} ccs;

/* what the ISR received, applied here, where the parameters live; no echo of it on MIDI out */
static void cc_take(void)
{
    uint32_t k, any = 0;
    uint8_t q = vox.pc_seq;
    for (k = 0; k < NCC; k++) {
        uint8_t c = vox.cc_seq[k];
        if (c != ccs.seen[k]) {
            ccs.seen[k] = c;
            cc_set(k, vox.cc_val[k]);
            ccs.out[k] = (int8_t)cc_get(k);      /* (what MIDI set is not sent back) */
            any = 1;
        }
    }
    if (q != ccs.pc_seen) {                      /* Program Change: 0..15 F1..F16, 16..47 U01..U32 */
        ccs.pc_seen = q;
        if (vox.pc_val < NPRESETS) {
            preset_load(vox.pc_val);
            ccs.index = cur_index;               /* (not sent back: nor its Program Change, nor its values) */
            ccs.epoch = snd_epoch;
            for (k = 0; k < NCC; k++)
                ccs.out[k] = (int8_t)cc_get(k);
        }
    }
    if (any)
        snd_update();
}

/* what the panel changed, sent (CC OUT on): a knob's CC; a whole new sound: its Program Change only */
static void cc_out(void)
{
    uint32_t k, ch = seqp.out_ch & 15u;
    int send = set.g2[G2_CCOUT] != 0;
    if (!ccs.ready || snd_epoch != ccs.epoch) {  /* boot, or a whole new sound: take it as it is */
        if (ccs.ready && send && cur_index < NPRESETS && cur_index != ccs.index)
            ccq_push(0xC0u | ch, cur_index, 0);
        for (k = 0; k < NCC; k++)
            ccs.out[k] = (int8_t)cc_get(k);
        ccs.epoch = snd_epoch;
        ccs.index = cur_index;
        ccs.ready = 1;
        return;
    }
    for (k = 0; k < NCC; k++) {
        int32_t v = cc_get(k);
        if (v != ccs.out[k] && (!send || !ccq_push(0xB0u | ch, CC_NUM[k], (uint32_t)v)))
            ccs.out[k] = (int8_t)v;              /* (queue full: sent on a later frame) */
    }
}

static void ui_frame(void)
{
    ui.frames++;
    cc_take();
    if (ui.recording) {                          /* REC: every note held while it records */
        uint32_t w;
        for (w = 0; w < 4u; w++)
            ui.rec[w] |= vox.raw_pub[w];
    }
    ui_buttons();
    ui_encoders();
    snd_shed();
    ui_master();
    ui_leds();
    ui_draw();
    cc_out();                                    /* (last: what the panel changed this frame goes out) */
}
