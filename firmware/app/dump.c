/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 R. Luke DuBois */
/* Presets over SysEx (main loop): a librarian or tools/solinaesque_presets.py asks for or sends a sound.
 *
 *   frame     F0 7D 53 4C <cmd> <args> F7 (7D: non-commercial; 53 4C "SL", Solinaesque; the screen reader's is "SR")
 *   01        get the current sound            -> 02 <sound>
 *   02 <sound>        it becomes the current sound (NEW: SAVE stores it)
 *   03 <slot>         get user slot 0..31      -> 04 <slot> <sound>
 *   04 <slot> <sound> store it in that user slot (written to flash)
 *   sound     02 (format), the name (12 bytes, ASCII, 0-padded), the parameter count (28), each parameter as two
 *             7-bit bytes (v + 128: high bit, low 7 bits), each parameter's fine part (28 bytes, 0..127: the 14-bit
 *             parameters' 1/128 steps), a checksum (the sum of every byte from the format on, & 7F). Format 01 (no
 *             fine parts, 71 bytes) is still taken.
 * A frame that is not well formed changes nothing (and says so). Replies wait for room in the USB queue (never
 * blocking), given up after a second. */

#define SL_H0 0x7Du
#define SL_H1 0x53u
#define SL_H2 0x4Cu
enum { SL_GET = 1, SL_SOUND = 2, SL_GETSLOT = 3, SL_SLOT = 4 };
#define SL_FORMAT 2u
#define SL_BODY1 (1u + NAME_LEN + 1u + 2u * NSP_STORED + 1u)   /* format 1: 71 bytes */
#define SL_BODY (SL_BODY1 + NSP_STORED)                         /* format 2: 99 bytes */

static struct {
    uint8_t pending;             /* a reply waits: SL_SOUND or SL_SLOT */
    uint8_t slot;
    uint32_t since;              /* when it was asked for (fm1_ms) */
    uint32_t replies, rejected;  /* (counts, for the tests) */
} sl;

static uint32_t sl_encode(const preset_t *p, uint8_t *b)   /* a sound's body; its length */
{
    uint32_t i, n = 0, sum = 0;
    b[n++] = SL_FORMAT;
    for (i = 0; i < NAME_LEN; i++)
        b[n++] = (uint8_t)p->name[i] & 0x7Fu;
    b[n++] = NSP_STORED;
    for (i = 0; i < NSP_STORED; i++) {
        uint32_t v = (uint32_t)((int32_t)p->p[i] + 128);
        b[n++] = (uint8_t)(v >> 7);
        b[n++] = (uint8_t)(v & 0x7Fu);
    }
    for (i = 0; i < NSP_STORED; i++)
        b[n++] = p->f[i] & 0x7Fu;
    for (i = 0; i < n; i++)
        sum += b[i];
    b[n++] = (uint8_t)(sum & 0x7Fu);
    return n;
}

static int sl_decode(const uint8_t *b, uint32_t n, preset_t *p)   /* 0: a good body, checked into p */
{
    uint32_t i, sum = 0;
    if (!((n == SL_BODY && b[0] == 2u) || (n == SL_BODY1 && b[0] == 1u)) || b[1u + NAME_LEN] != NSP_STORED)
        return -1;
    for (i = 0; i + 1u < n; i++)
        sum += b[i];
    if ((sum & 0x7Fu) != b[n - 1u])
        return -1;
    memset(p, 0, sizeof *p);
    for (i = 0; i < NAME_LEN; i++)
        p->name[i] = (char)b[1u + i];
    for (i = 0; i < NSP_STORED; i++) {
        uint32_t hi = b[2u + NAME_LEN + 2u * i], lo = b[3u + NAME_LEN + 2u * i];
        if (hi > 1u)
            return -1;
        p->p[i] = (int8_t)((int32_t)(hi << 7 | lo) - 128);
        if (n == SL_BODY)
            p->f[i] = b[2u + NAME_LEN + 2u * NSP_STORED + i];
    }
    preset_check(p);                             /* ranges, the name's characters (never trust what comes in) */
    return 0;
}

static void sl_send(void)                        /* the reply, when the queue has room */
{
    uint8_t f[8u + SL_BODY + 1u];
    uint32_t n = 0;
    f[n++] = 0xF0;
    f[n++] = SL_H0;
    f[n++] = SL_H1;
    f[n++] = SL_H2;
    f[n++] = sl.pending;
    if (sl.pending == SL_SLOT)
        f[n++] = sl.slot;
    n += sl_encode(sl.pending == SL_SLOT ? &user[sl.slot] : &cur, f + n);
    f[n++] = 0xF7;
    if (!sr_room(n)) {
        if (fm1_ms - sl.since > 1000u)           /* nobody reads it: given up */
            sl.pending = 0;
        return;
    }
    ota_wire_send(f, n);
    sl.replies++;
    sl.pending = 0;
}

/* a received frame (between F0 and F7): 1 if it is ours (the caller releases it) */
static int sl_frame(const uint8_t *b, uint32_t n)
{
    preset_t p;
    char l[8], m[24];
    if (n < 4u || b[0] != SL_H0 || b[1] != SL_H1 || b[2] != SL_H2)
        return 0;
    switch (b[3]) {
    case SL_GET:
        sl.pending = SL_SOUND;
        sl.since = fm1_ms;
        break;
    case SL_GETSLOT:
        if (n == 5u && b[4] < NUSER) {
            sl.pending = SL_SLOT;
            sl.slot = b[4];
            sl.since = fm1_ms;
        }
        break;
    case SL_SOUND:
        if (sl_decode(b + 4, n - 4u, &p)) {
            sl.rejected++;
            msg("SYSEX SOUND REJECTED");
            break;
        }
        cur = p;                                 /* the current sound, from no preset: SAVE stores it */
        cur_index = PRESET_NEW;
        snd_epoch++;
        snd_update();
        msg("SOUND RECEIVED");
        break;
    case SL_SLOT:
        if (n < 5u || b[4] >= NUSER || sl_decode(b + 5, n - 5u, &p)) {
            sl.rejected++;
            msg("SYSEX SLOT REJECTED");
            break;
        }
        user[b[4]] = p;
        preset_label(l, sizeof l, NFACT + b[4]);
        if (data_save_user(b[4]))
            msg("SAVE FAILED");
        else {
            str_cpy(m, "RECEIVED ", sizeof m);
            cat(m, l, sizeof m);
            msg(m);
        }
        break;
    }
    return 1;
}

static void sl_frame_tick(void)                  /* every frame: a reply waiting? */
{
    if (sl.pending)
        sl_send();
}
