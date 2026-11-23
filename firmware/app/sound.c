/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 R. Luke DuBois */
/* The sound (audio ISR): a divide-down string machine. 12 phase accumulators, one per pitch class, run at the
 * lowest octave (MIDI 0..11, 8.2 .. 15.4 Hz); note n of octave k reads its pitch class's phase shifted left by k,
 * which is that phase at 2^k times the frequency, so every C (every C#, ...) is phase-locked to the others as in an
 * organ's divider chain, and any number of keys can sound. Each sounding note mixes three registers: 16' (k - 1),
 * 8' (k), 4' (k + 1), folded back an octave past the ends of the range. A per-note key gate of ~3 ms keeps keys from
 * clicking; the shared envelope (ADSR, RETRIG or LEGATO) does the shaping, and the notes that were down when the last
 * key went up keep sounding through its release (paraphonic: one envelope for all).
 * The registers are band-limited saws (PolyBLEP), blended by SHAPE towards a square and a narrowing pulse (two saws
 * apart); the sum goes through one resonant low-pass (filt_block: cutoff, resonance, envelope amount, key tracking)
 * and then the envelope, then the effects (fx.c: ensemble, flanger, delay; stereo) and VOLUME x MASTER. No LFO yet
 * (milestone 4, docs/SPEC.md). Main loop -> ISR: snd
 * (derived numbers, 32-bit stores) and the volatile requests; ISR -> main loop: the published gated set and the
 * sounding count. */

#define NOTE_KEY0 53u            /* note key 0 is F3 (hal/fm1_input.h fm1_in.notes) */
#define MAX_SOUNDING 48u         /* notes rendered per block at most: held ones first (lowest first), then releasing.
                                  * Measured on the unit (milestone 2): all 27 keys x 3 registers stay under 20 %
                                  * CPU, so 48 is ~36 %, room left for the filter and the effects; snd_shed guards */
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
    int32_t rr[12];              /* the BLEP's reciprocal of each pitch class's step: 2^30 / (inc >> 12) */
    int32_t w1, w2;              /* SHAPE: osc = (w1 x saw(p) - w2 x saw(p + pw)) / 256 (w2 0: a plain saw) */
    uint32_t pw;                 /* SHAPE: the second saw's offset, the pulse width (2^31: a square) */
    int32_t f_cut, f_res, f_env, f_kt;   /* the filter: CUTOFF 0..127, RESONANCE %, ENV AMOUNT -100..100 %, KEY TRACK % */
    uint32_t lfo_inc;            /* the LFO's phase step per block */
    int32_t vib_c, pwm;          /* VIBRATO as cents (0..50: 100 % is +-50), PWM % (100 %: the width +-40 %) */
    int32_t lfo_fade;            /* the LFO's fade-in per block after an attack, Q16 (65536: at once) */
} snd;

/* A band-limited saw (PolyBLEP): the naive ramp, -32768 at p = 0 rising to 32767, with its jump at the wrap smoothed
 * by a two-sample polynomial, (1 - x)^2 with x the distance to the wrap in steps, added just after it and taken off
 * just before it. That takes the energy that would fold back below Nyquist down by ~25..40 dB over the naive ramp.
 * x = p / dt in Q15 without a divide: (p >> 12) x rr >> 15, rr = 2^30 / (dt >> 12) (snd_update; >> k per octave).
 * Integer only, 32-bit; the correction runs for ~2 samples a period, the rest is a shift and an XOR. */
/* the two samples around the wrap (out of line: the hot loop stays small, this runs ~2 samples a period) */
static __attribute__((noinline)) int32_t blep_fix(int32_t s, uint32_t p, uint32_t dt, int32_t rr)
{
    int32_t x, d;
    if (p < dt) {                                /* just after the wrap: s ~ -32768, lifted towards 0 */
        x = (int32_t)(p >> 12) * rr >> 15;
        d = 32768 - (x > 32768 ? 32768 : x);
        return s + (d * d >> 15);
    }
    x = (int32_t)((0u - p) >> 12) * rr >> 15;    /* just before it: s ~ +32767, brought down towards 0 */
    d = 32768 - (x > 32768 ? 32768 : x);
    return s - (d * d >> 15);
}

static inline int32_t blsaw(uint32_t p, uint32_t dt, int32_t rr)
{
    int32_t s = (int32_t)(p ^ 0x80000000u) >> 16;
    if (p + dt < dt << 1)                        /* within a step of the wrap, either side: one compare (rare) */
        return blep_fix(s, p, dt, rr);
    return s;
}

/* one register's oscillator: the saw, or SHAPE's blend towards the square / pulse, which is the saw minus the same
 * saw pw later (two band-limited saws: a band-limited pulse, and no DC at any width) */
static inline int32_t osc(uint32_t p, uint32_t dt, int32_t rr, int32_t w1, int32_t w2, uint32_t pw)
{
    int32_t s = blsaw(p, dt, rr);
    if (!w2)
        return s;
    return (w1 * s - w2 * blsaw(p + pw, dt, rr)) >> 8;
}

/* ------------------------------------------------------------ the shared filter --- */
/* A resonant 12 dB/oct low-pass on the summed notes: a zero-delay-feedback state-variable filter (the trapezoidal
 * integrator form), stable at every cutoff up to 16 kHz and every resonance, where the older Chamberlin form blows up
 * above ~FS / 6. Its coefficient g = tan(pi fc / FS) comes from this table, one entry per CUTOFF step (fc = 20 Hz x
 * 800^(i / 127), as data.c K_CUT shows it), interpolated between steps for the envelope and key tracking; there is
 * no libm on the target. (python3: math.tan(math.pi * 20 * 800 ** (i / 127) / 44100), i = 0..127.) */
static const float FILT_G[128] = {
    0.00142476f, 0.00150176f, 0.001582922f, 0.001668471f, 0.001758643f, 0.001853688f,
    0.00195387f, 0.002059466f, 0.00217077f, 0.002288089f, 0.002411748f, 0.00254209f,
    0.002679477f, 0.002824289f, 0.002976927f, 0.003137815f, 0.003307398f, 0.003486147f,
    0.003674555f, 0.003873147f, 0.004082472f, 0.00430311f, 0.004535673f, 0.004780805f,
    0.005039186f, 0.005311532f, 0.005598597f, 0.005901178f, 0.006220112f, 0.006556285f,
    0.006910627f, 0.007284122f, 0.007677805f, 0.008092766f, 0.008530157f, 0.00899119f,
    0.009477143f, 0.009989364f, 0.01052927f, 0.01109837f, 0.01169823f, 0.01233052f,
    0.01299699f, 0.01369949f, 0.01443997f, 0.01522049f, 0.01604322f, 0.01691043f,
    0.01782453f, 0.01878807f, 0.01980372f, 0.02087431f, 0.02200281f, 0.02319236f,
    0.02444626f, 0.02576802f, 0.02716131f, 0.02863001f, 0.03017822f, 0.03181027f,
    0.03353069f, 0.03534432f, 0.03725621f, 0.03927172f, 0.0413965f, 0.04363653f,
    0.04599808f, 0.04848782f, 0.05111277f, 0.05388035f, 0.05679838f, 0.05987517f,
    0.06311947f, 0.06654053f, 0.07014817f, 0.07395275f, 0.07796526f, 0.08219733f,
    0.08666128f, 0.09137021f, 0.09633799f, 0.1015794f, 0.10711f, 0.1129465f,
    0.1191068f, 0.1256096f, 0.1324752f, 0.1397252f, 0.1473827f, 0.1554724f,
    0.1640209f, 0.1730567f, 0.1826107f, 0.1927161f, 0.2034089f, 0.2147282f,
    0.2267166f, 0.2394203f, 0.2528903f, 0.2671824f, 0.282358f, 0.2984854f,
    0.3156403f, 0.3339073f, 0.3533811f, 0.3741689f, 0.3963919f, 0.4201883f,
    0.4457168f, 0.4731606f, 0.5027326f, 0.5346823f, 0.5693042f, 0.6069493f,
    0.6480393f, 0.6930867f, 0.7427205f, 0.797723f, 0.8590801f, 0.9280531f,
    1.006284f, 1.095953f, 1.200017f, 1.322601f, 1.469632f, 1.649952f,
    1.877377f, 2.174768f,
};
#define CUT_PER_SEMI 1.0975f     /* CUTOFF steps per semitone: 127 steps over log2(800) = 9.64 octaves */

static struct {
    float ic1, ic2;              /* the filter's two integrator states */
    uint32_t top;                /* the highest note sounding (key tracking), kept through the release */
    volatile int32_t idx_pub;    /* ISR -> main loop, tests: the cutoff index this block, Q8 (0 .. 127 x 256) */
} flt;

/* one block through the filter, in place, on the FPU (FM1_FPU: the target's float unit; a plain float elsewhere).
 * The cutoff index: CUTOFF + ENV AMOUNT x the envelope (+-100 % is +-127 steps, the whole range) + KEY TRACK x the
 * highest note's distance from C4 (100 %: the cutoff follows the keys); the coefficients once per block. */
static FM1_FPU __attribute__((noinline)) void filt_block(int32_t *mix, uint32_t n, int32_t env)
{
    float idx = (float)snd.f_cut + (float)snd.f_env * 1.27f * ((float)env * (1.0f / ENV_ONE)) +
                (float)snd.f_kt * 0.01f * CUT_PER_SEMI * (float)((int32_t)flt.top - 60);
    float g, k, a1, a2, a3, ic1 = flt.ic1, ic2 = flt.ic2, fr;
    int32_t i;
    uint32_t j;
    idx = idx < 0.0f ? 0.0f : idx > 127.0f ? 127.0f : idx;
    i = (int32_t)idx;
    fr = idx - (float)i;
    g = i < 127 ? FILT_G[i] + (FILT_G[i + 1] - FILT_G[i]) * fr : FILT_G[127];
    k = 2.0f - 1.96f * 0.01f * (float)snd.f_res;          /* damping: 2 (none) .. 0.04 (Q 25, near ringing) */
    a1 = 1.0f / (1.0f + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
    flt.idx_pub = (int32_t)(idx * 256.0f);
    for (j = 0; j < n; j++) {
        float v3 = (float)mix[j] - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        mix[j] = (int32_t)v2;
    }
    /* a tail far below one LSB goes to 0 (no denormals, exact silence); anything not finite starts over */
    if (!(ic1 > -1e9f && ic1 < 1e9f) || !(ic2 > -1e9f && ic2 < 1e9f))
        ic1 = ic2 = 0.0f;
    if (ic1 > -1e-6f && ic1 < 1e-6f && ic2 > -1e-6f && ic2 < 1e-6f)
        ic1 = ic2 = 0.0f;
    flt.ic1 = ic1;
    flt.ic2 = ic2;
}

enum { ENV_IDLE, ENV_ATK, ENV_DEC, ENV_REL };

static struct {
    uint32_t phase[12];          /* the pitch classes' accumulators (lowest octave) */
    uint16_t gate[128];          /* each note's key gate, 0 .. GATE_ONE */
    uint32_t open[4];            /* notes whose gate is not shut (bit n of word n >> 5) */
    uint32_t voiced[4];          /* notes whose gate should be open */
    uint32_t midi[4];            /* held over MIDI */
    uint32_t latch[4];           /* HOLD: notes kept after their keys went up */
    uint32_t sus[4];             /* the sustain pedal's notes */
    uint32_t klatch[4];          /* HOLD's notes from the panel keys (what MIDI out keeps on) */
    volatile uint32_t raw_pub[4];    /* ISR -> main loop: keys and MIDI notes down, before the chord (REC) */
    uint32_t prev_gated[4];      /* last block's gated set (new notes trigger the envelope) */
    uint32_t prev_input;         /* last block's: any key or MIDI note down (HOLD replaces the latched set) */
    int32_t env;                 /* the shared envelope, Q24 */
    uint8_t stage, pedal;
    int32_t mix[CTL];
    int32_t fxl[CTL], fxr[CTL];   /* the effects' stereo out */
    volatile uint8_t hold;       /* main loop -> ISR: HOLD on; off releases the latched notes */
    volatile uint8_t stop_req;   /* main loop -> ISR: forget every note, silence (app_audio_stop) */
    volatile uint8_t midi_off_req;   /* main loop -> ISR: release every MIDI note (the channel changed) */
    volatile uint32_t gated_pub[4];  /* ISR -> main loop: the gated notes (LEDs, the screen's dots) */
    volatile uint32_t sounding_pub;  /* ISR -> main loop: notes rendered last block */
    volatile uint32_t cap;           /* main loop -> ISR: notes rendered at most (CAP_MIN .. MAX_SOUNDING; snd_shed) */
    uint32_t cap_ms;                 /* (main loop: when the cap last rose) */
    /* the LFO and the performance controls (ISR state) */
    uint32_t lfo_ph;
    int32_t lfo_amt;                 /* the fade-in after an attack, Q16 (0 .. 65536) */
    int32_t bend;                    /* pitch bend in cents, -200 .. +200 */
    int32_t modw;                    /* the mod wheel (CC1), 0..127: more vibrato */
    volatile int32_t cents_pub;      /* ISR -> tests: the pitch offset this block, cents */
    volatile uint32_t pw_pub;        /* ISR -> tests: the pulse width this block */
    /* the CC map: the ISR writes a value, then bumps its counter; the main loop applies what changed (cc_take) */
    volatile uint8_t cc_val[16];
    volatile uint8_t cc_seq[16];
} vox;

/* the pitch modulation: 65536 x (2^(c / 1200) - 1) for c = -300 .. +300 cents (vibrato + mod wheel + bend), so a
 * step is inc + (inc >> 4) x CENT_DQ >> 12, all in 32 bits (|CENT_DQ| <= 12 400, inc >> 4 <= 94 000) */
static const int16_t CENT_DQ[601] = {
    -10427, -10395, -10363, -10331, -10300, -10268, -10236, -10204, -10172, -10140, -10108, -10076, -10044, -10012, -9980,
    -9947, -9915, -9883, -9851, -9819, -9787, -9754, -9722, -9690, -9658, -9625, -9593, -9561, -9528, -9496,
    -9464, -9431, -9399, -9366, -9334, -9302, -9269, -9237, -9204, -9171, -9139, -9106, -9074, -9041, -9008,
    -8976, -8943, -8910, -8878, -8845, -8812, -8779, -8747, -8714, -8681, -8648, -8615, -8582, -8549, -8517,
    -8484, -8451, -8418, -8385, -8352, -8319, -8286, -8252, -8219, -8186, -8153, -8120, -8087, -8054, -8020,
    -7987, -7954, -7921, -7887, -7854, -7821, -7787, -7754, -7721, -7687, -7654, -7620, -7587, -7553, -7520,
    -7486, -7453, -7419, -7386, -7352, -7318, -7285, -7251, -7217, -7184, -7150, -7116, -7083, -7049, -7015,
    -6981, -6947, -6914, -6880, -6846, -6812, -6778, -6744, -6710, -6676, -6642, -6608, -6574, -6540, -6506,
    -6472, -6438, -6403, -6369, -6335, -6301, -6267, -6232, -6198, -6164, -6129, -6095, -6061, -6026, -5992,
    -5958, -5923, -5889, -5854, -5820, -5785, -5751, -5716, -5682, -5647, -5613, -5578, -5543, -5509, -5474,
    -5439, -5404, -5370, -5335, -5300, -5265, -5231, -5196, -5161, -5126, -5091, -5056, -5021, -4986, -4951,
    -4916, -4881, -4846, -4811, -4776, -4741, -4706, -4671, -4635, -4600, -4565, -4530, -4495, -4459, -4424,
    -4389, -4353, -4318, -4283, -4247, -4212, -4176, -4141, -4106, -4070, -4035, -3999, -3963, -3928, -3892,
    -3857, -3821, -3785, -3750, -3714, -3678, -3643, -3607, -3571, -3535, -3499, -3463, -3428, -3392, -3356,
    -3320, -3284, -3248, -3212, -3176, -3140, -3104, -3068, -3032, -2996, -2960, -2923, -2887, -2851, -2815,
    -2779, -2742, -2706, -2670, -2633, -2597, -2561, -2524, -2488, -2451, -2415, -2378, -2342, -2305, -2269,
    -2232, -2196, -2159, -2123, -2086, -2049, -2013, -1976, -1939, -1902, -1866, -1829, -1792, -1755, -1718,
    -1682, -1645, -1608, -1571, -1534, -1497, -1460, -1423, -1386, -1349, -1312, -1275, -1237, -1200, -1163,
    -1126, -1089, -1051, -1014, -977, -940, -902, -865, -828, -790, -753, -715, -678, -640, -603,
    -565, -528, -490, -453, -415, -377, -340, -302, -264, -227, -189, -151, -113, -76, -38,
    0, 38, 76, 114, 152, 190, 228, 266, 304, 342, 380, 418, 456, 494, 532,
    570, 608, 647, 685, 723, 761, 800, 838, 876, 915, 953, 992, 1030, 1069, 1107,
    1146, 1184, 1223, 1261, 1300, 1338, 1377, 1416, 1454, 1493, 1532, 1571, 1609, 1648, 1687,
    1726, 1765, 1804, 1842, 1881, 1920, 1959, 1998, 2037, 2076, 2115, 2155, 2194, 2233, 2272,
    2311, 2350, 2390, 2429, 2468, 2507, 2547, 2586, 2625, 2665, 2704, 2744, 2783, 2823, 2862,
    2902, 2941, 2981, 3020, 3060, 3099, 3139, 3179, 3219, 3258, 3298, 3338, 3378, 3417, 3457,
    3497, 3537, 3577, 3617, 3657, 3697, 3737, 3777, 3817, 3857, 3897, 3937, 3977, 4017, 4058,
    4098, 4138, 4178, 4219, 4259, 4299, 4340, 4380, 4420, 4461, 4501, 4542, 4582, 4623, 4663,
    4704, 4744, 4785, 4826, 4866, 4907, 4948, 4988, 5029, 5070, 5111, 5151, 5192, 5233, 5274,
    5315, 5356, 5397, 5438, 5479, 5520, 5561, 5602, 5643, 5684, 5725, 5767, 5808, 5849, 5890,
    5932, 5973, 6014, 6055, 6097, 6138, 6180, 6221, 6263, 6304, 6346, 6387, 6429, 6470, 6512,
    6553, 6595, 6637, 6678, 6720, 6762, 6804, 6845, 6887, 6929, 6971, 7013, 7055, 7097, 7139,
    7181, 7223, 7265, 7307, 7349, 7391, 7433, 7475, 7518, 7560, 7602, 7644, 7687, 7729, 7771,
    7814, 7856, 7898, 7941, 7983, 8026, 8068, 8111, 8153, 8196, 8238, 8281, 8324, 8366, 8409,
    8452, 8495, 8537, 8580, 8623, 8666, 8709, 8752, 8794, 8837, 8880, 8923, 8966, 9009, 9053,
    9096, 9139, 9182, 9225, 9268, 9312, 9355, 9398, 9441, 9485, 9528, 9571, 9615, 9658, 9702,
    9745, 9789, 9832, 9876, 9919, 9963, 10006, 10050, 10094, 10137, 10181, 10225, 10269, 10313, 10356,
    10400, 10444, 10488, 10532, 10576, 10620, 10664, 10708, 10752, 10796, 10840, 10884, 10928, 10973, 11017,
    11061, 11105, 11150, 11194, 11238, 11283, 11327, 11371, 11416, 11460, 11505, 11549, 11594, 11638, 11683,
    11728, 11772, 11817, 11862, 11906, 11951, 11996, 12041, 12085, 12130, 12175, 12220, 12265, 12310, 12355,
    12400,
};
#define CENTS_MAX 300
#define PW_MIN 214748365u        /* the pulse width's limits under PWM: 5 % and 95 % of the period */
#define PW_MAX 4080218931u

/* the CC map (docs/SPEC.md "MIDI"): CC number -> the sound parameter it sets (0..127 across its range) */
static const uint8_t CC_NUM[16] = {12, 13, 14, 15, 74, 71, 20, 21, 73, 75, 70, 72, 76, 77, 78, 93};
static const uint8_t CC_PAR[16] = {P_R16, P_R8, P_R4, P_SHAPE, P_CUT, P_RES, P_FENV, P_KTRK,
                                   P_ATK, P_DEC, P_SUS, P_REL, P_LRATE, P_VIB, P_PWM, P_ENSD};

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
    if (((pkt >> 8) & 0xFFu) >= 0xF8u) {          /* clock, start, continue, stop: any channel (seq.c) */
        seq_rt((pkt >> 8) & 0xFFu);
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
    } else if (st == 0xE0u)                      /* pitch bend: 14 bits, centre 8192, +-2 semitones */
        vox.bend = ((int32_t)(d1 | d2 << 7) - 8192) * 200 >> 13;
    else if (st == 0xB0u && d1 == 1u)            /* the mod wheel: vibrato on top of VIBRATO */
        vox.modw = (int32_t)d2;
    else if (st == 0xB0u && d1 == 121u) {        /* reset all controllers */
        vox.bend = 0;
        vox.modw = 0;
        vox.pedal = 0;
        bits_clear(vox.sus);
    } else if (st == 0xB0u) {                    /* the CC map: handed to the main loop (cc_take) */
        uint32_t k;
        for (k = 0; k < 16u; k++)
            if (CC_NUM[k] == d1) {
                vox.cc_val[k] = (uint8_t)d2;
                RING_PUBLISH();                  /* (the value before the counter) */
                vox.cc_seq[k]++;
                break;
            }
    }
}

/* the envelope for one block: from where it is to where it will be (the samples interpolate between the two) */
static void env_block(uint32_t any_gated, uint32_t new_note)
{
    int32_t e = vox.env;
    if (new_note && (!snd.legato || vox.stage == ENV_IDLE || vox.stage == ENV_REL)) {
        if (vox.stage == ENV_IDLE || vox.stage == ENV_REL)
            vox.lfo_amt = 0;                     /* DELAY: the LFO fades in again from a fresh start */
        vox.stage = ENV_ATK;                     /* from the current level: no jump */
    }
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

/* the LFO, once per block: a triangle (RATE), faded in after an attack (DELAY); vibrato (VIBRATO + the mod wheel)
 * and pitch bend move all 12 pitch-class steps together (one master pitch, as on the old machines: the dividers
 * stay locked); PWM sweeps the pulse width (heard when SHAPE is past SAW). Integer, 32-bit. */
static void lfo_block(uint32_t *incm, int32_t *rrm, uint32_t *pw)
{
    int32_t tri, c, dq, k, i;
    uint32_t ph = vox.lfo_ph;
    tri = (int32_t)(ph >> 15) - 65536;                       /* -65536 .. 65535 */
    tri = (tri < 0 ? -tri : tri) - 32768;                    /* the triangle, -32768 .. 32768 */
    tri = tri > 32767 ? 32767 : tri;
    vox.lfo_ph = ph + snd.lfo_inc;
    k = vox.lfo_amt + snd.lfo_fade;
    vox.lfo_amt = k > 65536 ? 65536 : k;
    tri = tri * (vox.lfo_amt >> 1) >> 15;                    /* (2^15 x 2^15: 32 bits, no 64-bit helper) */
    /* cents: VIBRATO 100 % = +-50, the mod wheel adds up to +-50 more, the bend +-200 */
    c = (tri * (snd.vib_c + (vox.modw * 50 >> 7)) >> 15) + vox.bend;
    c = c < -CENTS_MAX ? -CENTS_MAX : c > CENTS_MAX ? CENTS_MAX : c;
    vox.cents_pub = c;
    dq = CENT_DQ[c + CENTS_MAX];
    for (i = 0; i < 12; i++) {
        incm[i] = snd.inc[i] + (uint32_t)((int32_t)(snd.inc[i] >> 4) * dq >> 12);
        rrm[i] = snd.rr[i] - ((snd.rr[i] >> 8) * dq >> 8);    /* 1 / (1 + d) ~ 1 - d: the BLEP width follows */
    }
    if (snd.pwm) {                               /* +-40 % of the period at 100 %, kept inside 5 .. 95 % */
        int32_t d = tri * snd.pwm * 524;         /* (32768 x 100 x 524 < 2^31) */
        uint32_t w = *pw;
        if (d >= 0)
            w = (uint32_t)d > PW_MAX - w ? PW_MAX : w + (uint32_t)d;
        else
            w = (uint32_t)-d > w - PW_MIN ? PW_MIN : w - (uint32_t)-d;
        *pw = w;
    }
    vox.pw_pub = *pw;
}

static void app_audio(int32_t *out, uint32_t n)
{
    uint32_t keys = fm1_in.notes, pkt, i, w, any = 0, fresh = 0, input = 0, rendered = 0, cap, pass;
    uint32_t gated[4], target[4], active[4], kb = snd.key_base, kset[4], mset[4], sound[4], want[4];
    int32_t e0, e1, de, a16 = snd.amp16, a8 = snd.amp8, a4 = snd.amp4, gain = snd.gain;
    int32_t w1 = snd.w1, w2 = snd.w2;
    uint32_t pw = snd.pw;
    uint32_t incm[12];
    int32_t rrm[12];

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
        bits_clear(vox.klatch);
        arp.note = -1;
        arp.idx = 0;
        vox.bend = 0;
        vox.modw = 0;
        vox.env = 0;
        vox.stage = ENV_IDLE;
        memset(vox.gate, 0, sizeof vox.gate);
        flt.ic1 = flt.ic2 = 0.0f;
        fx_reset();
    }

    /* what is down now: the keys at the octave shift, MIDI (each as the stored chord on it, seq.c); then HOLD and the
     * pedal keep what was down; then the arp, if on, picks what sounds of it (seq.c) */
    for (w = 0; w < 4u; w++)
        kset[w] = 0;
    for (i = 0; keys >> i; i++)
        if ((keys >> i) & 1u && kb + i < 128u)
            bit_set(kset, kb + i, 1);
    for (w = 0; w < 4u; w++) {
        mset[w] = vox.midi[w];
        vox.raw_pub[w] = kset[w] | mset[w];      /* (REC records these: before the chord) */
    }
    chord_expand(kset);
    chord_expand(mset);
    for (w = 0; w < 4u; w++) {
        gated[w] = kset[w] | mset[w];
        input |= gated[w];
    }
    if (vox.hold) {                              /* a new chord after all keys were up replaces the latched one */
        if (input && !vox.prev_input) {
            bits_clear(vox.latch);
            bits_clear(vox.klatch);
        }
        for (w = 0; w < 4u; w++) {
            vox.latch[w] |= gated[w];
            vox.klatch[w] |= kset[w];
        }
    } else {
        bits_clear(vox.latch);
        bits_clear(vox.klatch);
    }
    vox.prev_input = input;
    if (vox.pedal)
        for (w = 0; w < 4u; w++)
            vox.sus[w] |= gated[w];
    for (w = 0; w < 4u; w++)
        gated[w] |= vox.latch[w] | vox.sus[w];
    if (seqp.on) {                               /* the arp: one note at a time of what is held */
        arp_block(gated, sound, n);
        for (w = 0; w < 4u; w++)
            want[w] = sound[w];
    } else {
        arp.play_req = 0;                        /* (a request from before the arp went off: dropped, not kept) */
        arp.note_pub = -1;
        for (w = 0; w < 4u; w++) {
            sound[w] = gated[w];
            want[w] = kset[w] | vox.klatch[w];   /* MIDI out: the panel's notes (no echo of MIDI in) */
        }
    }
    seq_out(want);
    for (w = 0; w < 4u; w++) {
        vox.gated_pub[w] = gated[w] | sound[w];
        gated[w] = sound[w];
        any |= gated[w];
        fresh |= gated[w] & ~vox.prev_gated[w];
        vox.prev_gated[w] = gated[w];
    }

    e0 = vox.env;
    env_block(any, fresh);
    e1 = vox.env;
    lfo_block(incm, rrm, &pw);
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
            uint32_t b = (uint32_t)__builtin_ctz(bits), note = (w << 5) + b, k, k16, k4, ph, inc, pc;
            int32_t g0 = vox.gate[note], g1, gs, dg, rr;
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
            pc = note - k * 12u;
            ph = vox.phase[pc];
            inc = incm[pc];
            rr = rrm[pc];
            gs = g0 << 5;                        /* Q15, interpolated across the block */
            dg = g1 - g0;
            {   /* each register's step and BLEP reciprocal: octave k is the lowest octave's << k (and rr >> k) */
                uint32_t d16 = inc << k16, d8 = inc << k, d4 = inc << k4;
                int32_t r16 = rr >> k16, r8 = rr >> k, r4 = rr >> k4;
                if (!w2)                         /* SHAPE at SAW: the plain loop (a register at 0 % adds 0) */
                    for (i = 0; i < n; i++) {
                        int32_t v = blsaw(ph << k16, d16, r16) * a16 + blsaw(ph << k, d8, r8) * a8 +
                                    blsaw(ph << k4, d4, r4) * a4;
                        vox.mix[i] += (v >> 15) * gs >> 15;
                        gs += dg;
                        ph += inc;
                    }
                else
                    for (i = 0; i < n; i++) {
                        int32_t v = 0;
                        if (a16)
                            v += osc(ph << k16, d16, r16, w1, w2, pw) * a16;
                        if (a8)
                            v += osc(ph << k, d8, r8, w1, w2, pw) * a8;
                        if (a4)
                            v += osc(ph << k4, d4, r4, w1, w2, pw) * a4;
                        vox.mix[i] += (v >> 15) * gs >> 15;
                        gs += dg;
                        ph += inc;
                    }
            }
        }
    }
    for (i = 0; i < 12u; i++)
        vox.phase[i] += incm[i] * n;
    vox.sounding_pub = rendered;

    for (w = 4u; w-- > 0u;)                      /* key tracking follows the highest note held (kept in the release) */
        if (target[w]) {
            flt.top = (w << 5) + 31u - (uint32_t)__builtin_clz(target[w]);
            break;
        }
    filt_block(vox.mix, n, e1);

    for (i = 0; i < n; i++) {                    /* the envelope (Q15), into the effects */
        int32_t x = vox.mix[i];
        x = x > 65535 ? 65535 : x < -65535 ? -65535 : x;
        vox.mix[i] = x * ((e0 + de * (int32_t)i) >> 9) >> 15;
    }
    fx_block(vox.mix, vox.fxl, vox.fxr, n);      /* ensemble, flanger, delay: stereo */
    for (i = 0; i < 2u * n; i++) {
        int32_t x = (i & 1u ? vox.fxr : vox.fxl)[i >> 1], ax;
        x = x > 1000000 ? 1000000 : x < -1000000 ? -1000000 : x;
        x = x * gain >> 8;                       /* VOLUME x MASTER after the effects: their tails follow it */
        ax = x < 0 ? -x : x;
        if (ax > 20000)                          /* a soft knee, then the ceiling: full chords do not crack */
            ax = 20000 + ((ax - 20000) >> 2);
        ax = ax > 32767 ? 32767 : ax;
        out[i] = x < 0 ? -ax : ax;
    }
}

static void app_audio_stop(void) { vox.stop_req = 1; }

/* main loop: the engine's numbers from the parameters (on a change, and at boot) */
static uint8_t arp_on, chord_rec;    /* (ui.c: ARP on; REC recording a chord) */
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
    for (i = 0; i < 12; i++) {
        snd.inc[i] = (uint32_t)((uint64_t)INC0[i] * t >> 30);
        snd.rr[i] = (int32_t)((1u << 30) / (snd.inc[i] >> 12));   /* inc >= ~750 000: >> 12 is >= 183, no 0 */
    }
    {   /* SHAPE 0..99: saw (0) -> saw + square (33) -> square (66) -> a pulse narrowing to 10 % (99), continuous;
         * osc = A saw + B square, square = saw(p) - saw(p + pw): w1 = A + B, w2 = B */
        int32_t x = cur.p[P_SHAPE], a, bq;
        if (x < 33) {
            bq = 128 * x / 33;
            a = 256 - bq;
        } else if (x < 66) {
            a = 128 - 128 * (x - 33) / 33;
            bq = 256 - a;
        } else {
            a = 0;
            bq = 256;
        }
        snd.w1 = a + bq;                         /* (256 in every zone: the blend keeps its level) */
        snd.w2 = bq;
        snd.pw = x <= 66 ? 0x80000000u : 0x80000000u - (uint32_t)(x - 66) * 52060209u;   /* 0.4 x 2^32 / 33 a step */
    }
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
    /* the effects (fx.c): integers only; a rate in mHz is a phase step per block of mHz x 2^32 x CTL / 1000 / FS */
    fxp.ens_type = cur.p[P_ENS];
    fxp.ens_depth = cur.p[P_ENSD];
    fxp.ens_inc = pv_ms(K_HZ, cur.p[P_ENSR]) * 3117u;
    fxp.flg_amt = cur.p[P_FLGA];
    fxp.flg_inc = pv_ms(K_HZ, cur.p[P_FLGR]) * 3117u;
    fxp.dly_len = (int32_t)(pv_ms(K_DLY, cur.p[P_DTIME]) * (FS / 100u) / 10u);   /* ms x 44.1 */
    fxp.dly_fb = cur.p[P_DFB];
    fxp.dly_mix = cur.p[P_DMIX];
    fxp.dly_tone = cur.p[P_DTONE];
    snd.lfo_inc = pv_ms(K_HZ, cur.p[P_LRATE]) * 3117u;     /* mHz -> a phase step per block (fx.c's rule) */
    snd.vib_c = (cur.p[P_VIB] * 50 + 50) / 100;   /* (the divide here, not in the ISR) */
    snd.pwm = cur.p[P_PWM];
    ms = pv_ms(K_MS, cur.p[P_LDLY]);                      /* DELAY: 0 .. 5 s to full; 32 frames a block */
    snd.lfo_fade = ms ? (int32_t)(65536u * CTL / (ms * (FS / 1000u))) : 65536;
    if (snd.lfo_fade < 1)
        snd.lfo_fade = 1;
    {   /* the arp, chord memory, MIDI out (seq.c): steps a quarter for 1/4 1/8 1/8T 1/16 1/16T 1/32 */
        static const uint8_t DIV[6] = {1, 2, 3, 4, 6, 8};
        uint32_t d = DIV[set.g[G_ARPR] % 6], k;
        seqp.mode = (uint32_t)set.g[G_ARPM];
        seqp.tps = 24u / d;
        seqp.oct = (uint32_t)set.g[G_ARPO];
        seqp.step_q8 = (uint32_t)FS * 60u * 256u / (set.bpm * d);
        seqp.gfrac = (uint32_t)set.g[G_ARPG] * 256u / 100u;
        seqp.out_ch = set.g[G_MIDICH] ? (uint32_t)set.g[G_MIDICH] - 1u : 0u;
        for (k = 0; k < CHORD_MAX; k++)
            seqp.chord[k] = set.chord[0][k];
        seqp.chord_n = chord_rec ? 0u : set.chord_n[0];   /* (while REC records: the keys plain) */
        seqp.on = arp_on;
    }
    snd.f_cut = cur.p[P_CUT];
    snd.f_res = cur.p[P_RES];
    snd.f_env = cur.p[P_FENV];
    snd.f_kt = cur.p[P_KTRK];
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
