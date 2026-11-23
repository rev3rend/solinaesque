/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M-Vave-OP1 contributors
 * Copyright (C) 2026 R. Luke DuBois */
/* Solinaesque: a paraphonic divide-down string machine (docs/SPEC.md). The app contract's five hooks (app.h) and
 * the parts, in dependency order:
 *   data.c    the parameters, the sound being played, the settings, the factory and user presets, their records
 *   fx.c      the effects (audio ISR, on the FPU): ensemble, flanger, delay
 *   sound.c   the audio ISR: the divide-down oscillator bank, the registers, the key gates, the shared envelope
 *   ui.c      the panel: controls, LEDs, the HOME, page and SAVE screens */
#ifndef APP_NAME
#define APP_NAME "SOLINAESQUE"
#endif
#ifndef APP_VERSION
#define APP_VERSION "v0.1"       /* build.py --release X.Y */
#endif
#define APP_ID 0x1D39CD8Eu       /* this app's records in the store (store.c): every app its own (setup/new_project.py
                                  * drew it), so another firmware's data in the same flash reads as empty */

#include "data.c"
#include "fx.c"
#include "sound.c"
#include "ui.c"

static void app_init(void)
{
    data_load();
    snd_init();
    vox.hold = 0;
    ui_init();
    ui_draw();
}

static void app_frame(void) { ui_frame(); }

static const char *app_fault_name(void) { return APP_NAME " CRASH"; }
