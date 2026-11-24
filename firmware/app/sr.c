/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 R. Luke DuBois */
/* The screen reader link (main loop only): the fm1-reader protocol, version 1, as
 * https://github.com/charlesvestal/fm1-reader (PROTOCOL.md) defines it, written here from that description. A reader
 * on the computer (a web page or a terminal tool) speaks what the FM-1 sends; the FM-1 makes no sound of its own.
 *
 *   frames   USB-MIDI SysEx F0 7D 53 52 <cmd> <args> F7, every byte 7-bit; text is ASCII
 *   reader   1 HELLO <version>: start or keep listening (every 2 s; nothing for 5 s: stopped); 2 BYE; 3 WHERE: the
 *            whole screen now
 *   device   1 HELLO 01 <name> 00 <words link>: the answer to every HELLO (the link may be empty: Solinaesque speaks
 *            in full words, the reader needs no words file); 16 SAY <flags> <text>: flags 1 NOW (cut what is being
 *            said: the player caused it), 2 KEEP (a warning: heard whole)
 *   silence  nothing is sent until a HELLO, so a DAW on the same port sees no bytes; the first HELLO after silence
 *            reads the whole screen (as WHERE)
 *   never    waits: a message the USB queue has no room for is dropped (the reader repeats nothing, the next change
 *            says it again); the platform's queue is 64 packets of 3 bytes, so a SAY carries at most SR_MAX (160)
 *            characters, cut at a space
 * One debounced message waits in sr.later (knob values: said after 120 ms still, at least every 4 x that while
 * turning; pages after 200 ms); a newer one replaces it. */

#define SR_H0 0x7Du              /* non-commercial manufacturer id, then "SR" */
#define SR_H1 0x53u
#define SR_H2 0x52u
#define SR_VER 1u
enum { SR_HELLO = 1, SR_BYE = 2, SR_WHERE = 3, SR_SAY = 16 };
#define SR_NOW 1u
#define SR_KEEP 2u
#define SR_MAX 160u              /* characters of a SAY: (160 + 7 bytes) / 3 = 56 packets of the queue's 64 */
#define SR_GONE_MS 5000u
#define SR_NAME APP_NAME " " APP_VERSION  /* the HELLO answer's name: the firmware and its version */

static struct {
    uint8_t on;                  /* a reader is listening */
    uint8_t where;               /* it asked for the whole screen (sr_where takes it) */
    uint32_t heard;              /* the last HELLO, fm1_ms */
    char later[SR_MAX + 1];      /* the debounced message waiting, "" none */
    uint8_t later_flags;
    uint32_t later_t, later_first, later_quiet;
    char more[3][SR_MAX + 1];    /* a long readout's further parts, sent a frame at a time as the queue has room */
    uint8_t more_flags[3], n_more;
    uint32_t sent, dropped;      /* (counts, for the tests) */
} sr;

/* room for n bytes in the USB SysEx queue (never wait): the target's ring, or the host's stand-in */
static int sr_room(uint32_t n)
{
#if FELUCCA_OTA
    return usb.config && !usb.suspended && SXQ - (so_w - so_r) >= (n + 2u) / 3u;
#else
    return usb.config && host_sx_room >= (n + 2u) / 3u;
#endif
}

/* one frame: F0 7D 53 52 cmd flags a [00 b] F7 (b != 0: the HELLO answer's second string, after a 00) */
static void sr_send(uint32_t cmd, uint32_t flags, const char *a, const char *b)
{
    uint8_t f[8u + SR_MAX + 1u];
    uint32_t n = 0, k = 0, cut;
    f[n++] = 0xF0;
    f[n++] = SR_H0;
    f[n++] = SR_H1;
    f[n++] = SR_H2;
    f[n++] = (uint8_t)(cmd & 0x7Fu);
    f[n++] = (uint8_t)(flags & 0x7Fu);
    while (a && a[k] && k < SR_MAX)
        k++;
    cut = k;
    if (a && a[k] && k == SR_MAX) {              /* too long: back to the last space */
        while (cut && a[cut] != ' ')
            cut--;
        while (cut && a[cut - 1u] == ' ')
            cut--;
    }
    for (k = 0; k < cut; k++)
        f[n++] = (uint8_t)a[k] & 0x7Fu;          /* (ASCII: the text is made so; 7-bit regardless) */
    if (b) {
        f[n++] = 0x00;                           /* the separator, there even when b is empty */
        for (k = 0; b[k] && n < sizeof f - 1u; k++)
            f[n++] = (uint8_t)b[k] & 0x7Fu;
    }
    f[n++] = 0xF7;
    if (!sr_room(n) || ota_wire_send(f, n)) {
        sr.dropped++;
        return;
    }
    sr.sent++;
}

/* a received frame (the bytes between F0 and F7): 1 if it is the reader's (the caller releases it) */
static int sr_frame(const uint8_t *p, uint32_t n, uint32_t now)
{
    if (n < 4u || p[0] != SR_H0 || p[1] != SR_H1 || p[2] != SR_H2)
        return 0;
    switch (p[3]) {
    case SR_HELLO:
        if (!sr.on)
            sr.where = 1;                        /* a new reader: the whole screen */
        sr.on = 1;
        sr.heard = now;
        sr_send(SR_HELLO, SR_VER, SR_NAME, "");
        break;
    case SR_BYE:
        sr.on = 0;
        sr.later[0] = 0;
        sr.n_more = 0;
        break;
    case SR_WHERE:
        sr.where = 1;
        break;
    }
    return 1;                                    /* (an unknown command of ours: taken, ignored) */
}

static int sr_on(void) { return sr.on; }

static int sr_where(void)                        /* the whole screen was asked for (once) */
{
    int w = sr.where && sr.on;
    sr.where = 0;
    return w;
}

/* say now: NOW drops a debounced message that waits; otherwise that one goes first */
static void sr_say(const char *s, uint32_t flags)
{
    if (!sr.on || !s || !s[0])
        return;
    if (flags & SR_NOW)
        sr.n_more = 0;                           /* what is still to come of an older readout goes */
    if (sr.later[0]) {
        if (!(flags & SR_NOW))
            sr_send(SR_SAY, sr.later_flags, sr.later, 0);
        sr.later[0] = 0;
    }
    sr_send(SR_SAY, flags, s, 0);
}

/* a readout longer than a SAY: cut at sentence ends (". "), the first part now, the rest queued behind it */
static void sr_say_long(const char *s, uint32_t flags)
{
    char part[SR_MAX + 1];
    uint32_t start = 0, first = 1;
    if (!sr.on || !s)
        return;
    while (s[start]) {
        uint32_t end = start, cut = 0, k;
        while (s[end] && end - start < SR_MAX) {
            if (s[end] == '.' && s[end + 1u] == ' ')
                cut = end + 1u;
            end++;
        }
        if (!s[end])
            cut = end;
        else if (!cut)                           /* no sentence end in reach: a space */
            for (cut = end; cut > start && s[cut] != ' '; cut--)
                ;
        if (cut <= start)
            cut = end;
        for (k = 0; k < cut - start && k < SR_MAX; k++)
            part[k] = s[start + k];
        part[k] = 0;
        if (first) {
            sr_say(part, flags);
            first = 0;
        } else if (sr.n_more < 3u) {
            str_cpy(sr.more[sr.n_more], part, SR_MAX + 1u);
            sr.more_flags[sr.n_more++] = (uint8_t)(flags & ~SR_NOW);
        }
        start = cut;
        while (s[start] == ' ')
            start++;
    }
}

/* say when things settle: after quiet_ms with no newer message, or every 4 x quiet_ms while they keep changing */
static void sr_later(const char *s, uint32_t flags, uint32_t quiet_ms, uint32_t now)
{
    if (!sr.on || !s || !s[0])
        return;
    if (!sr.later[0])
        sr.later_first = now;
    str_cpy(sr.later, s, sizeof sr.later);
    sr.later_flags = (uint8_t)flags;
    sr.later_t = now;
    sr.later_quiet = quiet_ms;
}

static void sr_tick(uint32_t now)                /* every frame: the reader gone? a debounced message due? */
{
    if (sr.on && now - sr.heard > SR_GONE_MS) {
        sr.on = 0;
        sr.later[0] = 0;
        sr.n_more = 0;
    }
    if (sr.n_more && sr_room(8u + str_len(sr.more[0]))) {   /* a long readout's next part, when it fits */
        uint32_t i;
        sr_send(SR_SAY, sr.more_flags[0], sr.more[0], 0);
        for (i = 1; i < sr.n_more; i++) {
            str_cpy(sr.more[i - 1u], sr.more[i], SR_MAX + 1u);
            sr.more_flags[i - 1u] = sr.more_flags[i];
        }
        sr.n_more--;
        return;
    }
    if (sr.later[0] && !sr.n_more &&
        (now - sr.later_t >= sr.later_quiet || now - sr.later_first >= 4u * sr.later_quiet)) {
        sr_send(SR_SAY, sr.later_flags, sr.later, 0);
        sr.later[0] = 0;
    }
}

