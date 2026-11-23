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
    uint8_t hold, arp, chord;    /* SEQ (HOLD), ARP on, a chord stored (milestone 8) */
    uint8_t set_dirty;           /* the settings changed since they were saved */
    uint32_t last_buttons;
    uint32_t down_ms[NB];        /* when each label went down (holds) */
    uint8_t held_fired[NB];      /* its hold already acted (no tap on release) */
    char msg[24];
    uint32_t msg_until;
    uint32_t sig[4];             /* the drawn areas' signatures */
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
    *val = &set.g[(page == PG_GLO ? 0u : 4u) + row];
    return &GP[(page == PG_GLO ? 0u : 4u) + row];
}

static void page_open(uint32_t page)
{
    if ((ui.page == PG_GLO || ui.page == PG_ARP) && page != ui.page && ui.set_dirty) {
        ui.set_dirty = 0;                        /* leaving the settings' pages: keep what changed */
        msg(data_save_settings() ? "SAVE FAILED" : "SETTINGS SAVED");
    }
    ui.page = (uint8_t)page;
}

static void preset_load(uint32_t index)
{
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
    str_cpy(b, ui.hold ? (ui.arp ? "HOLD ARP" : "HOLD") : ui.arp ? "ARP" : "", sizeof b);
    cv_text_r(232, 36, &AF_S, b, T_ACCENT, T_BG);   /* the flags right, a message left of them */
    cv_text_fit(8, 36, &AF_S, bottom_text(), T_TEXT, T_BG, 224 - 8 - text_w(&AF_S, "HOLD ARP"));
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
    cat(b, "/8", sizeof b);
    cv_text_r(230, 8, &AF_S, b, T_MID, T_BG);
    if (!live)
        cv_text_r(190, 8, &AF_S, "SOON", T_ACCENT, T_BG);
    cv_rect(10, 28, 220, 1, T_LINE);
    cv_blit(0, 0);
}

static void draw_page_rows(uint32_t r0)          /* two rows (r0, r0 + 1), 44 px each */
{
    uint32_t r, page_live = 0;
    int8_t *v;
    for (r = 0; r < 4u; r++)
        page_live |= row_param(ui.page, r, &v)->live;
    cv_begin(240, 88, T_BG);
    for (r = r0; r < r0 + 2u; r++) {
        const param_t *p = row_param(ui.page, r, &v);
        int32_t y = (int32_t)(r - r0) * 44, x;
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
        draw_bar_h(34, y + 28, 196, 6, *v - (p->kind == K_BIP || p->kind == K_TUNE ? 0 : p->lo),
                   p->kind == K_BIP || p->kind == K_TUNE ? p->hi : p->hi - p->lo,
                   p->kind == K_BIP || p->kind == K_TUNE);
    }
    cv_blit(0, 32u + r0 * 44u);
}

static void draw_foot(void)
{
    const char *m = bottom_text();
    cv_begin(240, 26, T_BG);
    cv_rect(10, 0, 220, 1, T_LINE);
    cv_text_fit(10, 8, &AF_S, m[0] ? m : "SELECT: PAGE   HOME: BACK", m[0] ? T_TEXT : T_DIM, T_BG, 220);
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
    uint32_t screen = ui.saving ? 0xFEu : ui.page, s[4], i, all = app_redraw || screen != ui.drawn_page;
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
        s[3] = m;
        if (all || s[0] != ui.sig[0])
            draw_page_head();
        if (all || s[1] != ui.sig[1])
            draw_page_rows(0);
        if (all || s[2] != ui.sig[2])
            draw_page_rows(2);
        if (all || s[3] != ui.sig[3])
            draw_foot();
    }
    for (i = 0; i < 4u; i++)
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
            case B_GLO: page_open(PG_GLO); break;
            case B_FX:
                page_open(ui.page == PG_FX1 ? PG_FX2 : PG_FX1);   /* FX 1 first; again: FX 2, and back */
                break;
            case B_SEQ:
                ui.hold ^= 1u;
                vox.hold = ui.hold;
                msg(ui.hold ? "HOLD ON" : "HOLD OFF");
                break;
            case B_PLAY: msg("PLAY: SOON"); break;
            case B_REC: msg("CHORD MEMORY: SOON"); break;
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
                msg("CHORD CLEAR: SOON");
            else if (b == B_HOME) {              /* revert: the stored preset again */
                if (cur_index < NPRESETS) {
                    cur = *preset_at(cur_index);
                    snd_update();
                    msg("REVERTED");
                } else
                    msg("NEW SOUND: NOTHING TO REVERT");
            } else if (b == B_SAVE) {            /* INIT: a plain sound to start from */
                cur = INIT_SOUND;
                cur_index = PRESET_NEW;
                snd_update();
                msg("INIT SOUND");
            }
        } else if (!down && was && !ui.held_fired[b]) {   /* released before the hold: a tap */
            if (b == B_ARP) {
                ui.arp ^= 1u;
                msg(ui.arp ? "ARP ON: SOON" : "ARP OFF");
            } else if (b == B_SAVE)
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
    static const uint8_t PAGE_BTN[NPAGES] = {B_EDIT, B_SCL, B_ENV, B_LFO, B_FX, B_FX, B_ARP, B_GLO};
    uint32_t b, k, blink = (fm1_ms / 250u) & 1u, kb = snd.key_base;
    for (b = 0; b < NB; b++) {
        int on = 0;
        if (ui.page < NPAGES && PAGE_BTN[ui.page] == b)
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

/* the CC map: what the ISR received (sound.c snd_midi), applied to the sound here, where the parameters live */
static void cc_take(void)
{
    static uint8_t seen[16];
    uint32_t k, any = 0;
    for (k = 0; k < 16u; k++) {
        uint8_t q = vox.cc_seq[k];
        if (q != seen[k]) {
            const param_t *p = &SP[CC_PAR[k]];
            int32_t v = vox.cc_val[k];
            seen[k] = q;
            cur.p[CC_PAR[k]] = clamp8(p->lo + (v * (p->hi - p->lo) + 63) / 127, p);
            any = 1;
        }
    }
    if (any)
        snd_update();
}

static void ui_frame(void)
{
    ui.frames++;
    cc_take();
    ui_buttons();
    ui_encoders();
    snd_shed();
    ui_master();
    ui_leds();
    ui_draw();
}
