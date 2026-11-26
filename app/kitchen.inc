/* SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
 * Timer Tom - four kitchen timers for the M-VAVE FM-1 (two-tone LCD edition).
 *
 * Included once by screen.c, after its drawing helpers and before
 * apply_controls(). It replaces the DRUM page of Drum Peter: the page id is
 * still DRUM inside screen.c, everything else here is the KITCHEN page.
 * Everything runs on the control task and uses only the public instrument API.
 *
 * KITCHEN page:
 *   K1..K4      set the time of timer 1..4 (and select it)
 *   PRESETS     icon of the selected timer (12 dishes)
 *   SELECT      sound of the key loops         ALGORITHM  off / tick 1 / tick 2 / rhythm
 *   PLAY        start / pause the selected timer, stop its alarm
 *   PLAY (hold) start all timers that are set (or pause all running ones)
 *   REC         reset the selected timer; again to clear it
 *   OCT - / +   octave of the key loops        keys       start a loop (same key stops it)
 *
 * Sound: T1 DX7 ambient pad (slow LFO, long release, big reverb) playing the
 * key loops from the engine's own sequencer (64 steps, tied notes), T2 the 808
 * rimshot as a soft clock tick, T3 a DX7 kalimba for the alarm, T4..T7 four
 * 808 tracks of soft ambient-techno percussion: each running timer unmutes one
 * of them (timer 1 kick, 2 open hat, 3 rim echo, 4 shaker). T8 is off.
 * The keyboard lights show an hourglass of the timer that ends first: black
 * keys are the upper bulb, white keys the lower one, and a grain falls from
 * the middle black key to the middle white keys on every tick.
 */
#include "drum808.h"

/* KT_MAX_S keeps every millisecond product below 2^32 (no 64-bit division). */
enum { KT_TIMERS = 4, KT_ICONS = 12, KT_SOUNDS = 4, KT_MAX_S = 4 * 3600, KT_RING_MS = 180000 };
enum { KT_OFF, KT_SET, KT_RUN, KT_PAUSE, KT_DONE };
enum { KT_PAD = 0, KT_TICK = 1, KT_BELL = 2, KT_PERC = 3, KT_USED = 7 }; /* tracks */
enum { KT_SILENT, KT_TICK1, KT_TICK2, KT_RHYTHM, KT_MODES, KT_TEMPO = 118 };
enum { KT_C_HEAD, KT_C_ROW, KT_C_LINE = KT_C_ROW + KT_TIMERS, KT_C_COUNT = KT_C_LINE + 3 };
_Static_assert(KT_C_COUNT < 16, "KITCHEN page cache slots");

/* 16 x 16 icons, bit 15 is the left pixel; drawn twice the size.
 * Generated from ASCII art by tools/icons.py. */
static const uint16_t kt_icons[KT_ICONS][16] = {
    {0x0000, 0x03c0, 0x07e0, 0x0e70, 0x0df0, 0x1bf8, 0x1ff8, 0x3ffc, 0x3ffc, 0x3ffc, 0x3ffc, 0x3ffc, 0x1ff8, 0x0ff0, 0x07e0, 0x0000}, /* EGGS */
    {0x0015, 0x0015, 0x001f, 0x000e, 0x0010, 0x2a20, 0x5540, 0xaaa8, 0xffff, 0xffff, 0x7ffe, 0x3ffc, 0x1ff8, 0x0ff0, 0x03c0, 0x0000}, /* PASTA */
    {0x0000, 0x0420, 0x0840, 0x0420, 0x0840, 0x0000, 0x07e0, 0x15a8, 0x2d6c, 0xffff, 0xffff, 0x7ffe, 0x3ffc, 0x1ff8, 0x07e0, 0x0ff0}, /* RICE */
    {0x0000, 0x0000, 0x07e0, 0x1ff8, 0x3dfc, 0x7ffe, 0x7ef6, 0xffff, 0xffff, 0x77fe, 0x7fde, 0x3ffc, 0x1ff0, 0x0000, 0x0000, 0x0000}, /* POTATO */
    {0x0888, 0x1110, 0x0888, 0x1110, 0x0000, 0x7ffe, 0x0000, 0x3ffc, 0xffff, 0xbffd, 0x3ffc, 0x3ffc, 0x3ffc, 0x3ffc, 0x1ff8, 0x0000}, /* SOUP */
    {0x0000, 0x0078, 0x01fe, 0x03fe, 0x07fe, 0x07fe, 0x07fc, 0x03f8, 0x0770, 0x0e00, 0x1c00, 0x7800, 0xf800, 0x6000, 0x0000, 0x0000}, /* MEAT */
    {0x0000, 0x0000, 0x0000, 0x07e0, 0x1ff1, 0x3ffb, 0x6ffd, 0xfffe, 0xfffe, 0x7ffd, 0x3ffb, 0x1ff1, 0x07e0, 0x0000, 0x0000, 0x0000}, /* FISH */
    {0xffff, 0xffff, 0x4002, 0x5832, 0x3874, 0x2824, 0x1008, 0x1188, 0x0b90, 0x0910, 0x0420, 0x0420, 0x0240, 0x0240, 0x0180, 0x0000}, /* PIZZA */
    {0x0100, 0x0380, 0x0100, 0x0100, 0x0100, 0x1ff8, 0x2004, 0x3ffc, 0x2aaa, 0x3ffc, 0x2004, 0x7ffe, 0x5555, 0x7ffe, 0xffff, 0x0000}, /* CAKE */
    {0x0000, 0x0000, 0x0ff0, 0x3ffc, 0x6666, 0x7332, 0xffff, 0xffff, 0x7ffe, 0x7ffe, 0x7ffe, 0x7ffe, 0x7ffe, 0x3ffc, 0x0000, 0x0000}, /* BREAD */
    {0x0012, 0x0024, 0x00fc, 0x01f0, 0x03f6, 0x07e0, 0x0be0, 0x0fc0, 0x1e80, 0x1f80, 0x3700, 0x3e00, 0x7800, 0x6000, 0x8000, 0x0000}, /* VEG */
    {0x0440, 0x0880, 0x0440, 0x0880, 0x0000, 0x7ff0, 0x7ff6, 0x7ff1, 0x7ff1, 0x7ff2, 0x3fec, 0x1fc0, 0x0000, 0xffff, 0x7ffe, 0x0000}, /* TEA */
};
static const char kt_icon_names[KT_ICONS][7] = {"EGGS", "PASTA", "RICE", "POTATO", "SOUP", "MEAT", "FISH", "PIZZA", "CAKE", "BREAD", "VEG", "TEA"};

static const struct { uint8_t program; char name[11]; uint8_t speed, pmd, amd; } kt_sounds[KT_SOUNDS] = {
    {23, "GLASS PAD", 10, 6, 40}, /* factory SYN PAD 6 */
    {82, "STRINGS", 8, 5, 30},    /* STRING 5 */
    {15, "WARM PAD", 12, 4, 45},  /* SYN PAD 4 */
    {95, "CHOIR", 9, 7, 35},      /* VOICE 8 */
};
/* Alarm chime of each timer, so the kitchen can hear which one is ready. */
static const uint8_t kt_chimes[KT_TIMERS][3] = {{76, 79, 84}, {74, 78, 81}, {72, 76, 79}, {79, 83, 86}};
static const uint8_t kt_black[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
/* Sand order: upper bulb drains from the edges, lower bulb piles up from the middle. */
static const uint8_t kt_top_order[11] = {5, 4, 6, 3, 7, 2, 8, 1, 9, 0, 10};
static const uint8_t kt_low_order[16] = {7, 8, 6, 9, 5, 10, 4, 11, 3, 12, 2, 13, 1, 14, 0, 15};
/* Percussion, one 808 track per timer: drum, 16 steps, two velocities
 * (even / odd step), volume, reverb and delay sends, decay, tune, tone. */
static const struct {
    uint8_t pad; uint16_t steps; uint8_t even, odd, volume, reverb, delay, decay, tune, tone;
} kt_perc[KT_TIMERS] = {
    {D8_BD, 0x1111, 110, 110, 127, 5, 0, 100, 46, 20}, /* kick, four on the floor, long and dark */
    {D8_OH, 0x4444, 76, 76, 110, 12, 6, 70, 62, 64},    /* open hat on the offbeat, washed in reverb */
    {D8_RS, 0x4848, 84, 84, 110, 8, 13, 40, 70, 64},    /* rim with dub delay */
    {D8_CH, 0xffff, 60, 40, 104, 9, 4, 18, 94, 64},     /* shaker sixteenths, odd steps soft and uncertain */
};
/* Key loops: 64 steps, eight tied notes. Contour (degrees), rhythm and scale
 * come from the key, the root is the key itself an octave down. */
static const uint8_t kt_contour[9][8] = {
    {0, 2, 4, 3, 2, 1, 0, 1}, {0, 4, 3, 5, 4, 2, 3, 1}, {2, 1, 0, 1, 2, 3, 4, 3},
    {0, 0, 3, 2, 0, 0, 4, 3}, {4, 3, 2, 0, 1, 2, 3, 2}, {0, 2, 1, 3, 2, 4, 3, 5},
    {5, 4, 2, 3, 0, 1, 2, 1}, {0, 3, 0, 4, 0, 5, 0, 2}, {1, 2, 4, 6, 4, 2, 1, 0}};
static const uint8_t kt_rhythm[3][8] = {
    {0, 8, 16, 24, 32, 40, 48, 56}, {0, 6, 16, 22, 32, 38, 48, 58}, {0, 12, 16, 28, 32, 40, 44, 56}};
static const uint8_t kt_scale[3][8] = {
    {0, 3, 5, 7, 10, 12, 15, 17}, {0, 2, 3, 5, 7, 9, 10, 12}, {0, 2, 4, 6, 7, 9, 11, 12}};

static struct { uint32_t set_ms, left_ms, end_at, done_at; uint8_t state, icon; } kt[KT_TIMERS];
static unsigned kt_sel, kt_sound, kt_tick_level = KT_RHYTHM, kt_ready;
static unsigned kt_loop = 255, kt_step_seen = 255; /* key of the running loop */
static int kt_loop_octave;
static uint32_t kt_transport_at;
static unsigned kt_play_down, kt_play_long;
static uint32_t kt_play_ms;
static int kt_focus_seen = -1;
static unsigned kt_tick_sec, kt_tock, kt_tick_note = 255;
static uint32_t kt_tick_at;
static int kt_ring = -1;
static unsigned kt_chime_n, kt_bell_note = 255;
static uint32_t kt_chime_at;
static char kt_msg[32];

static void dp_init(void)
{
    memset(kt, 0, sizeof kt);
    kt[0].icon = 0; kt[1].icon = 1; kt[2].icon = 2; kt[3].icon = 5;
    kt_sel = kt_sound = kt_ready = kt_play_down = kt_play_long = 0;
    kt_tick_level = KT_RHYTHM; kt_focus_seen = kt_ring = -1;
    kt_loop = kt_step_seen = 255;
    kt_tick_note = kt_bell_note = 255;
}

/* ---- Time ------------------------------------------------------------------ */
static uint32_t kt_left(unsigned i, uint32_t now)
{
    if (kt[i].state != KT_RUN) return kt[i].left_ms;
    int32_t left = (int32_t)(kt[i].end_at - now);
    return left > 0 ? (uint32_t)left : 0U;
}
static unsigned kt_seconds(uint32_t ms) { return (ms + 999U) / 1000U; }
static void kt_time(char out[12], unsigned s)
{
    unsigned h = s / 3600U, m = s / 60U % 60U, sec = s % 60U, n = 0;
    if (h) { out[n++] = (char)('0' + h); out[n++] = ':'; }
    else m = s / 60U;
    out[n++] = (char)('0' + m / 10U); out[n++] = (char)('0' + m % 10U); out[n++] = ':';
    out[n++] = (char)('0' + sec / 10U); out[n++] = (char)('0' + sec % 10U); out[n] = 0;
}
/* Knob step grows with the time: 5 s, 15 s, 30 s, 1 min, 5 min. */
static unsigned kt_step(unsigned s)
{
    return s < 60U ? 5U : s < 300U ? 15U : s < 900U ? 30U : s < 3600U ? 60U : 300U;
}
/* The running timer that ends first; the selected one if it runs. */
static int kt_focus(uint32_t now)
{
    if (kt[kt_sel].state == KT_RUN) return (int)kt_sel;
    int best = -1;
    for (unsigned i = 0; i < KT_TIMERS; ++i)
        if (kt[i].state == KT_RUN && (best < 0 || kt_left(i, now) < kt_left((unsigned)best, now))) best = (int)i;
    return best;
}
static void kt_say(const char *a, unsigned timer, const char *b, uint32_t now)
{
    unsigned n = 0;
    while (*a && n < 30) kt_msg[n++] = *a++;
    if (timer < KT_TIMERS && n < 30) kt_msg[n++] = (char)('1' + timer);
    while (b && *b && n < 30) kt_msg[n++] = *b++;
    kt_msg[n] = 0;
    notice = kt_msg; notice_until = now + 2500U;
}

/* ---- Sound ----------------------------------------------------------------- */
/* Absolute values through the relative edit API: first down to the minimum,
 * then up. The edit calls clamp, so no possibly stale snapshot is needed. */
static void kt_fm(unsigned t, unsigned parameter, unsigned value)
{
    groove_fm_edit(t, parameter, -255);
    if (value) groove_fm_edit(t, parameter, (int)value);
}
static void kt_volume(unsigned t, unsigned volume)
{
    groove_control_edit(t, 0, -127, 0);
    groove_control_edit(t, 0, (int)volume, 0);
}
static void kt_program(unsigned t, unsigned program)
{
    struct groove_view v = {0};
    groove_get_view(t, 0, &v);
    if (v.program != program) groove_control_edit(t, (int)program - (int)v.program, 0, 0);
}
/* One ambient voice: factory program, slow sine LFO on pitch and amplitude,
 * every operator a little amplitude sensitive, slow attack and a long release
 * on every operator (the timbre opens and closes with the envelopes). */
static void kt_voice(unsigned sound)
{
    kt_program(KT_PAD, kt_sounds[sound].program);
    kt_fm(KT_PAD, 137, kt_sounds[sound].speed);
    kt_fm(KT_PAD, 138, 0);
    kt_fm(KT_PAD, 139, kt_sounds[sound].pmd);
    kt_fm(KT_PAD, 140, kt_sounds[sound].amd);
    kt_fm(KT_PAD, 142, 4); /* sine */
    kt_fm(KT_PAD, 143, 3); /* pitch modulation sensitivity */
    for (unsigned op = 0; op < 6; ++op) {
        kt_fm(KT_PAD, op * 21U + 14U, 2U);  /* amplitude modulation sensitivity */
        kt_fm(KT_PAD, op * 21U + 0U, 45U);  /* attack rate: a slow swell */
        kt_fm(KT_PAD, op * 21U + 3U, 30U);  /* release rate: a long tail */
    }
}
static void kt_tone(unsigned t, unsigned part, const char *name, unsigned value)
{
    unsigned count = groove_tone_count(t, part);
    for (unsigned p = 0; p < count && p < GROOVE_TONE_PARAMS; ++p) {
        const fm1_param_t *d = groove_tone_parameter(t, part, p);
        const char *a = d ? d->name : 0, *b = name;
        while (a && *a && *a == *b) { ++a; ++b; }
        if (!a || *a || *b) continue;
        groove_tone_edit(t, part, p, -255);
        groove_tone_edit(t, part, p, (int)value);
        return;
    }
}
static void kt_set_send(unsigned t, unsigned which, unsigned value)
{
    groove_sends_edit(t, which, -15);
    if (value) groove_sends_edit(t, which, (int)value);
}
/* The kitchen setup, every start: three tracks, no steps, nothing muted. */
static void kt_setup(void)
{
    for (unsigned t = 0; t < GROOVE_TRACKS; ++t) {
        struct groove_view v = {0};
        groove_get_view(t, 0, &v);
        if (groove_arp_enabled(t)) groove_arp_set(t, 0);
        if (v.muted) groove_mix_toggle(t, 0);
        if (v.solo) groove_mix_toggle(t, 1);
        int role = t < KT_USED ? GROOVE_TRACK_NORMAL : GROOVE_TRACK_OFF;
        if ((int)groove_track_role(t) != role) groove_settings_track(t, 0, role - (int)groove_track_role(t));
        if (t >= KT_USED) continue;
        unsigned type = t == KT_PAD || t == KT_BELL ? GROOVE_DX7 : GROOVE_808;
        if (groove_source_type(t) != type) groove_source_set(t, type);
        groove_edit_clear(t, 0, 2);
        groove_sequence_length_set(t, t == KT_PAD ? 64 : 16);
    }
    /* Percussion: patterns written once, muted until their timers run. */
    for (unsigned i = 0; i < KT_TIMERS; ++i) {
        unsigned t = KT_PERC + i, note = groove_pad_note(t, kt_perc[i].pad);
        kt_tone(t, kt_perc[i].pad, "Decay", kt_perc[i].decay);
        kt_tone(t, kt_perc[i].pad, "Tune", kt_perc[i].tune);
        kt_tone(t, kt_perc[i].pad, "Tone", kt_perc[i].tone);
        kt_volume(t, kt_perc[i].volume);
        kt_set_send(t, 0, kt_perc[i].reverb); kt_set_send(t, 1, kt_perc[i].delay);
        for (unsigned step = 0; step < 16; ++step)
            if ((kt_perc[i].steps >> step) & 1U) {
                groove_sequence_edit(t, step, note, step & 1U ? kt_perc[i].odd : kt_perc[i].even);
                if (i == 3 && (step & 1U)) groove_step_setting_edit(t, step, 2, 5); /* 5/8 chance */
            }
        groove_mix_toggle(t, 0);
    }
    {
        struct groove_view v = {0};
        groove_get_view(0, 0, &v);
        if (!v.clock_mode && v.tempo) groove_tempo_edit(KT_TEMPO - (int)v.tempo);
        groove_sequence_setting_edit(0, 10 - (int)groove_sequence_setting(0)); /* a little swing */
    }
    kt_voice(kt_sound);
    /* The bell: kalimba, plain envelope, some reverb. */
    kt_program(KT_BELL, 117);
    kt_volume(KT_PAD, 124); kt_volume(KT_TICK, 84); kt_volume(KT_BELL, 100);
    kt_set_send(KT_PAD, 0, 12); kt_set_send(KT_PAD, 1, 5);
    kt_set_send(KT_BELL, 0, 8); kt_set_send(KT_BELL, 1, 3);
    kt_set_send(KT_TICK, 0, 0); kt_set_send(KT_TICK, 1, 0);
    /* The tick: a short, high, dry rimshot. */
    kt_tone(KT_TICK, D8_RS, "Decay", 12);
    kt_tone(KT_TICK, D8_RS, "Tune", 104);
    /* Reverb decay, delay feedback and effect return. */
    groove_effects_edit(0, -127); groove_effects_edit(0, 108);
    groove_effects_edit(2, -127); groove_effects_edit(2, 64);
    groove_effects_edit(3, -127); groove_effects_edit(3, 112);
    for (unsigned i = 0; i < 12; ++i) groove_project_rename(i, "TIMER TOM   "[i]);
    track = 0; octave = 0;
}
static void kt_note(unsigned t, unsigned note, unsigned velocity)
{
    post((uint8_t)((velocity ? 0x90 : 0x80) + groove_midi_input_channel(t)), (uint8_t)note, (uint8_t)velocity, 3);
}

/* ---- Key loops ------------------------------------------------------------- */
static void kt_loop_write(unsigned key)
{
    groove_edit_clear(KT_PAD, 0, 2);
    kt_loop = key;
    kt_loop_octave = octave;
    if (key == 255) return;
    const uint8_t *contour = kt_contour[key % 9], *at = kt_rhythm[(key / 9) % 3], *scale = kt_scale[(key / 3) % 3];
    int root = 41 + (int)key + octave * 12;
    for (unsigned n = 0; n < 8; ++n) {
        int note = clamp(root + scale[contour[n]], 24, 108);
        groove_sequence_edit(KT_PAD, at[n], (unsigned)note, 48U + (n * 7U + key) % 22U);
        groove_step_setting_edit(KT_PAD, at[n], 4, 1); /* tie: hold until the next note */
    }
}
/* Tap a key: its loop starts; the same key again stops it. */
static void kt_key(unsigned key, uint32_t now)
{
    if (key == kt_loop) { kt_loop_write(255); kt_say("LOOP OFF", 9, 0, now); return; }
    kt_loop_write(key);
    char n[4] = {(char)('0' + (key + 1) / 10), (char)('0' + (key + 1) % 10), 0, 0};
    kt_say("LOOP ", 9, n, now);
}
/* The engine transport runs while the rhythm or a loop has something to play. */
static void kt_transport(uint32_t now)
{
    unsigned want = kt_loop != 255;
    for (unsigned i = 0; i < KT_TIMERS; ++i) want |= kt_tick_level == KT_RHYTHM && kt[i].state == KT_RUN;
    struct groove_view v = {0};
    groove_get_view(KT_PERC, 0, &v);
    if (v.playing != want && now - kt_transport_at >= 300U) {
        kt_transport_at = now;
        post(want ? 0xfa : 0xfc, 0, 0, 1);
    }
    /* Each running timer unmutes its percussion track (rhythm mode only). */
    for (unsigned i = 0; i < KT_TIMERS; ++i) {
        struct groove_view p = {0};
        groove_get_view(KT_PERC + i, 0, &p);
        unsigned mute = !(kt_tick_level == KT_RHYTHM && kt[i].state == KT_RUN);
        if (p.muted != mute) groove_mix_toggle(KT_PERC + i, 0);
    }
    /* In rhythm mode the hourglass grain falls on every beat. */
    if (v.playing && v.play_step != kt_step_seen) {
        kt_step_seen = v.play_step;
        if (kt_tick_level == KT_RHYTHM && !(v.play_step & 3U)) { kt_tick_at = now; ++kt_tick_sec; }
    } else if (!v.playing) kt_step_seen = 255;
}

/* ---- Timer actions --------------------------------------------------------- */
static void kt_start(unsigned i, uint32_t now)
{
    if (kt[i].state != KT_SET && kt[i].state != KT_PAUSE) return;
    if (!kt[i].left_ms) return;
    kt[i].end_at = now + kt[i].left_ms;
    kt[i].state = KT_RUN;
}
static void kt_pause(unsigned i, uint32_t now)
{
    if (kt[i].state != KT_RUN) return;
    kt[i].left_ms = kt_left(i, now);
    kt[i].state = KT_PAUSE;
}
/* Back to the time that was set; stops an alarm. */
static void kt_stop(unsigned i)
{
    kt[i].left_ms = kt[i].set_ms;
    kt[i].state = kt[i].set_ms ? KT_SET : KT_OFF;
}
static void kt_turn(unsigned i, int delta, uint32_t now)
{
    if (i != kt_sel) {
        kt_sel = i;
        /* A running, paused or ringing timer is only selected by the first click. */
        if (kt[i].state >= KT_RUN) return;
    }
    if (kt[i].state == KT_DONE) { kt_stop(i); kt_say("ALARM ", i, " OFF", now); return; }
    uint32_t left = kt_left(i, now);
    int cur = (int)kt_seconds(left), s = (int)kt_step(delta > 0 ? (unsigned)cur : cur ? (unsigned)cur - 1U : 0U);
    int next = delta > 0 ? (cur / s + delta) * s : ((cur + s - 1) / s + delta) * s;
    next = clamp(next, 0, KT_MAX_S);
    uint32_t ms = (uint32_t)next * 1000U;
    if (kt[i].state == KT_RUN) {
        if (!next) { kt[i].state = KT_OFF; kt[i].set_ms = kt[i].left_ms = 0; return; }
        kt[i].end_at = now + ms;
        if (ms > kt[i].set_ms) kt[i].set_ms = ms;
    } else if (kt[i].state == KT_PAUSE) {
        kt[i].left_ms = ms;
        if (ms > kt[i].set_ms || !next) kt[i].set_ms = ms;
        if (!next) kt[i].state = KT_OFF;
    } else {
        kt[i].left_ms = kt[i].set_ms = ms;
        kt[i].state = next ? KT_SET : KT_OFF;
    }
}
static void kt_tap(uint32_t now)
{
    unsigned i = kt_sel;
    switch (kt[i].state) {
    case KT_OFF: kt_say("TURN K", i, " TO SET A TIME", now); break;
    case KT_SET: case KT_PAUSE: kt_start(i, now); kt_say("TIMER ", i, " RUNS", now); break;
    case KT_RUN: kt_pause(i, now); kt_say("TIMER ", i, " PAUSED", now); break;
    default: kt_stop(i); kt_say("ALARM ", i, " OFF", now); break;
    }
}
static void kt_all(uint32_t now)
{
    unsigned done = 0, ready = 0, running = 0;
    for (unsigned i = 0; i < KT_TIMERS; ++i) {
        done |= kt[i].state == KT_DONE;
        ready |= kt[i].state == KT_SET || kt[i].state == KT_PAUSE;
        running |= kt[i].state == KT_RUN;
    }
    if (done) {
        for (unsigned i = 0; i < KT_TIMERS; ++i) if (kt[i].state == KT_DONE) kt_stop(i);
        kt_say("ALL ALARMS OFF", 9, 0, now);
    } else if (ready) {
        for (unsigned i = 0; i < KT_TIMERS; ++i) kt_start(i, now);
        kt_say("ALL TIMERS RUN", 9, 0, now);
    } else if (running) {
        for (unsigned i = 0; i < KT_TIMERS; ++i) kt_pause(i, now);
        kt_say("ALL TIMERS PAUSED", 9, 0, now);
    } else kt_say("NO TIMER IS SET", 9, 0, now);
}
static void kt_reset(uint32_t now)
{
    unsigned i = kt_sel;
    if (kt[i].state == KT_SET || kt[i].state == KT_OFF) {
        kt[i].set_ms = kt[i].left_ms = 0; kt[i].state = KT_OFF;
        kt_say("TIMER ", i, " CLEARED", now);
    } else { kt_stop(i); kt_say("TIMER ", i, " RESET", now); }
}

/* ---- Controls -------------------------------------------------------------- */
static void kt_controls(const struct fm1_controls *c, uint32_t pressed, uint32_t now)
{
    /* Held-button gestures of the other pages do not exist here. */
    arp_down = arp_long = seq_down = edit_down = edit_long = glo_down = glo_used = 0;
    for (unsigned i = 0; i < KT_TIMERS; ++i)
        if (c->encoders[2 + i]) kt_turn(i, fm_encoder_delta(2 + i, c->encoders[2 + i]), now);
    if (c->encoders[6]) {
        kt[kt_sel].icon = (uint8_t)(((int)kt[kt_sel].icon + c->encoders[6] % KT_ICONS + KT_ICONS) % KT_ICONS);
        kt_say("TIMER ", kt_sel, " ICON", now);
    }
    if (c->encoders[0]) {
        unsigned next = (unsigned)clamp((int)kt_sound + c->encoders[0], 0, KT_SOUNDS - 1);
        if (next != kt_sound && release_keys()) { kt_sound = next; kt_voice(kt_sound); }
        kt_say("SOUND ", 9, kt_sounds[kt_sound].name, now);
    }
    if (c->encoders[1]) {
        kt_tick_level = (unsigned)clamp((int)kt_tick_level + c->encoders[1], 0, KT_MODES - 1);
        static const char *const levels[KT_MODES] = {"SILENT", "TICK SOFT", "TICK CLEAR", "RHYTHM: ONE DRUM PER TIMER"};
        kt_say(levels[kt_tick_level], 9, 0, now);
    }
    int oct = !!(pressed & BUTTON(OCT_UP)) - !!(pressed & BUTTON(OCT_DOWN));
    if (oct) {
        octave = clamp(octave + oct, -2, 2);
        if (kt_loop != 255 && octave != kt_loop_octave) kt_loop_write(kt_loop);
    }
    if (pressed & BUTTON(REC)) kt_reset(now);
    if (pressed & BUTTON(PLAY)) { kt_play_down = 1; kt_play_long = 0; kt_play_ms = now; }
    if (kt_play_down && !(c->buttons & BUTTON(PLAY))) {
        if (!kt_play_long) kt_tap(now);
        kt_play_down = 0;
    } else if (kt_play_down && !kt_play_long && now - kt_play_ms >= 600U) {
        kt_play_long = 1;
        kt_all(now);
    }
}
/* The keys start and stop loops; screen.c plays no notes from them. */
static uint32_t dp_keys(uint32_t keys, uint32_t key_down, uint32_t held)
{
    (void)held;
    for (unsigned key = 0; key < 27; ++key)
        if ((key_down & keys) & (1U << key)) kt_key(key, fm1_millis());
    return 0;
}
/* Runs on every control poll: setup, timers ending, ticks and alarm chimes. */
static void dp_poll(uint32_t now)
{
    if (boot_active) return;
    if (!kt_ready) { kt_ready = 1; kt_setup(); }
    for (unsigned i = 0; i < KT_TIMERS; ++i)
        if (kt[i].state == KT_RUN && !kt_left(i, now)) {
            kt[i].state = KT_DONE; kt[i].done_at = now; kt[i].left_ms = 0;
            kt_sel = i;
            kt_say("TIMER ", i, " IS READY", now);
            kt_ring = -1; /* restart the chime with this timer */
        }
    kt_transport(now);
    /* Tick: once per second of the focus timer, alternately tick and tock. */
    if (kt_tick_note != 255 && now - kt_tick_at >= 60U) { kt_note(KT_TICK, kt_tick_note, 0); kt_tick_note = 255; }
    int f = kt_focus(now);
    if (f >= 0) {
        unsigned sec = kt_seconds(kt_left((unsigned)f, now));
        if (kt_tick_level != KT_RHYTHM && (f != kt_focus_seen || sec != kt_tick_sec)) {
            kt_focus_seen = f; kt_tick_sec = sec; kt_tick_at = now; kt_tock ^= 1U;
            static const uint8_t velocity[KT_MODES] = {0, 20, 40, 0};
            unsigned v = velocity[kt_tick_level];
            if (v) {
                if (kt_tock) v -= v / 3U;
                if (sec <= 10U) v += 8U;
                if (kt_tick_note != 255) kt_note(KT_TICK, kt_tick_note, 0);
                kt_tick_note = groove_pad_note(KT_TICK, D8_RS);
                if (kt_tick_note < 128) kt_note(KT_TICK, kt_tick_note, v);
                else kt_tick_note = 255;
            }
        }
    } else kt_focus_seen = -1;
    /* Alarm: the lowest ready timer chimes its three notes every 2.4 s for three minutes. */
    int ring = -1;
    for (unsigned i = 0; i < KT_TIMERS && ring < 0; ++i)
        if (kt[i].state == KT_DONE && now - kt[i].done_at < KT_RING_MS) ring = (int)i;
    if (ring != kt_ring) { kt_ring = ring; kt_chime_at = now; kt_chime_n = 0; }
    if (kt_bell_note != 255 && (ring < 0 || (kt_chime_n >= 3 && now - kt_chime_at >= 900U))) {
        kt_note(KT_BELL, kt_bell_note, 0); kt_bell_note = 255;
    }
    if (ring < 0) return;
    if (now - kt_chime_at >= 2400U) { kt_chime_at = now; kt_chime_n = 0; }
    if (kt_chime_n < 3 && now - kt_chime_at >= kt_chime_n * 220U) {
        if (kt_bell_note != 255) kt_note(KT_BELL, kt_bell_note, 0);
        kt_bell_note = kt_chimes[ring][kt_chime_n++];
        kt_note(KT_BELL, kt_bell_note, 84);
    }
}

/* ---- Lights ---------------------------------------------------------------- */
static void kt_leds(void)
{
    uint32_t now = frame_time, bits = 0;
    unsigned fast = (now / 125U) & 1U, any_done = 0, any_run = 0;
    for (unsigned i = 0; i < KT_TIMERS; ++i) { any_done |= kt[i].state == KT_DONE; any_run |= kt[i].state == KT_RUN; }
    int f = kt_focus(now);
    if (any_done) {
        /* Ready: white and black keys swap four times a second. */
        for (unsigned i = 0; i < 16; ++i) if (fast) bits |= 1U << white_keys[i];
        for (unsigned i = 0; i < 11; ++i) if (!fast) bits |= 1U << kt_black[i];
    } else if (f >= 0) {
        uint32_t left = kt_left((unsigned)f, now), total = kt[f].set_ms > left ? kt[f].set_ms : left;
        unsigned top = total ? (unsigned)((left * 11U + total - 1U) / total) : 0U;
        unsigned low = total ? (unsigned)(((total - left) * 16U + total / 2U) / total) : 16U;
        for (unsigned i = 0; i < top && i < 11; ++i) bits |= 1U << kt_black[kt_top_order[i]];
        for (unsigned i = 0; i < low && i < 16; ++i) bits |= 1U << white_keys[kt_low_order[i]];
        /* The grain of this second: middle black key, then the middle white key. */
        uint32_t phase = now - kt_tick_at;
        if (phase < 140U) bits ^= 1U << kt_black[5];
        else if (phase < 300U) bits ^= 1U << white_keys[kt_tick_sec & 1U ? 7 : 8];
    }
    /* The key of the running loop blinks twice a second. */
    if (kt_loop < 27 && !any_done) bits = (bits & ~(1U << kt_loop)) | ((uint32_t)((now / 250U) & 1U) << kt_loop);
    uint32_t changed = bits ^ led_cache;
    for (unsigned key = 0; key < 27; ++key)
        if (changed & (1U << key)) fm1_led_set(key + 14, (bits >> key) & 1U);
    led_cache = bits;
    unsigned pulse = any_run && now - kt_tick_at < 500U;
    bits = ((octave < 0) << OCT_DOWN) | ((octave > 0) << OCT_UP) |
           ((any_done ? fast : pulse || (kt_play_down && !kt_play_long)) << PLAY) |
           ((kt[kt_sel].state != KT_OFF && kt[kt_sel].state != KT_SET) << REC);
    changed = bits ^ button_led_cache;
    for (unsigned key = 0; key < 14; ++key)
        if (changed & (1U << key)) fm1_led_set(key, (bits >> key) & 1U);
    button_led_cache = (uint16_t)bits;
}

/* ---- Drawing (240 x 240) -------------------------------------------------- */
static int kt_dirty(unsigned y, unsigned h, unsigned slot, uint32_t sig)
{
    if (paint_rendering && (y >= paint_top + PAINT_ROWS || y + h <= paint_top)) return 0;
    if (cache->hlv[slot] == sig) return 0;
    cache->hlv[slot] = sig;
    return 1;
}
static void kt_icon(unsigned x, unsigned y, unsigned icon, uint16_t ink)
{
    for (unsigned r = 0; r < 16; ++r) {
        unsigned bits = kt_icons[icon % KT_ICONS][r];
        for (unsigned c = 0; c < 16;) {
            if (!(bits & (0x8000U >> c))) { ++c; continue; }
            unsigned start = c;
            while (c < 16 && (bits & (0x8000U >> c))) ++c;
            paint_fill(x + start * 2U, y + r * 2U, (c - start) * 2U, 2, ink);
        }
    }
}
/* Header hourglass of the focus timer: 8 rows of sand in each bulb. */
static void kt_glass(unsigned x, unsigned y, unsigned top, unsigned low, unsigned grain)
{
    static const uint8_t half[20] = {7, 7, 6, 5, 4, 3, 2, 1, 1, 1, 1, 1, 1, 2, 3, 4, 5, 6, 7, 7};
    unsigned cx = x + 9;
    paint_fill(x, y, 18, 2, TEXT);
    paint_fill(x, y + 22, 18, 2, TEXT);
    for (unsigned r = 0; r < 20; ++r) {
        unsigned w = half[r], ry = y + 2 + r;
        paint_fill(cx - w - 1, ry, 1, 1, TEXT);
        paint_fill(cx + w, ry, 1, 1, TEXT);
        unsigned sand = r < 8 ? r >= 8 - top : r >= 12 ? r >= 20 - low : grain;
        paint_fill(cx - w, ry, w * 2, 1, sand ? TEXT : BACK);
    }
}
static void kt_header(uint32_t now)
{
    int f = kt_focus(now);
    unsigned top = 0, low = 0, grain = 0;
    if (f >= 0) {
        uint32_t left = kt_left((unsigned)f, now), total = kt[f].set_ms > left ? kt[f].set_ms : left;
        top = total ? (unsigned)((left * 8U + total - 1U) / total) : 0U;
        low = total ? (unsigned)(((total - left) * 8U + total / 2U) / total) : 8U;
        grain = now - kt_tick_at < 300U;
    }
    unsigned hold = kt_play_down && !kt_play_long ? (unsigned)clamp((int)(now - kt_play_ms), 0, 600) * 240U / 600U : 0U;
    uint32_t sig = top | (low << 4) | (grain << 8) | (kt_sound << 9) | (kt_tick_level << 11) |
                   ((uint32_t)(octave + 2) << 13) | (hold << 16) | ((uint32_t)(f >= 0) << 25) | ((uint32_t)(kt_loop & 31U) << 26);
    if (!kt_dirty(0, 31, KT_C_HEAD, sig)) return;
    paint_fill(0, 0, 240, 29, BACK);
    text(6, 8, "TIMER TOM", 2, TEXT, BACK);
    text(124, 5, kt_sounds[kt_sound].name, 1, TEXT, BACK);
    static const char *const modes[KT_MODES] = {"SILENT", "TICK 1", "TICK 2", "RHYTHM"};
    char info[16];
    strcpy(info, modes[kt_tick_level]);
    unsigned n = (unsigned)strlen(info);
    if (kt_loop < 27) {
        info[n++] = ' '; info[n++] = 'L';
        info[n++] = (char)('0' + (kt_loop + 1) / 10); info[n++] = (char)('0' + (kt_loop + 1) % 10);
    }
    if (octave) { info[n++] = ' '; info[n++] = octave < 0 ? '-' : '+'; info[n++] = (char)('0' + (octave < 0 ? -octave : octave)); }
    info[n] = 0;
    text(124, 16, info, 1, DIM, BACK);
    if (f >= 0) kt_glass(218, 3, top, low, grain);
    else kt_glass(218, 3, 0, 0, 0);
    /* Hold PLAY: the bar fills while all timers are about to start. */
    paint_fill(0, 29, 240, 2, BACK);
    paint_fill(0, 29, 240, 1, PANEL);
    if (hold) paint_fill(0, 29, hold, 2, TEXT);
}
static void kt_row(unsigned i, uint32_t now)
{
    unsigned y = 32 + i * 41;
    uint32_t left = kt_left(i, now);
    unsigned secs = kt_seconds(left), state = kt[i].state, sel = i == kt_sel;
    unsigned blink = state == KT_DONE && ((now / 400U) & 1U);
    uint32_t total = kt[i].set_ms > left ? kt[i].set_ms : left;
    unsigned bar = total ? (unsigned)(left * 128U / total) : 0U;
    uint32_t meta = state | (kt[i].icon << 3) | (sel << 7) | (blink << 8) | (bar << 9);
    uint32_t sig = signature(meta, &kt[i].set_ms, sizeof kt[i].set_ms);
    sig = signature(sig, &secs, sizeof secs);
    if (!kt_dirty(y, 40, KT_C_ROW + i, sig)) return;
    unsigned inverse = sel ^ blink;
    uint16_t ink = inverse ? BACK : TEXT, ground = inverse ? TEXT : BACK;
    paint_fill(0, y, 240, 40, ground);
    kt_icon(4, y + 4, kt[i].icon, ink);
    char value[12];
    if (state == KT_OFF) strcpy(value, "--:--");
    else kt_time(value, secs);
    text(42, y + 5, value, 3, ink, ground);
    static const char *const states[5] = {"OFF", "SET", "RUN", "PAUSE", "READY!"};
    char line[12] = {'K', (char)('1' + i), ' ', 0};
    strcat(line, states[state]);
    text(174, y + 3, line, 1, ink, ground);
    text(174, y + 14, kt_icon_names[kt[i].icon % KT_ICONS], 1, ink, ground);
    if (kt[i].set_ms && state != KT_SET) {
        value[0] = '/';
        kt_time(value + 1, kt_seconds(kt[i].set_ms));
        text(174, y + 25, value, 1, inverse ? PANEL : DIM, ground);
    }
    /* Sand left: a bar that empties while the timer runs. */
    paint_fill(42, y + 35, 128, 1, inverse ? DIM : PANEL);
    if (state != KT_OFF && bar) paint_fill(42, y + 33, bar, 3, ink);
}
static void kt_line(unsigned n, const char *s, unsigned inverse)
{
    unsigned y = 201 + n * 13;
    uint32_t sig = signature(inverse | (n << 1), s, (unsigned)strlen(s));
    if (!kt_dirty(y - 2, 12, KT_C_LINE + n, sig)) return;
    paint_fill(0, y - 2, 240, 12, inverse ? TEXT : BACK);
    text(4, y, s, 1, inverse ? BACK : n ? DIM : TEXT, inverse ? TEXT : BACK);
}
static void dp_draw(const struct groove_view v[8])
{
    (void)v;
    uint32_t now = frame_time;
    kt_header(now);
    for (unsigned i = 0; i < KT_TIMERS; ++i) kt_row(i, now);
    if (!(paint_rendering && paint_top + PAINT_ROWS <= 196)) {
        if (kt_dirty(196, 2, KT_C_COUNT, 0x5a5a5a5aU)) { paint_fill(0, 196, 240, 1, PANEL); paint_fill(0, 197, 240, 1, BACK); }
    }
    kt_line(0, "K1-K4 TIME  PRESET ICON  SELECT SOUND", 0);
    char hint[40];
    unsigned i = kt_sel;
    switch (kt[i].state) {
    case KT_OFF: strcpy(hint, "TURN K"); hint[6] = (char)('1' + i); strcpy(hint + 7, " TO SET A TIME"); break;
    case KT_RUN: strcpy(hint, "PLAY = PAUSE   HOLD PLAY = ALL"); break;
    case KT_DONE: strcpy(hint, "PLAY OR K"); hint[9] = (char)('1' + i); strcpy(hint + 10, " = ALARM OFF"); break;
    default: strcpy(hint, "PLAY = START   HOLD PLAY = ALL"); break;
    }
    kt_line(1, hint, 0);
    kt_line(2, notice ? notice : "REC RESET  ALG RHYTHM  KEY=LOOP ON/OFF", notice != 0);
}

/* ---- Drum Peter hooks that screen.c still calls ---------------------------- */
static unsigned dp_phase(const struct groove_view *v) { (void)v; return 0; }
static const char *dp_song_name(unsigned song) { (void)song; return 0; }
static int dp_song_load(unsigned song) { (void)song; return GROOVE_PROJECT_EMPTY; }
static void dp_menu_open(void) { page = DRUM; }
static void dp_menu_controls(const struct fm1_controls *c, uint32_t pressed, uint32_t key_down, uint32_t now)
{
    (void)c; (void)pressed; (void)key_down; (void)now;
    page = music_page = DRUM;
}
static void dp_menu_draw(void) {}
static int dp_browser_gesture(const struct fm1_controls *c, uint32_t pressed, uint32_t key_down)
{
    (void)c; (void)pressed; (void)key_down;
    dp_delete = 0;
    return 0;
}
static void dp_delete_draw(void) {}
static void dp_controls(const struct fm1_controls *c, uint32_t pressed, uint32_t now) { kt_controls(c, pressed, now); }
