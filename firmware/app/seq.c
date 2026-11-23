/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 R. Luke DuBois */
/* Notes before they sound (audio ISR, once per block, called from sound.c app_audio): chord memory, the arpeggiator
 * and its clock, MIDI note out.
 *
 *   chord     a stored chord (REC, keys, REC: semitones above its lowest note) turns each key or MIDI note into that
 *             chord on it; the expanded notes then go on as if played (HOLD, the pedal, the arp see them).
 *   arp       when ARP is on, the held (and latched) notes are not played together: one at a time, UP, DOWN,
 *             UP-DOWN, RANDOM or PLAYED (the order they went down), over 1..4 octaves, a step every RATE (1/4 .. 1/32,
 *             triplets) of the tempo, sounding GATE % of the step. The clock counts samples (the internal tempo, 40 ..
 *             240 BPM) or follows MIDI clock (24 a quarter) while it comes in; MIDI start / continue / stop and PLAY
 *             run and stop it. Steps land on block boundaries (32 frames, 0.7 ms).
 *   MIDI out  what the panel plays goes out on USB: the keys (as the chord expands them, HOLD keeping them) or, with
 *             the arp on, its notes; on the chosen channel (OMNI: channel 1). Notes that came in over MIDI are not
 *             echoed (no loops through a DAW), except as the arp's input.
 * The main loop sets seqp (sound.c snd_update) and asks through volatile fields; the ISR owns the rest. */

#define ARP_MAXN 16u             /* notes the arp takes (the lowest 16) and the PLAYED order remembers */
#define OUT_VEL 100u             /* the velocity of the notes sent */
#define OUT_PER_BLOCK 16u        /* MIDI out messages a block at most (the ring holds 64; the rest goes next block) */

static struct {
    uint32_t on;                 /* ARP on */
    uint32_t mode;               /* G_ARPM: 0 UP, 1 DOWN, 2 UP-DOWN, 3 RANDOM, 4 PLAYED */
    uint32_t tps;                /* MIDI clock ticks a step (24 / steps a quarter) */
    uint32_t oct;                /* 1..4 */
    uint32_t step_q8;            /* the internal clock's step, samples x 256 */
    uint32_t gfrac;              /* GATE as a share of the step, x 256 */
    uint32_t chord_n;            /* notes in the chord (0: chord memory off) */
    int8_t chord[CHORD_MAX];     /* semitones above the lowest */
    uint32_t out_ch;             /* MIDI out channel, 0..15 */
} seqp;

static struct {
    uint32_t ph_q8;              /* the internal clock: samples into the step, x 256 */
    uint32_t idx;                /* the position in the pattern */
    int32_t note;                /* the note the arp sounds now, -1: none */
    uint32_t gate_q8;            /* samples left in its gate, x 256 */
    uint8_t order[ARP_MAXN], n_order;   /* PLAYED: the notes in the order they went down */
    uint32_t prev_in[4];
    uint32_t ticks;              /* MIDI clock ticks since start */
    uint32_t since_tick;         /* samples since the last tick (> FS / 2: the external clock has stopped) */
    uint32_t tick_q8;            /* samples a tick, smoothed, x 256 */
    uint32_t step_due;           /* steps the clock asks for */
    uint32_t rnd;                /* RANDOM's xorshift */
    uint32_t out[4];             /* the notes on at MIDI out */
    uint32_t prev_any;           /* the last block had input notes (the first key steps at once) */
    uint8_t ext;                 /* MIDI clock is coming in */
    uint8_t halt;                /* the transport stopped (PLAY, MIDI stop); 0 at boot: running */
    volatile uint8_t play_req;   /* main loop -> ISR: PLAY pressed (run <-> stop) */
    volatile uint8_t halt_pub, ext_pub;   /* ISR -> main loop: the transport, the clock (screen, LEDs) */
    volatile int32_t note_pub;   /* ISR -> main loop, tests: the arp's note now */
} arp;

static void seq_bit(uint32_t *s, uint32_t n) { s[n >> 5] |= 1u << (n & 31u); }

/* CCs and Program Changes the main loop sends (a knob turned, a preset changed): midi_out is for one context, and the
 * notes go out from here, so the main loop queues its messages and the ISR sends them after its notes. One writer,
 * one reader: the main loop moves w, the ISR r. */
#define CCQ 32u
static struct {
    volatile uint32_t q[CCQ];    /* status | d1 << 8 | d2 << 16 */
    volatile uint32_t w, r;
} ccq;

static int ccq_push(uint32_t st, uint32_t d1, uint32_t d2)   /* main loop: 0 queued, -1 full (try again later) */
{
    if (ccq.w - ccq.r >= CCQ)
        return -1;
    ccq.q[ccq.w % CCQ] = (st & 0xFFu) | (d1 & 0x7Fu) << 8 | (d2 & 0x7Fu) << 16;
    RING_PUBLISH();
    ccq.w++;
    return 0;
}

/* a system real-time message (snd_midi): the clock, start, continue, stop */
static __attribute__((noinline)) void seq_rt(uint32_t st)
{
    if (st == 0xF8u) {                           /* a tick: a step every tps of them, from the first after start */
        uint32_t tps = seqp.tps ? seqp.tps : 6u;
        if (arp.ext && arp.since_tick < FS / 2u) /* samples a tick, smoothed over ~8 ticks */
            arp.tick_q8 = arp.tick_q8 ? arp.tick_q8 - (arp.tick_q8 >> 3) + (arp.since_tick << 5) : arp.since_tick << 8;
        if (arp.ticks % tps == 0u)
            arp.step_due++;
        arp.ticks++;
        arp.since_tick = 0;
        arp.ext = 1;
    } else if (st == 0xFAu) {                    /* start: from the top, on the next tick */
        arp.halt = 0;
        arp.ticks = 0;
        arp.idx = 0;
        arp.step_due = 0;
    } else if (st == 0xFBu)                      /* continue */
        arp.halt = 0;
    else if (st == 0xFCu) {                      /* stop */
        arp.halt = 1;
        arp.note = -1;
    }
}

/* each note of in, as the stored chord on it (in place) */
static void chord_expand(uint32_t *in)
{
    uint32_t w, out[4] = {0, 0, 0, 0}, k;
    if (seqp.chord_n < 2u)
        return;
    for (w = 0; w < 4u; w++) {
        uint32_t bits = in[w];
        while (bits) {
            uint32_t n = (w << 5) + (uint32_t)__builtin_ctz(bits);
            bits &= bits - 1u;
            for (k = 0; k < seqp.chord_n && k < CHORD_MAX; k++)
                if (n + (uint32_t)seqp.chord[k] < 128u)
                    seq_bit(out, n + (uint32_t)seqp.chord[k]);
        }
    }
    for (w = 0; w < 4u; w++)
        in[w] = out[w];
}

/* PLAYED: the press order, kept from the input's changes */
static void arp_order(const uint32_t *in)
{
    uint32_t w, i, j;
    for (i = j = 0; i < arp.n_order; i++)        /* released notes out */
        if ((in[arp.order[i] >> 5] >> (arp.order[i] & 31u)) & 1u)
            arp.order[j++] = arp.order[i];
    arp.n_order = (uint8_t)j;
    for (w = 0; w < 4u; w++) {                   /* new notes in, at the end */
        uint32_t bits = in[w] & ~arp.prev_in[w];
        while (bits && arp.n_order < ARP_MAXN) {
            arp.order[arp.n_order++] = (uint8_t)((w << 5) + (uint32_t)__builtin_ctz(bits));
            bits &= bits - 1u;
        }
        arp.prev_in[w] = in[w];
    }
}

/* the next note of the pattern (rare: once a step) */
static __attribute__((noinline)) int32_t arp_next(const uint32_t *in)
{
    uint8_t list[ARP_MAXN];
    uint32_t c = 0, w, len, k, pos = arp.idx++;
    int32_t n;
    if (seqp.mode == 4u) {                       /* PLAYED */
        for (c = 0; c < arp.n_order; c++)
            list[c] = arp.order[c];
    } else
        for (w = 0; w < 4u && c < ARP_MAXN; w++) {
            uint32_t bits = in[w];
            while (bits && c < ARP_MAXN) {
                list[c++] = (uint8_t)((w << 5) + (uint32_t)__builtin_ctz(bits));
                bits &= bits - 1u;
            }
        }
    if (!c)
        return -1;
    len = c * (seqp.oct ? seqp.oct : 1u);
    switch (seqp.mode) {
    case 1: k = len - 1u - pos % len; break;     /* DOWN */
    case 2:                                      /* UP-DOWN: the ends once */
        if (len < 2u)
            k = 0;
        else {
            uint32_t m = pos % (2u * len - 2u);
            k = m < len ? m : 2u * len - 2u - m;
        }
        break;
    case 3:                                      /* RANDOM */
        arp.rnd ^= arp.rnd << 13;
        arp.rnd ^= arp.rnd >> 17;
        arp.rnd ^= arp.rnd << 5;
        k = arp.rnd % len;
        break;
    default: k = pos % len;                      /* UP, PLAYED */
    }
    n = (int32_t)list[k % c] + 12 * (int32_t)(k / c);
    while (n > 127)                              /* past the top: folded back an octave */
        n -= 12;
    return n;
}

/* the arp for one block: in (the held / latched notes) -> out (what sounds) */
static void arp_block(const uint32_t *in, uint32_t *out, uint32_t n)
{
    uint32_t any = in[0] | in[1] | in[2] | in[3];
    if (arp.play_req) {
        arp.play_req = 0;
        arp.halt ^= 1u;
        if (!arp.halt) {
            arp.idx = 0;
            arp.ph_q8 = 0;
            arp.step_due = 1;                    /* (PLAY: the first step at once) */
        }
    }
    if (!arp.rnd)
        arp.rnd = 0x9E3779B9u;
    arp.since_tick += n;
    if (arp.since_tick > FS / 2u)                /* no tick for half a second: the internal clock again */
        arp.ext = 0;
    arp_order(in);
    if (!arp.ext && !arp.halt) {                 /* the internal clock */
        if (arp.note < 0 && !arp.step_due && any && arp.prev_any == 0u)
            arp.step_due = 1, arp.ph_q8 = 0;     /* the first key: a step at once */
        arp.ph_q8 += n << 8;
        if (arp.ph_q8 >= seqp.step_q8) {
            arp.ph_q8 -= seqp.step_q8;
            if (arp.ph_q8 >= seqp.step_q8)       /* (the tempo just went up a lot) */
                arp.ph_q8 = 0;
            arp.step_due++;
        }
    }
    arp.prev_any = any;
    if (arp.note >= 0) {                         /* the gate */
        uint32_t d = n << 8;
        arp.gate_q8 = arp.gate_q8 > d ? arp.gate_q8 - d : 0u;
        if (!arp.gate_q8)
            arp.note = -1;
    }
    if (arp.step_due) {
        arp.step_due = 0;
        if (!arp.halt) {
            uint32_t len = arp.ext && arp.tick_q8 ? arp.tick_q8 * (seqp.tps ? seqp.tps : 6u) : seqp.step_q8;
            arp.note = arp_next(in);
            arp.gate_q8 = (len >> 8) * seqp.gfrac;
            if (!any)
                arp.idx = 0;
        }
    }
    if (!any || arp.halt)
        arp.note = -1;
    if (!any)
        arp.idx = 0;
    out[0] = out[1] = out[2] = out[3] = 0;
    if (arp.note >= 0)
        seq_bit(out, (uint32_t)arp.note);
    arp.note_pub = arp.note;
    arp.halt_pub = arp.halt;
    arp.ext_pub = arp.ext;
}

/* MIDI out: the notes that changed since the last block (offs first), at most OUT_PER_BLOCK a block */
static void seq_out(const uint32_t *want)
{
    uint32_t w, sent = 0, ch = seqp.out_ch & 15u;
    for (w = 0; w < 4u && sent < OUT_PER_BLOCK; w++) {
        uint32_t off = arp.out[w] & ~want[w];
        while (off && sent < OUT_PER_BLOCK) {
            uint32_t b = (uint32_t)__builtin_ctz(off);
            off &= off - 1u;
            midi_out(0x80u | ch, (w << 5) + b, 0);
            arp.out[w] &= ~(1u << b);
            sent++;
        }
    }
    for (w = 0; w < 4u && sent < OUT_PER_BLOCK; w++) {
        uint32_t on = want[w] & ~arp.out[w];
        while (on && sent < OUT_PER_BLOCK) {
            uint32_t b = (uint32_t)__builtin_ctz(on);
            on &= on - 1u;
            midi_out(0x90u | ch, (w << 5) + b, OUT_VEL);
            arp.out[w] |= 1u << b;
            sent++;
        }
    }
    while (ccq.r != ccq.w && sent < OUT_PER_BLOCK) {   /* then the main loop's CCs and Program Changes */
        uint32_t m = ccq.q[ccq.r % CCQ];
        RING_PUBLISH();
        midi_out(m & 0xFFu, (m >> 8) & 0x7Fu, (m >> 16) & 0x7Fu);
        ccq.r++;
        sent++;
    }
}
