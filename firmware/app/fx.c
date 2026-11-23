/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 R. Luke DuBois */
/* The effects (audio ISR, after the envelope): ensemble -> flanger -> delay, mono in, stereo out, in float on the FPU
 * (FM1_FPU: one function, fx_block; the main loop hands it integers only, so no soft float runs anywhere).
 *
 *   ensemble  the string machine's three-phase chorus: three taps of one short delay line, each swept by a slow sine
 *             (ENS RATE) plus a fast one at 10x, the three 120 degrees apart, as the BBD ensembles of the 1970s did;
 *             taps 1 and 3 go left and right, tap 2 to both. ALGORITHM picks the type: OFF, I (slow only, subtle),
 *             II (slow + fast, the classic), III (deep), FLANGE (no ensemble: the flanger alone). ENS DEPTH scales the
 *             sweep (60 %: the type's own), 0 % leaves three still taps (no movement, left = right).
 *   flanger   a short delay (0.3 .. 3.8 ms) swept by a triangle (FLG RATE), with feedback, left and right swept in
 *             opposite phase; FLANGER sets feedback and mix together (0: off). With FLANGE it runs at >= 60 %.
 *   reverb    a four-line feedback delay network after the delay (REVERB, REV SIZE), below at rv_buf
 *   delay     a stereo echo of up to 1 s: DLY TIME (a change glides, like tape, no click), FEEDBACK (0 .. 95 %), a
 *             one-pole low-pass in the loop (DLY TONE: 0 % dark .. 100 % open), DLY MIX added to the dry signal.
 *             Its lines are int16 in the pool (2 x 45 056 samples, 176 KB): a store truncates towards 0, so every tail
 *             ends in exact silence.
 * Big buffers in the pool (zeroed at boot: all-zero is a silent, valid start). */

/* sin(2 pi i / 256), i = 0..256 (the LFOs, once per block: a lookup and an interpolation; there is no libm) */
static const float FX_SIN[257] = {
    0.000000f, 0.024541f, 0.049068f, 0.073565f, 0.098017f, 0.122411f, 0.146730f, 0.170962f,
    0.195090f, 0.219101f, 0.242980f, 0.266713f, 0.290285f, 0.313682f, 0.336890f, 0.359895f,
    0.382683f, 0.405241f, 0.427555f, 0.449611f, 0.471397f, 0.492898f, 0.514103f, 0.534998f,
    0.555570f, 0.575808f, 0.595699f, 0.615232f, 0.634393f, 0.653173f, 0.671559f, 0.689541f,
    0.707107f, 0.724247f, 0.740951f, 0.757209f, 0.773010f, 0.788346f, 0.803208f, 0.817585f,
    0.831470f, 0.844854f, 0.857729f, 0.870087f, 0.881921f, 0.893224f, 0.903989f, 0.914210f,
    0.923880f, 0.932993f, 0.941544f, 0.949528f, 0.956940f, 0.963776f, 0.970031f, 0.975702f,
    0.980785f, 0.985278f, 0.989177f, 0.992480f, 0.995185f, 0.997290f, 0.998795f, 0.999699f,
    1.000000f, 0.999699f, 0.998795f, 0.997290f, 0.995185f, 0.992480f, 0.989177f, 0.985278f,
    0.980785f, 0.975702f, 0.970031f, 0.963776f, 0.956940f, 0.949528f, 0.941544f, 0.932993f,
    0.923880f, 0.914210f, 0.903989f, 0.893224f, 0.881921f, 0.870087f, 0.857729f, 0.844854f,
    0.831470f, 0.817585f, 0.803208f, 0.788346f, 0.773010f, 0.757209f, 0.740951f, 0.724247f,
    0.707107f, 0.689541f, 0.671559f, 0.653173f, 0.634393f, 0.615232f, 0.595699f, 0.575808f,
    0.555570f, 0.534998f, 0.514103f, 0.492898f, 0.471397f, 0.449611f, 0.427555f, 0.405241f,
    0.382683f, 0.359895f, 0.336890f, 0.313682f, 0.290285f, 0.266713f, 0.242980f, 0.219101f,
    0.195090f, 0.170962f, 0.146730f, 0.122411f, 0.098017f, 0.073565f, 0.049068f, 0.024541f,
    0.000000f, -0.024541f, -0.049068f, -0.073565f, -0.098017f, -0.122411f, -0.146730f, -0.170962f,
    -0.195090f, -0.219101f, -0.242980f, -0.266713f, -0.290285f, -0.313682f, -0.336890f, -0.359895f,
    -0.382683f, -0.405241f, -0.427555f, -0.449611f, -0.471397f, -0.492898f, -0.514103f, -0.534998f,
    -0.555570f, -0.575808f, -0.595699f, -0.615232f, -0.634393f, -0.653173f, -0.671559f, -0.689541f,
    -0.707107f, -0.724247f, -0.740951f, -0.757209f, -0.773010f, -0.788346f, -0.803208f, -0.817585f,
    -0.831470f, -0.844854f, -0.857729f, -0.870087f, -0.881921f, -0.893224f, -0.903989f, -0.914210f,
    -0.923880f, -0.932993f, -0.941544f, -0.949528f, -0.956940f, -0.963776f, -0.970031f, -0.975702f,
    -0.980785f, -0.985278f, -0.989177f, -0.992480f, -0.995185f, -0.997290f, -0.998795f, -0.999699f,
    -1.000000f, -0.999699f, -0.998795f, -0.997290f, -0.995185f, -0.992480f, -0.989177f, -0.985278f,
    -0.980785f, -0.975702f, -0.970031f, -0.963776f, -0.956940f, -0.949528f, -0.941544f, -0.932993f,
    -0.923880f, -0.914210f, -0.903989f, -0.893224f, -0.881921f, -0.870087f, -0.857729f, -0.844854f,
    -0.831470f, -0.817585f, -0.803208f, -0.788346f, -0.773010f, -0.757209f, -0.740951f, -0.724247f,
    -0.707107f, -0.689541f, -0.671559f, -0.653173f, -0.634393f, -0.615232f, -0.595699f, -0.575808f,
    -0.555570f, -0.534998f, -0.514103f, -0.492898f, -0.471397f, -0.449611f, -0.427555f, -0.405241f,
    -0.382683f, -0.359895f, -0.336890f, -0.313682f, -0.290285f, -0.266713f, -0.242980f, -0.219101f,
    -0.195090f, -0.170962f, -0.146730f, -0.122411f, -0.098017f, -0.073565f, -0.049068f, -0.024541f,
    -0.000000f,
};

#define ENS_LEN 2048u            /* the ensemble line: 46 ms, the deepest sweep needs ~16 ms */
#define FLG_LEN 256u             /* the flanger lines: 5.8 ms */
#define DLY_LEN 45056u           /* the delay lines: 1.02 s */

static float ens_buf[ENS_LEN] __attribute__((section(".pool")));
static float flg_buf[2][FLG_LEN] __attribute__((section(".pool")));
static int16_t dly_buf[2][DLY_LEN] __attribute__((section(".pool")));

/* the reverb: a four-line feedback delay network (Jot's form) behind two allpass diffusers. Its lines' lengths are
 * mutually prime, 67 .. 89 ms; their outputs are mixed by a 4 x 4 Hadamard matrix (lossless: the decay is the gain
 * alone), each through a one-pole low-pass (damping), and fed back with the input. REV SIZE sets the gain (0.45 .. 0.93:
 * ~0.7 s .. ~7 s to -60 dB) and darkens the damping with it; lines 1 + 3 go left, 2 + 4 right. 56 KB of pool. */
#define RV_N 4u
static const uint16_t RV_LEN[RV_N] = {2939, 3203, 3557, 3923};
static const uint16_t RV_AP[2] = {347, 113};
#define RV_TOTAL (2939u + 3203u + 3557u + 3923u)
#define RV_LONGEST 3923u
static float rv_buf[RV_TOTAL] __attribute__((section(".pool")));
static float rv_ap[347u + 113u] __attribute__((section(".pool")));

/* the parameters, set by the main loop (sound.c snd_update), integers only: 32-bit stores, read whole by the ISR */
static struct {
    int32_t ens_type;            /* 0 OFF, 1 I, 2 II, 3 III, 4 FLANGE */
    int32_t ens_depth;           /* ENS DEPTH %, x 128 (14-bit) */
    uint32_t ens_inc;            /* the slow sine's phase step per block (the fast one: 10x) */
    int32_t flg_amt;             /* FLANGER %, x 128 */
    uint32_t flg_inc;            /* the flanger's triangle, per block */
    int32_t dly_len;             /* DLY TIME in samples */
    int32_t dly_fb, dly_mix, dly_tone;   /* %, x 128 */
    int32_t dly_mode;            /* 0 stereo, 1 ping-pong */
    int32_t rv_mix, rv_size;     /* REVERB, REV SIZE: %, x 128 (14-bit) */
    uint32_t dly_sync_f24;       /* DLY SYNC: the time in 24ths of a quarter (0: FREE, dly_len from DLY TIME) */
} fxp;

static struct {
    uint32_t ens_w, ens_slow, ens_fast;   /* the ensemble line's write index, its two LFO phases */
    uint32_t flg_w, flg_ph;
    uint32_t dly_w;
    float dly_cur;               /* the delay time now (samples), gliding to fxp.dly_len */
    float dly_lp[2];             /* the tone filter in each feedback loop */
    uint32_t dly_on;             /* the last block had DLY MIX above 0 (turning it on starts at the set time) */
    int32_t dly_len_ext;         /* DLY SYNC under MIDI clock: the time from its tempo (0: fxp.dly_len) */
    uint16_t rv_pos[RV_N], rv_appos[2];
    float rv_lp[RV_N];           /* each line's damping */
    uint32_t rv_tail;            /* samples the reverb still runs after REVERB went to 0 (its lines decay to 0) */
    uint32_t rv_quiet;           /* after a stop: samples it writes zeros and adds nothing (one pass, no clear) */
    uint32_t dly_quiet;          /* samples the delay still writes zeros and adds nothing (after a stop: one pass
                                  * through its lines silences them without a 176 KB clear in the ISR) */
} fxs;

static FM1_FPU float fx_sin(uint32_t ph)         /* sin(2 pi ph / 2^32), table + interpolation */
{
    uint32_t i = ph >> 24;
    float f = (float)((ph >> 8) & 0xFFFFu) * (1.0f / 65536.0f);
    return FX_SIN[i] + (FX_SIN[i + 1u] - FX_SIN[i]) * f;
}

/* the ensemble's sweep for one tap at given LFO phases: its delay in samples */
static FM1_FPU float ens_delay(uint32_t slow, uint32_t fast, float base, float ds, float df)
{
    return base + ds * fx_sin(slow) + df * fx_sin(fast);
}

static FM1_FPU __attribute__((noinline)) void fx_block(const int32_t *in, int32_t *outl, int32_t *outr, uint32_t n)
{
    /* per type: base delay, slow and fast sweep (ms), wet share */
    static const float ENS_BASE[5] = {0, 7.0f, 7.0f, 9.0f, 0}, ENS_SLOW[5] = {0, 1.0f, 1.5f, 2.5f, 0},
                       ENS_FAST[5] = {0, 0.0f, 0.3f, 0.5f, 0},
                       /* wet shares 0.5, 0.7, 0.75: dry sqrt(1 - w), taps sqrt(w / 1.25) */
                       ENS_DRY[5] = {1, 0.7071f, 0.5477f, 0.5f, 1}, ENS_TAP[5] = {0, 0.6325f, 0.7483f, 0.7746f, 0};
    float l[CTL], r[CTL];
    int32_t type = fxp.ens_type, flg = fxp.flg_amt, j;
    uint32_t i;
    if (type < 0 || type > 4)
        type = 0;
    if (type == 4 && flg < 60 * 128)
        flg = 60 * 128;

    /* ---- ensemble */
    if (type >= 1 && type <= 3) {
        /* equal power: the dry share a and the taps' b (two taps, 1 and 1/2) so that a^2 + 1.25 b^2 = 1: the swept,
         * incoherent sum keeps the level of the dry sound (at ENS DEPTH 0 the still taps add up ~4 dB louder) */
        const float ms = FS / 1000.0f, k = (float)fxp.ens_depth * (1.0f / (60.0f * 128.0f));
        const float a = ENS_DRY[type], b = ENS_TAP[type];
        float base = ENS_BASE[type] * ms, ds = ENS_SLOW[type] * ms * k, df = ENS_FAST[type] * ms * k;
        float d0[3], dd[3];
        uint32_t s0 = fxs.ens_slow, f0 = fxs.ens_fast, s1 = s0 + fxp.ens_inc, f1 = f0 + fxp.ens_inc * 10u, w = fxs.ens_w;
        const uint32_t third = 0x55555555u;
        for (j = 0; j < 3; j++) {                /* each tap's delay at the block's start and its step a sample */
            float a = ens_delay(s0 + third * (uint32_t)j, f0 + third * (uint32_t)j, base, ds, df);
            float b = ens_delay(s1 + third * (uint32_t)j, f1 + third * (uint32_t)j, base, ds, df);
            d0[j] = a;
            dd[j] = (b - a) * (1.0f / CTL);
        }
        for (i = 0; i < n; i++) {
            float x = (float)in[i], t[3];
            ens_buf[w & (ENS_LEN - 1u)] = x;
            for (j = 0; j < 3; j++) {
                float d = d0[j] + dd[j] * (float)i, fr;
                int32_t di = (int32_t)d;
                fr = d - (float)di;
                t[j] = ens_buf[(w - (uint32_t)di) & (ENS_LEN - 1u)] * (1.0f - fr) +
                       ens_buf[(w - (uint32_t)di - 1u) & (ENS_LEN - 1u)] * fr;
            }
            l[i] = a * x + b * (t[0] + 0.5f * t[1]);
            r[i] = a * x + b * (t[2] + 0.5f * t[1]);
            w++;
        }
        fxs.ens_w = w;
        fxs.ens_slow = s1;
        fxs.ens_fast = f1;
    } else
        for (i = 0; i < n; i++)
            l[i] = r[i] = (float)in[i];

    /* ---- flanger */
    if (flg > 0) {
        const float ms = FS / 1000.0f, amt = (float)flg * (0.01f / 128.0f), fb = 0.7f * amt, mix = 0.7f * amt;
        uint32_t w = fxs.flg_w, p0 = fxs.flg_ph, p1 = p0 + fxp.flg_inc;
        float tl0, tl1, tr0, tr1, dl, dr, sl, sr;
        /* a triangle 0..1 from a phase: |2 x - 1|, and the other side half a cycle later */
        tl0 = (float)(p0 >> 8) * (1.0f / 8388608.0f) - 1.0f;
        tl1 = (float)(p1 >> 8) * (1.0f / 8388608.0f) - 1.0f;
        tr0 = (float)((p0 + 0x80000000u) >> 8) * (1.0f / 8388608.0f) - 1.0f;
        tr1 = (float)((p1 + 0x80000000u) >> 8) * (1.0f / 8388608.0f) - 1.0f;
        tl0 = tl0 < 0 ? -tl0 : tl0;
        tl1 = tl1 < 0 ? -tl1 : tl1;
        tr0 = tr0 < 0 ? -tr0 : tr0;
        tr1 = tr1 < 0 ? -tr1 : tr1;
        dl = (0.3f + 3.5f * tl0) * ms;
        dr = (0.3f + 3.5f * tr0) * ms;
        sl = ((0.3f + 3.5f * tl1) * ms - dl) * (1.0f / CTL);
        sr = ((0.3f + 3.5f * tr1) * ms - dr) * (1.0f / CTL);
        for (i = 0; i < n; i++) {
            float d = dl + sl * (float)i, fr, y;
            int32_t di = (int32_t)d;
            fr = d - (float)di;
            y = flg_buf[0][(w - (uint32_t)di) & (FLG_LEN - 1u)] * (1.0f - fr) +
                flg_buf[0][(w - (uint32_t)di - 1u) & (FLG_LEN - 1u)] * fr;
            {
                float v = l[i] + fb * y;
                flg_buf[0][w & (FLG_LEN - 1u)] = v > -0.5f && v < 0.5f ? 0.0f : v;   /* tails end at 0 */
            }
            l[i] += mix * y;
            d = dr + sr * (float)i;
            di = (int32_t)d;
            fr = d - (float)di;
            y = flg_buf[1][(w - (uint32_t)di) & (FLG_LEN - 1u)] * (1.0f - fr) +
                flg_buf[1][(w - (uint32_t)di - 1u) & (FLG_LEN - 1u)] * fr;
            {
                float v = r[i] + fb * y;
                flg_buf[1][w & (FLG_LEN - 1u)] = v > -0.5f && v < 0.5f ? 0.0f : v;
            }
            r[i] += mix * y;
            w++;
        }
        fxs.flg_w = w;
        fxs.flg_ph = p1;
    }

    /* ---- delay */
    {
        int32_t mixp = fxp.dly_mix, len = fxs.dly_len_ext ? fxs.dly_len_ext : fxp.dly_len;
        const int pp = fxp.dly_mode == 1;       /* ping-pong: in on the left, each repeat crosses to the other side */
        uint32_t w = fxs.dly_w;
        len = len < 16 ? 16 : len > (int32_t)DLY_LEN - 4 ? (int32_t)DLY_LEN - 4 : len;
        if (fxs.dly_cur < 1.0f)
            fxs.dly_cur = (float)len;
        if (fxs.dly_quiet) {                     /* after a stop: silent, the lines filled with zeros as it goes */
            for (i = 0; i < n; i++) {
                dly_buf[0][w] = dly_buf[1][w] = 0;
                if (++w >= DLY_LEN)
                    w = 0;
            }
            fxs.dly_quiet = fxs.dly_quiet > n ? fxs.dly_quiet - n : 0u;
            fxs.dly_lp[0] = fxs.dly_lp[1] = 0.0f;
            fxs.dly_cur = (float)len;            /* (no glide in from an old time: it starts where it is set) */
            fxs.dly_on = 0;
        } else if (mixp > 0) {
            const float fb = (float)fxp.dly_fb * (0.01f / 128.0f), mix = (float)mixp * (0.01f / 128.0f);
            const float t = (float)fxp.dly_tone * (0.01f / 128.0f), c = 0.05f + 0.95f * t * t;
            float cur = fxs.dly_cur, lpl = fxs.dly_lp[0], lpr = fxs.dly_lp[1];
            const float target = (float)len;
            if (!fxs.dly_on)                     /* just turned on: no glide from the time it had while off */
                cur = target;
            for (i = 0; i < n; i++) {
                float fr, yl, yr, v;
                int32_t di;
                uint32_t a, b;
                cur += (target - cur) * 0.0002f;         /* the glide: ~110 ms time constant */
                di = (int32_t)cur;
                fr = cur - (float)di;
                a = w >= (uint32_t)di ? w - (uint32_t)di : w + DLY_LEN - (uint32_t)di;
                b = a ? a - 1u : DLY_LEN - 1u;
                yl = (float)dly_buf[0][a] * (1.0f - fr) + (float)dly_buf[0][b] * fr;
                yr = (float)dly_buf[1][a] * (1.0f - fr) + (float)dly_buf[1][b] * fr;
                lpl += (yl - lpl) * c;
                lpr += (yr - lpr) * c;
                v = pp ? 0.5f * (l[i] + r[i]) + fb * lpr : l[i] + fb * lpl;
                dly_buf[0][w] = (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v);
                v = pp ? fb * lpl : r[i] + fb * lpr;
                dly_buf[1][w] = (int16_t)(v > 32767.0f ? 32767.0f : v < -32767.0f ? -32767.0f : v);
                l[i] += mix * lpl;
                r[i] += mix * lpr;
                if (++w >= DLY_LEN)
                    w = 0;
            }
            fxs.dly_cur = cur;
            fxs.dly_on = 1;
            fxs.dly_lp[0] = lpl > -0.5f && lpl < 0.5f ? 0.0f : lpl;   /* (a tail far below one LSB: exact 0) */
            fxs.dly_lp[1] = lpr > -0.5f && lpr < 0.5f ? 0.0f : lpr;
        } else {                                 /* off: keep the lines current (no feedback), so turning it on
                                                  * starts clean */
            for (i = 0; i < n; i++) {
                float vl = l[i], vr = r[i];
                dly_buf[0][w] = (int16_t)(vl > 32767.0f ? 32767.0f : vl < -32767.0f ? -32767.0f : vl);
                dly_buf[1][w] = (int16_t)(vr > 32767.0f ? 32767.0f : vr < -32767.0f ? -32767.0f : vr);
                if (++w >= DLY_LEN)
                    w = 0;
            }
            fxs.dly_cur = (float)len;
            fxs.dly_lp[0] = fxs.dly_lp[1] = 0.0f;
            fxs.dly_on = 0;
        }
        fxs.dly_w = w;
    }

    /* ---- reverb */
    if (fxp.rv_mix > 0)
        fxs.rv_tail = 12u * FS;                  /* (silence from below one LSB takes ~1.6 x its longest decay) */
    if (fxs.rv_quiet || fxs.rv_tail) {
        const float s = (float)fxp.rv_size * (0.01f / 128.0f), g = 0.5f * (0.45f + 0.48f * s);   /* (Hadamard / 2) */
        const float c = 0.75f - 0.35f * s, mix = fxs.rv_quiet ? 0.0f : (float)fxp.rv_mix * (0.0035f / 128.0f);
        const float in_g = fxs.rv_quiet || !fxp.rv_mix ? 0.0f : 0.25f;
        uint32_t base[RV_N] = {0, 2939u, 2939u + 3203u, 2939u + 3203u + 3557u}, k;
        for (i = 0; i < n; i++) {
            float x = in_g * (l[i] + r[i]), d[RV_N], h[RV_N], v;
            uint32_t a;
            /* two allpasses: y = buf - g x; buf = x + g y */
            a = fxs.rv_appos[0];
            v = rv_ap[a] - 0.7f * x;
            rv_ap[a] = x + 0.7f * v;
            fxs.rv_appos[0] = (uint16_t)(a + 1u >= RV_AP[0] ? 0u : a + 1u);
            x = v;
            a = fxs.rv_appos[1];
            v = rv_ap[347u + a] - 0.7f * x;
            rv_ap[347u + a] = x + 0.7f * v;
            fxs.rv_appos[1] = (uint16_t)(a + 1u >= RV_AP[1] ? 0u : a + 1u);
            x = v;
            for (k = 0; k < RV_N; k++)
                d[k] = rv_buf[base[k] + fxs.rv_pos[k]];
            h[0] = d[0] + d[1] + d[2] + d[3];
            h[1] = d[0] - d[1] + d[2] - d[3];
            h[2] = d[0] + d[1] - d[2] - d[3];
            h[3] = d[0] - d[1] - d[2] + d[3];
            for (k = 0; k < RV_N; k++) {
                float lp = fxs.rv_lp[k] + (h[k] * g - fxs.rv_lp[k]) * c, w = (k & 1u ? -x : x) + lp;
                fxs.rv_lp[k] = lp > -0.5f && lp < 0.5f ? 0.0f : lp;
                rv_buf[base[k] + fxs.rv_pos[k]] = w > -0.5f && w < 0.5f ? 0.0f : w;   /* tails end at exactly 0 */
                fxs.rv_pos[k] = (uint16_t)(fxs.rv_pos[k] + 1u >= RV_LEN[k] ? 0u : fxs.rv_pos[k] + 1u);
            }
            l[i] += mix * (d[0] + d[2]);
            r[i] += mix * (d[1] + d[3]);
        }
        if (fxs.rv_quiet)
            fxs.rv_quiet = fxs.rv_quiet > n ? fxs.rv_quiet - n : 0u;
        if (!fxp.rv_mix && fxs.rv_tail)
            fxs.rv_tail = fxs.rv_tail > n ? fxs.rv_tail - n : 0u;
    }

    for (i = 0; i < n; i++) {                    /* back to integers (truncation: towards 0) */
        float a = l[i], b = r[i];
        outl[i] = (int32_t)(a > 1e6f ? 1e6f : a < -1e6f ? -1e6f : a);
        outr[i] = (int32_t)(b > 1e6f ? 1e6f : b < -1e6f ? -1e6f : b);
    }
}

static void fx_reset(void)                       /* the ISR, on app_audio_stop: every tail silenced at once */
{
    memset(ens_buf, 0, sizeof ens_buf);          /* (10 KB: the short lines are cleared outright) */
    memset(flg_buf, 0, sizeof flg_buf);
    fxs.dly_quiet = DLY_LEN;
    fxs.rv_quiet = RV_LONGEST + 512u;            /* (every line and diffuser overwritten with zeros, then off) */
    fxs.rv_tail = 0;
    fxs.rv_lp[0] = fxs.rv_lp[1] = fxs.rv_lp[2] = fxs.rv_lp[3] = 0.0f;
}
