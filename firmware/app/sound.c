/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 R. Luke DuBois */
/* The sound (audio ISR): a divide-down string machine. 12 phase accumulators, one per pitch class, run at the
 * lowest octave (MIDI 0..11, 8.2 .. 15.4 Hz); note n of octave k reads its pitch class's phase shifted left by k,
 * which is that phase at 2^k times the frequency, so every C (every C#, ...) is phase-locked to the others as in an
 * organ's divider chain, and any number of keys can sound. Each sounding note mixes three registers: 16' (k - 1),
 * 8' (k), 4' (k + 1), folded back an octave past the ends of the range. A per-note key gate of ~3 ms keeps keys from
 * clicking; the shared envelope (ADSR, RETRIG or LEGATO) does the shaping, and the notes that were down when the last
 * key went up keep sounding through its release (paraphonic: one envelope for all).
 * Milestone 1 (docs/SPEC.md): naive saws, no filter, LFO or effects yet; the band-limited shapes, the shared filter,
 * the LFO and the effects come with their milestones. Main loop -> ISR: snd (derived numbers, 32-bit stores) and the
 * volatile requests; ISR -> main loop: the published gated set and the sounding count. */

#define NOTE_KEY0 53u            /* note key 0 is F3 (hal/fm1_input.h fm1_in.notes) */
#define MAX_SOUNDING 32u         /* notes rendered per block at most: held ones first (lowest first), then releasing */
#define CAP_MIN 8u               /* the load shedding (snd_shed) never goes below this many */
#define GATE_ONE 1024            /* a key gate fully open */
#define GATE_STEP 256            /* per block (32 frames): 4 blocks, ~2.9 ms, from shut to open */
#define ENV_ONE (1 << 24)        /* the envelope's full level, Q24 */
#define REG_AMP 5000             /* one register at 100 %, Q15 peak (before VOLUME and MASTER): F1 CLASSIC's single
                                  * note ~ -18 dBFS at the default VOLUME, a big chord into the soft knee */

/* phase increments of MIDI notes 0..11 (2^32 f / FS); octave k is these << k */
static const uint32_t INC0[12] = {796254u, 843601u, 893765u, 946911u, 1003217u, 1062871u,
                                  1126073u, 1193033u, 1263974u, 1339134u, 1418763u, 1503127u};

/* the numbers the ISR uses, worked out from the parameters in the main loop (snd_update): every field a 32-bit
 * aligned store, so the ISR reads a whole old or a whole new value (a torn set of fields for one block is harmless) */
static struct {
    uint32_t inc[12];            /* INC0 with the master tune */
    int32_t amp16, amp8, amp4;   /* the registers' levels, Q15 peak */
    int32_t atk_inc;             /* envelope: attack per block (Q24) */
    int32_t dec_coef, rel_coef;  /* decay, release: the share of the distance left per block, Q15 */
    int32_t sus;                 /* sustain level, Q24 */
    int32_t gain;                /* VOLUME x MASTER, Q8 (0 .. 256) */
    uint32_t legato;             /* trigger mode: 1 LEGATO, 0 RETRIG */
    uint32_t midi_ch;            /* 0: omni, 1..16 */
    uint32_t key_base;           /* the MIDI note of key 0 with the octave shift */
} snd;

enum { ENV_IDLE, ENV_ATK, ENV_DEC, ENV_REL };

static struct {
    uint32_t phase[12];          /* the pitch classes' accumulators (lowest octave) */
    uint16_t gate[128];          /* each note's key gate, 0 .. GATE_ONE */
    uint32_t open[4];            /* notes whose gate is not shut (bit n of word n >> 5) */
    uint32_t voiced[4];          /* notes whose gate should be open */
    uint32_t midi[4];            /* held over MIDI */
    uint32_t latch[4];           /* HOLD: notes kept after their keys went up */
    uint32_t sus[4];             /* the sustain pedal's notes */
    uint32_t prev_gated[4];      /* last block's gated set (new notes trigger the envelope) */
    uint32_t prev_input;         /* last block's: any key or MIDI note down (HOLD replaces the latched set) */
    int32_t env;                 /* the shared envelope, Q24 */
    uint8_t stage, pedal;
    int32_t mix[CTL];
    volatile uint8_t hold;       /* main loop -> ISR: HOLD on; off releases the latched notes */
    volatile uint8_t stop_req;   /* main loop -> ISR: forget every note, silence (app_audio_stop) */
    volatile uint8_t midi_off_req;   /* main loop -> ISR: release every MIDI note (the channel changed) */
    volatile uint32_t gated_pub[4];  /* ISR -> main loop: the gated notes (LEDs, the screen's dots) */
    volatile uint32_t sounding_pub;  /* ISR -> main loop: notes rendered last block */
    volatile uint32_t cap;           /* main loop -> ISR: notes rendered at most (CAP_MIN .. MAX_SOUNDING; snd_shed) */
    uint32_t cap_ms;                 /* (main loop: when the cap last rose) */
} vox;

static const uint8_t *note_oct_tbl(void)        /* n / 12 without a divide in the ISR */
{
    static uint8_t t[128];
    static uint8_t ready;
    uint32_t n;
    if (!ready) {
        for (n = 0; n < 128u; n++)
            t[n] = (uint8_t)(n / 12u);
        ready = 1;
    }
    return t;
}
static const uint8_t *NOTE_OCT;

static void bits_clear(uint32_t *s) { s[0] = s[1] = s[2] = s[3] = 0; }
static void bit_set(uint32_t *s, uint32_t n, int on)
{
    if (on)
        s[n >> 5] |= 1u << (n & 31u);
    else
        s[n >> 5] &= ~(1u << (n & 31u));
}

/* one MIDI message (rare: out of the hot loop) */
static __attribute__((noinline)) void snd_midi(uint32_t pkt)
{
    uint32_t st = (pkt >> 8) & 0xF0u, ch = (pkt >> 8) & 0x0Fu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
    if (pkt == MIDI_LOST) {                      /* events are gone: no note may hang */
        bits_clear(vox.midi);
        bits_clear(vox.sus);
        vox.pedal = 0;
        return;
    }
    if (st < 0x80u || st == 0xF0u || (snd.midi_ch && ch + 1u != snd.midi_ch))
        return;
    if (st == 0x90u && d2)
        bit_set(vox.midi, d1, 1);
    else if (st == 0x80u || st == 0x90u)
        bit_set(vox.midi, d1, 0);
    else if (st == 0xB0u && d1 == 64u) {         /* sustain pedal */
        vox.pedal = d2 >= 64u;
        if (!vox.pedal)
            bits_clear(vox.sus);
    } else if (st == 0xB0u && (d1 == 123u || d1 == 120u)) {   /* all notes off, all sound off */
        bits_clear(vox.midi);
        bits_clear(vox.sus);
        bits_clear(vox.latch);
        vox.pedal = 0;
    }
}

/* the envelope for one block: from where it is to where it will be (the samples interpolate between the two) */
static void env_block(uint32_t any_gated, uint32_t new_note)
{
    int32_t e = vox.env;
    if (new_note && (!snd.legato || vox.stage == ENV_IDLE || vox.stage == ENV_REL))
        vox.stage = ENV_ATK;                     /* from the current level: no jump */
    else if (!any_gated && vox.stage != ENV_IDLE)
        vox.stage = ENV_REL;
    switch (vox.stage) {
    case ENV_ATK:
        e += snd.atk_inc;
        if (e >= ENV_ONE) {
            e = ENV_ONE;
            vox.stage = ENV_DEC;
        }
        break;
    case ENV_DEC: {
        int32_t d = snd.sus - e;
        e += (d >> 8) * snd.dec_coef >> 7;       /* (2^16 x 2^15 fits 31 bits) */
        if (d > -32768 && d < 32768)             /* (0.2 %: the truncated steps stall short of it) */
            e = snd.sus;
        break;
    }
    case ENV_REL:
        e += (-e >> 8) * snd.rel_coef >> 7;      /* floors towards -inf: it keeps falling */
        if (e < 4096) {                          /* ~ -72 dB: the end, exact silence */
            e = 0;
            vox.stage = ENV_IDLE;
        }
        break;
    default:
        e = 0;
    }
    vox.env = e;
}

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t keys = fm1_in.notes, pkt, i, w, any = 0, fresh = 0, input = 0, rendered = 0, cap, pass;
    uint32_t gated[4], target[4], active[4], kb = snd.key_base;
    int32_t e0, e1, de, a16 = snd.amp16, a8 = snd.amp8, a4 = snd.amp4, gain = snd.gain;

    if (vox.midi_off_req) {                      /* before the queue: the old channel's notes go, what came in on the
                                                  * new channel since stays */
        vox.midi_off_req = 0;
        bits_clear(vox.midi);
        bits_clear(vox.sus);
    }
    while (midi_in_take(&pkt, 0))
        snd_midi(pkt);
    if (vox.stop_req) {                          /* after the queue: what came before the stop is forgotten too */
        vox.stop_req = 0;
        bits_clear(vox.midi);
        bits_clear(vox.latch);
        bits_clear(vox.sus);
        bits_clear(vox.voiced);
        bits_clear(vox.open);
        bits_clear(vox.prev_gated);
        vox.pedal = 0;
        vox.env = 0;
        vox.stage = ENV_IDLE;
        memset(vox.gate, 0, sizeof vox.gate);
    }

    /* what is down now: the keys at the octave shift, MIDI; then HOLD and the pedal keep what was down */
    for (w = 0; w < 4u; w++)
        gated[w] = vox.midi[w];
    for (i = 0; keys >> i; i++)
        if ((keys >> i) & 1u && kb + i < 128u)
            bit_set(gated, kb + i, 1);
    for (w = 0; w < 4u; w++)
        input |= gated[w];
    if (vox.hold) {                              /* a new chord after all keys were up replaces the latched one */
        if (input && !vox.prev_input)
            bits_clear(vox.latch);
        for (w = 0; w < 4u; w++)
            vox.latch[w] |= gated[w];
    } else
        bits_clear(vox.latch);
    vox.prev_input = input;
    if (vox.pedal)
        for (w = 0; w < 4u; w++)
            vox.sus[w] |= gated[w];
    for (w = 0; w < 4u; w++) {
        gated[w] |= vox.latch[w] | vox.sus[w];
        any |= gated[w];
        fresh |= gated[w] & ~vox.prev_gated[w];
        vox.prev_gated[w] = gated[w];
        vox.gated_pub[w] = gated[w];
    }

    e0 = vox.env;
    env_block(any, fresh);
    e1 = vox.env;
    de = (e1 - e0) >> 5;                         /* / CTL as a shift: a signed "/ 32" compiles to a hardware divide;
                                                  * the floor ends a falling block <= 31 / 2^24 low: inaudible */
    /* while keys are down their gates follow them; when the last goes up, the notes it leaves keep sounding through
     * the shared release, and close when the envelope ends */
    for (w = 0; w < 4u; w++) {
        if (any)
            vox.voiced[w] = gated[w];
        else if (vox.stage == ENV_IDLE)
            vox.voiced[w] = 0;
        target[w] = vox.voiced[w];
        active[w] = target[w] | vox.open[w];
    }

    for (i = 0; i < n; i++)
        vox.mix[i] = 0;
    /* two passes: the held notes first (lowest first), then the releasing ones in what the cap leaves; a releasing
     * note past the cap is dropped (its gate shut: it was fading anyway), a held one past it waits silent */
    cap = vox.cap;
    cap = cap < CAP_MIN ? CAP_MIN : cap > MAX_SOUNDING ? MAX_SOUNDING : cap;
    for (pass = 0; pass < 8u; pass++) {
        uint32_t w = pass & 3u, held = pass < 4u;
        uint32_t bits = held ? active[w] & target[w] : active[w] & ~target[w];
        while (bits) {
            uint32_t b = (uint32_t)__builtin_ctz(bits), note = (w << 5) + b, k, k16, k4, ph, inc;
            int32_t g0 = vox.gate[note], g1, gs, dg;
            bits &= bits - 1u;
            if (rendered >= cap) {
                if (!held) {
                    vox.gate[note] = 0;
                    bit_set(vox.open, note, 0);
                }
                continue;
            }
            g1 = held ? g0 + GATE_STEP : g0 - GATE_STEP;
            g1 = g1 < 0 ? 0 : g1 > GATE_ONE ? GATE_ONE : g1;
            vox.gate[note] = (uint16_t)g1;
            bit_set(vox.open, note, g1 > 0);
            if (!g0 && !g1)
                continue;
            rendered++;
            k = NOTE_OCT[note];
            k16 = k ? k - 1u : k;                /* folded back past the ends of the range */
            k4 = k < 10u ? k + 1u : k;
            ph = vox.phase[note - k * 12u];
            inc = snd.inc[note - k * 12u];
            gs = g0 << 5;                        /* Q15, interpolated across the block */
            dg = g1 - g0;
            for (i = 0; i < n; i++) {
                int32_t v = ((int32_t)(ph << k16) >> 16) * a16 + ((int32_t)(ph << k) >> 16) * a8 +
                            ((int32_t)(ph << k4) >> 16) * a4;
                vox.mix[i] += (v >> 15) * gs >> 15;
                gs += dg;
                ph += inc;
            }
        }
    }
    for (i = 0; i < 12u; i++)
        vox.phase[i] += snd.inc[i] * n;
    vox.sounding_pub = rendered;

    for (i = 0; i < n; i++) {
        int32_t x = vox.mix[i], ax;
        x = x > 65535 ? 65535 : x < -65535 ? -65535 : x;
        x = x * ((e0 + de * (int32_t)i) >> 9) >> 15;          /* the envelope, Q15 */
        x = x * gain >> 8;
        ax = x < 0 ? -x : x;
        if (ax > 20000)                          /* a soft knee, then the ceiling: full chords do not crack */
            ax = 20000 + ((ax - 20000) >> 2);
        ax = ax > 32767 ? 32767 : ax;
        x = x < 0 ? -ax : ax;
        out[2u * i] = x;
        out[2u * i + 1u] = x;
    }
}

static void app_audio_stop(void) { vox.stop_req = 1; }

/* main loop: the engine's numbers from the parameters (on a change, and at boot) */
static int32_t master_q8 = 256;  /* the MASTER pot, 0 .. 256 (ui.c reads it) */
static void snd_update(void)
{
    /* 32-bit divides only: the target's runtime has no 64-bit one (__udivdi3) */
    static const uint32_t CENT_UP = 1074362221u, CENT_DN = 1073121785u;   /* 2^(+-1/1200) in Q30 */
    static const uint32_t ATK_K = 12173943u;    /* ENV_ONE x CTL x 1000 / FS: the attack per block is ATK_K / ms */
    /* decay and release: exponential with a time constant of a sixth of the time shown, so the time shown is where
     * the sound is ~52 dB down and the end (~72 dB) comes at ~1.4x: CTL x 1000 x 32768 x 6 / FS, then / ms */
    static const uint32_t COEF_K = 142663u;
    uint64_t t = 1u << 30;
    int32_t c = set.g[G_TUNE], i;
    uint32_t ms;
    for (i = 0; i < (c < 0 ? -c : c); i++)
        t = t * (c > 0 ? CENT_UP : CENT_DN) >> 30;
    for (i = 0; i < 12; i++)
        snd.inc[i] = (uint32_t)((uint64_t)INC0[i] * t >> 30);
    snd.amp16 = cur.p[P_R16] * REG_AMP / 100;
    snd.amp8 = cur.p[P_R8] * REG_AMP / 100;
    snd.amp4 = cur.p[P_R4] * REG_AMP / 100;
    ms = pv_ms(K_ATK, cur.p[P_ATK]);
    snd.atk_inc = (int32_t)(ATK_K / ms);
    if (snd.atk_inc < 1)
        snd.atk_inc = 1;
    ms = pv_ms(K_DEC, cur.p[P_DEC]);
    snd.dec_coef = (int32_t)(COEF_K / ms);
    snd.dec_coef = snd.dec_coef > 32767 ? 32767 : snd.dec_coef < 1 ? 1 : snd.dec_coef;
    ms = pv_ms(K_REL, cur.p[P_REL]);
    snd.rel_coef = (int32_t)(COEF_K / ms);
    snd.rel_coef = snd.rel_coef > 32767 ? 32767 : snd.rel_coef < 1 ? 1 : snd.rel_coef;
    snd.sus = (ENV_ONE >> 2) * cur.p[P_SUS] / 25;   /* exact at 25, 50, 100 %; 2^22 x 100 fits 31 bits */
    snd.gain = set.g[G_VOL] * master_q8 / 100;
    snd.legato = set.g[G_TRIG] ? 1u : 0u;
    snd.midi_ch = (uint32_t)set.g[G_MIDICH];
    snd.key_base = (uint32_t)((int32_t)NOTE_KEY0 + 12 * set.oct);
}

/* main loop, every frame: the load shedding. audio_cpu_q8 is the last half buffer's ISR time in 1/256 of the half
 * (audio.c); above ~80 % the main loop starves (the watchdog, the update entry) and the sound crackles, so the
 * number of notes rendered falls fast (2 a frame) and climbs back slowly (1 every 250 ms) once the load is under
 * ~60 %. Felucca sheds voices above 85 % the same way. */
#define SHED_HI 205u             /* 80 % */
#define SHED_LO 154u             /* 60 % */
static void snd_shed(void)
{
    uint32_t load = audio_cpu_q8, cap = vox.cap;
    if (load > SHED_HI && cap > CAP_MIN) {
        vox.cap = cap - 2u < CAP_MIN ? CAP_MIN : cap - 2u;
        vox.cap_ms = fm1_ms;
    } else if (load < SHED_LO && cap < MAX_SOUNDING && fm1_ms - vox.cap_ms >= 250u) {
        vox.cap = cap + 1u;
        vox.cap_ms = fm1_ms;
    }
}

static void snd_init(void)
{
    NOTE_OCT = note_oct_tbl();
    vox.cap = MAX_SOUNDING;
    snd_update();
}
