/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 R. Luke DuBois */
/* What the screen reader says (main loop, after ui_frame, only while a reader listens: sr.c). Each frame compares
 * what the screen shows with the last frame and says what changed, in full words (fm1-reader's RULES.md: no
 * abbreviations or symbols, the important thing first, short):
 *   a page          its name and its four values ("envelope page. attack 153 milliseconds, ...")
 *   a knob          its name and value, when it settles ("cutoff 2.2 kilohertz")
 *   a preset        "factory 3, violins"
 *   a message       the line at the bottom ("hold on", "arpeggiator stopped", "saved user 3"); a failure as a
 *                   warning (KEEP)
 *   SAVE            the slot, the source, the name and how to edit it; each edit as it happens
 *   WHERE           the whole screen
 * The words come from the screen's own texts through spk(), which spells out what the small screen abbreviates. */

/* the screen's tokens and how they are said (whole words; the rest is said as written, in small letters) */
static const char *const SPK_TOK[][2] = {
    {"ENS", "ensemble"},           {"DLY", "delay"},
    {"FLG", "flanger"},            {"ENV", "envelope"},
    {"OCT", "octave"},             {"ARP", "arpeggiator"},
    {"TRACK", "tracking"},         {"RETRIG", "retrigger"},
    {"LFO", "L F O"},              {"FX", "effects"},
    {"CH", "channel"},             {"CPU", "C P U"},
    {"BPM", "beats per minute"},   {"PWM", "pulse width"},
    {"CLK", "clock"},              {"REC", "record"},
    {"MIDI", "MIDI"},              {"UP-DOWN", "up down"},
    {"PLAYED", "as played"},       {"SAW+SQR", "saw plus square"},
    {"I", "1"},                    {"II", "2"},
    {"III", "3"},                  {"16'", "16 foot"},
    {"8'", "8 foot"},              {"4'", "4 foot"},
    {"1/4", "quarter notes"},      {"1/8", "eighth notes"},
    {"1/8T", "eighth note triplets"}, {"1/16", "sixteenth notes"},
    {"1/16T", "sixteenth note triplets"}, {"1/32", "thirty second notes"},
    {"ms", "milliseconds"},        {"s", "seconds"},
    {"Hz", "hertz"},               {"kHz", "kilohertz"},
    {"ct", "cents"},               {"K1", "knob 1"},
    {"K2", "knob 2"},              {"K3", "knob 3"},
    {"K4", "knob 4"},              {"*", "edited"},
    {"FLANGE", "flanger"},         {"OMNI", "omni"},
};
#define NSPK (sizeof SPK_TOK / sizeof SPK_TOK[0])

static void spk_cat(char *d, unsigned max, const char *s) { cat(d, s, max); }

static int spk_digits(const char *t)             /* t is all digits (and at least one) */
{
    if (!*t)
        return 0;
    for (; *t; t++)
        if (*t < '0' || *t > '9')
            return 0;
    return 1;
}

/* one token of screen text, spoken */
static void spk_tok(char *d, unsigned max, const char *t)
{
    char w[48];
    uint32_t i, n = str_len(t);
    if (!n)
        return;
    for (i = 0; i < NSPK; i++)
        if (str_eq(t, SPK_TOK[i][0])) {
            spk_cat(d, max, SPK_TOK[i][1]);
            return;
        }
    if ((t[0] == 'F' || t[0] == 'U') && n >= 2u && n <= 3u && spk_digits(t + 1)) {   /* F3 U07: a preset */
        const char *num = t + 1;
        while (num[0] == '0' && num[1])
            num++;
        spk_cat(d, max, t[0] == 'F' ? "factory " : "user ");
        spk_cat(d, max, num);
        return;
    }
    if (t[n - 1u] == ':') {                      /* "SAVE TO:" -> "save to," */
        str_cpy(w, t, sizeof w);
        w[n - 1u] = 0;
        spk_tok(d, max, w);
        spk_cat(d, max, ",");
        return;
    }
    if (t[n - 1u] == '%' && n > 1u) {            /* 100% -> 100 percent */
        str_cpy(w, t, sizeof w);
        w[n - 1u] = 0;
        spk_tok(d, max, w);
        spk_cat(d, max, " percent");
        return;
    }
    if ((t[0] == '+' || t[0] == '-') && n > 1u && t[1] >= '0' && t[1] <= '9') {   /* +12 -2: plus / minus */
        spk_cat(d, max, t[0] == '+' ? "plus " : "minus ");
        spk_tok(d, max, t + 1);
        return;
    }
    for (i = 0; t[i] && i + 1u < sizeof w; i++) {   /* as written, small letters, no symbols */
        char c = t[i];
        if (c >= 'A' && c <= 'Z')
            c = (char)(c - 'A' + 'a');
        else if (c == '/' || c == '|' || c == '>' || c == '<' || c == '*' || c == '_' || c == '=' || c == '#' ||
                 c == '[' || c == ']' || c == '{' || c == '}' || c == '~' || c == '^' || c == '@' || c == '&' ||
                 c == '$' || c == '+' || c == '"' || c == '\\' || c < 32 || c > 126)
            c = ' ';
        w[i] = c;
    }
    w[i] = 0;
    spk_cat(d, max, w);
}

/* screen text -> spoken text, appended to d */
static void spk(char *d, unsigned max, const char *s)
{
    char t[48];
    uint32_t k;
    while (*s) {
        while (*s == ' ')
            s++;
        for (k = 0; *s && *s != ' ' && k + 1u < sizeof t; s++)
            t[k++] = *s;
        t[k] = 0;
        if (k) {
            if (d[0] && d[str_len(d) - 1u] != ' ')
                spk_cat(d, max, " ");
            spk_tok(d, max, t);
        }
    }
}

/* ------------------------------------------------------------ what each screen says --- */
#define SPK_LEN 400u             /* a whole readout (sr_say_long cuts it into SAYs) */

static void spk_param(char *d, unsigned max, const param_t *p, int32_t v)   /* "cutoff 2.2 kilohertz" */
{
    char b[24];
    spk(d, max, p->name);
    pv_text(b, sizeof b, p, v);
    spk(d, max, b);
}

static void spk_page(char *d, unsigned max)       /* "envelope page. attack ..., decay ..., sustain ..., release ..." */
{
    static const char *const NAME[NPAGES] = {"strings", "filter", "envelope", "L F O", "effects 1, ensemble and flanger",
                                             "effects 2, delay", "arpeggiator", "global"};
    uint32_t r;
    int8_t *v;
    str_cpy(d, NAME[ui.page], max);
    cat(d, " page.", max);
    for (r = 0; r < 4u; r++) {
        const param_t *p = row_param(ui.page, r, &v);
        if (r)
            cat(d, ",", max);
        spk_param(d, max, p, *v);
    }
    cat(d, ".", max);
    if (ui.page == PG_ARP) {
        char t[8];
        fmt_int(t, set.bpm);
        cat(d, arp.ext_pub ? " MIDI clock." : " tempo ", max);
        if (!arp.ext_pub) {
            cat(d, t, max);
            cat(d, " beats per minute.", max);
        }
        if (arp.halt_pub)
            cat(d, " stopped.", max);
    }
}

static void spk_preset(char *d, unsigned max)     /* "factory 3, violins" (", edited") */
{
    char b[8];
    preset_label(b, sizeof b, cur_index);
    spk(d, max, cur_index >= NPRESETS ? "NEW SOUND" : b);
    cat(d, ",", max);
    spk(d, max, cur.name);
    if (preset_edited())
        cat(d, ", edited", max);
}

static void spk_home(char *d, unsigned max)
{
    static const uint8_t P[4] = {P_R16, P_R8, P_R4, P_CUT};
    uint32_t i;
    char t[8];
    str_cpy(d, "home. ", max);
    spk_preset(d, max);
    cat(d, ".", max);
    for (i = 0; i < 4u; i++) {
        if (i)
            cat(d, ",", max);
        spk_param(d, max, &SP[P[i]], cur.p[P[i]]);
    }
    cat(d, ". ensemble ", max);
    spk(d, max, ENS_NAME[cur.p[P_ENS]]);
    cat(d, ", octave ", max);
    fmt_int(t, set.oct);
    spk(d, max, set.oct > 0 ? (t[0] = '+', t) : t);   /* (fmt_int writes no sign for positives) */
    cat(d, ", ", max);
    spk(d, max, TRIG_NAME[set.g[G_TRIG]]);
    if (ui.hold)
        cat(d, ", hold", max);
    if (ui.arp)
        cat(d, arp.halt_pub ? ", arpeggiator stopped" : ", arpeggiator", max);
    if (ui.recording)
        cat(d, ", recording a chord", max);
    else if (chord_on())
        cat(d, ", chord memory", max);
    cat(d, ".", max);
}

static void spk_name_char(char *d, unsigned max)  /* the SAVE editor at its cursor: "position 3, letter c" */
{
    char t[4], c = ui.nm[ui.nm_cur];
    fmt_int(t, ui.nm_cur + 1);
    cat(d, "position ", max);
    cat(d, t, max);
    cat(d, c == ' ' ? ", space" : c >= '0' && c <= '9' ? ", digit " : c == '-' ? ", dash" : c == '.' ? ", dot" : ", letter ",
        max);
    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
        t[0] = c;
        t[1] = 0;
        cat(d, t, max);
    }
}

static void spk_save_name(char *d, unsigned max)  /* "name strings" or "name empty" */
{
    char n[NAME_EDIT + 1];
    uint32_t k = NAME_EDIT;
    memcpy(n, ui.nm, NAME_EDIT + 1u);
    while (k && n[k - 1u] == ' ')
        n[--k] = 0;
    cat(d, "name ", max);
    if (k)
        spk(d, max, n);
    else
        cat(d, "empty", max);
}

static void spk_save_src(char *d, unsigned max)
{
    char b[32];
    cat(d, "from ", max);
    if (!ui.save_src)
        cat(d, "the current sound", max);
    else if (ui.save_src > NFACT)
        cat(d, "init, clearing the slot", max);
    else {
        preset_label(b, sizeof b, ui.save_src - 1u);
        spk(d, max, b);
        cat(d, ",", max);
        spk(d, max, FACTORY[ui.save_src - 1u].name);
    }
}

static void spk_save_slot(char *d, unsigned max)  /* "save to user 5, now holding init" */
{
    char b[8];
    preset_label(b, sizeof b, NFACT + ui.save_slot);
    cat(d, "save to ", max);
    spk(d, max, b);
    cat(d, ", now holding", max);
    spk(d, max, user[ui.save_slot].name);
}

static void spk_save(char *d, unsigned max)
{
    str_cpy(d, "", max);
    spk_save_slot(d, max);
    cat(d, ". ", max);
    spk_save_src(d, max);
    cat(d, ". ", max);
    spk_save_name(d, max);
    cat(d, ". knob 1 moves the cursor, knob 2 changes the letter, knob 3 picks a word, knob 4 clears the name, "
           "presets picks the slot, algorithm picks what to store. press save to store, any other button cancels.",
        max);
}

static void spk_screen(char *d, unsigned max)     /* the whole screen */
{
    d[0] = 0;
    if (ui.saving)
        spk_save(d, max);
    else if (ui.page == PG_HOME)
        spk_home(d, max);
    else
        spk_page(d, max);
}

/* ------------------------------------------------------------ what changed --- */
static struct {
    uint8_t screen;              /* the page, PG_HOME, or 0xFE for SAVE */
    int8_t v[4];                 /* the four values shown (a page's rows, HOME's drawbars) */
    uint8_t index;
    char name[NAME_LEN];
    uint32_t msg_until;
    uint8_t slot, src, cur;
    char nm[NAME_EDIT + 1];
    uint8_t ext, valid;
} sk;

static void sk_values(int8_t *v)                 /* the four values the screen shows now */
{
    static const uint8_t P[4] = {P_R16, P_R8, P_R4, P_CUT};
    uint32_t r;
    int8_t *pv;
    for (r = 0; r < 4u; r++) {
        if (ui.page == PG_HOME)
            v[r] = cur.p[P[r]];
        else {
            row_param(ui.page, r, &pv);
            v[r] = *pv;
        }
    }
}

static int msg_is_preset(const char *m)          /* preset_load's own message: the preset readout says it better */
{
    uint32_t n = str_len(m);
    return (n > 8u && str_eq(m + n - 8u, " FACTORY")) || (n > 5u && str_eq(m + n - 5u, " USER"));
}

static void speak_frame(void)
{
    char d[SPK_LEN];
    uint32_t now = fm1_ms, r;
    uint8_t screen = ui.saving ? 0xFEu : ui.page;
    int8_t v[4];
    if (!sr_on()) {
        sk.valid = 0;
        sr_tick(now);
        return;
    }
    sk_values(v);
    if (sr_where() || !sk.valid) {               /* a new reader, or WHERE: the whole screen */
        spk_screen(d, sizeof d);
        sr_say_long(d, SR_NOW);
    } else {
        d[0] = 0;
        if (ui.msg_until != sk.msg_until && bottom_text()[0] && !msg_is_preset(ui.msg)) {   /* a new message */
            spk(d, sizeof d, ui.msg);
            sr_say(d, SR_NOW | (str_len(ui.msg) >= 6u && (str_eq(ui.msg + str_len(ui.msg) - 6u, "FAILED")) ? SR_KEEP : 0u));
        }
        if (screen != sk.screen) {               /* another screen */
            spk_screen(d, sizeof d);
            if (str_len(d) <= SR_MAX)
                sr_later(d, SR_NOW, 200u, now);
            else
                sr_say_long(d, SR_NOW);
        } else if (ui.saving) {                  /* SAVE: each edit */
            d[0] = 0;
            if (ui.save_slot != sk.slot)
                spk_save_slot(d, sizeof d);
            else if (ui.save_src != sk.src) {
                spk_save_src(d, sizeof d);
                cat(d, ". ", sizeof d);
                spk_save_name(d, sizeof d);
            } else if (ui.nm_cur != sk.cur)
                spk_name_char(d, sizeof d);
            else if (memcmp(ui.nm, sk.nm, NAME_EDIT)) {
                uint32_t diff = 0;
                for (r = 0; r < NAME_EDIT; r++)
                    diff += ui.nm[r] != sk.nm[r];
                if (diff == 1u)                  /* one letter: the letter */
                    spk_name_char(d, sizeof d);
                else
                    spk_save_name(d, sizeof d);
            }
            if (d[0])
                sr_later(d, SR_NOW, 120u, now);
        } else {
            if (cur_index != sk.index || !str_eq(cur.name, sk.name)) {   /* another preset (or a revert, a save) */
                d[0] = 0;
                spk_preset(d, sizeof d);
                sr_later(d, SR_NOW, 200u, now);
            } else
                for (r = 0; r < 4u; r++)
                    if (v[r] != sk.v[r]) {           /* a knob: its name and value when it settles */
                        int8_t *pv;
                        const param_t *p = ui.page == PG_HOME
                                               ? &SP[r == 3u ? P_CUT : r == 0u ? P_R16 : r == 1u ? P_R8 : P_R4]
                                               : row_param(ui.page, r, &pv);
                        d[0] = 0;
                        spk_param(d, sizeof d, p, v[r]);
                        sr_later(d, SR_NOW, 120u, now);
                        break;
                    }
            if (arp.ext_pub != sk.ext && ui.arp) /* MIDI clock came or went (not the player's doing: queued) */
                sr_say(arp.ext_pub ? "following MIDI clock" : "internal tempo", 0);
        }
    }
    sk.screen = screen;
    memcpy(sk.v, v, sizeof sk.v);
    sk.index = cur_index;
    str_cpy(sk.name, cur.name, sizeof sk.name);
    sk.msg_until = ui.msg_until;
    sk.slot = ui.save_slot;
    sk.src = ui.save_src;
    sk.cur = ui.nm_cur;
    memcpy(sk.nm, ui.nm, sizeof sk.nm);
    sk.ext = arp.ext_pub;
    sk.valid = 1;
    sr_tick(now);
}
