/*
 * Clock Slicer for Zoom G1 FOUR: a 16-step gate locked to MIDI clock.
 *
 * Needs the MIDI-clock firmware build that publishes the transport block (research/diag_clock/transport.h
 * in the Zoom G1 Four workspace; layout after MIDISTOMP's). On stock firmware the block is missing and the
 * slicer free-runs at the master tempo.
 *
 * Phase source, checked every block (16 frames):
 *   locked   DAW sent Start and clocks keep coming: position = clocks since Start - 1 (the first clock
 *            after Start is the downbeat) + samples since that clock. Restarts on every Start, so the
 *            pattern lands on the DAW's bar.
 *   grid     clocks come without Start: position = every clock ever received (beat-locked, not bar-locked)
 *   free     no clock for 0.5 s: runs on from where it was, at the master tempo (or the last clock tempo)
 * Between clocks the position advances at the clock tempo; each new clock re-anchors it, so it can't
 * drift. A clock that comes late never steps the position back (it runs at half speed until the clock
 * catches up),
 * so a step boundary is never crossed twice. After Stop with the clock still running, the grid keeps
 * the last Start's bar. Clock arrival is seen at block resolution (16 samples = 0.36 ms).
 *
 * Knobs (the stock Chorus shell's four, renamed by the build; the pedal shows each value + 1):
 *   Hits   (Depth slot, get_param 2)  0..15 -> 1..16 open steps of 16, spread evenly (Euclidean)
 *   Step   (Rate slot,  get_param 3)  0..5  -> 1/4, 1/8, 1/16, 1/32, 1/8 triplet, 1/16 triplet
 *   Gate   (Tone slot,  get_param 4)  0..10 -> each open step is open for 10 % .. 100 % of its length
 *   Depth  (Mix slot,   coeff P[29])  0..100 -> how far closed steps go down (100 = silence)
 * Gain changes are smoothed with a ~1 ms one-pole, so the chops don't click.
 *
 * Safety rules as in tapechorus.c / dualshift.c: init-ran check, every input range-checked, state in the
 * delay arena behind a magic, every output clamped; no globals, tables, switch, division, RTS or double.
 */

typedef unsigned int u32;
#ifdef HOST_TEST
#include <stdint.h>
typedef uintptr_t uw;
#else
typedef u32 uw;
#endif

#define ARENA_MAGIC  0x534C4943u      /* "SLIC" */
#define ARENA_VER    2u
#define MIN_ARENA    4096u
#define MAX_ARENA    0x08000000u

#ifdef HOST_TEST
extern volatile u32 *g_zmt;           /* the host test provides the block and the master tempo */
extern volatile int *g_master_tempo;
#define ZMT          g_zmt
#define MASTER_TEMPO (*g_master_tempo)
#else
#define ZMT          ((volatile u32 *)0x1181FF00u)
#define MASTER_TEMPO (*(volatile int *)0x11812F78u)
#endif
#define ZMT_MAGIC    0x5A4D5401u
#define ZMT_G1_TAG   0x47310001u

#define FRESH_SAMPLES 22050u          /* 0.5 s without a clock = stopped */

typedef struct {
    u32   magic, version;
    u32   last_total;                 /* block word 8 seen last */
    u32   since;                      /* samples since that clock (saturates) */
    float freepos;                    /* position in clocks, 0 .. period, used when no clock */
    float g;                          /* smoothed gain */
    u32   seen;                       /* a clock was seen since this instance started */
    u32   offset;                     /* (clocks since Start - 1) - total clocks, from the last Start */
} Arena;

static inline float clamp01(float x) { return (x >= 0.0f) ? (x > 1.0f ? 1.0f : x) : 0.0f; }
static inline float scrub(float x)   { return (x > -8.0f && x < 8.0f) ? x : 0.0f; }
static inline int   init_ok(const float *P) { return P[5] == 1.0f && P[26] == 1.0f && P[28] == 1.0f; }

/* x mod m for m > 0, shift-subtract: no hardware divide, no RTS */
static u32 umod(u32 x, u32 m)
{
    u32 r = 0;
    int b;
    for (b = 31; b >= 0; b--) {
        r = (r << 1) | ((x >> b) & 1u);
        if (r >= m) r -= m;
    }
    return r;
}

/* the transport words we use; 0 if the block is missing or mid-update twice in a row */
typedef struct { u32 ok, run, clocks, total, tempo100; } Zmt;

static void zmt_read(Zmt *z)
{
    int tries;
    z->ok = 0;
    if (ZMT[0] != ZMT_MAGIC || ZMT[9] != ZMT_G1_TAG) return;
    for (tries = 0; tries < 4; tries++) {
        u32 s = ZMT[1];
        if (s & 1u) continue;
        z->run = ZMT[2]; z->clocks = ZMT[4]; z->tempo100 = ZMT[5]; z->total = ZMT[8];
        if (ZMT[1] == s) { z->ok = 1; return; }
    }
}

#pragma CODE_SECTION(Fx_MOD_Chorus, ".text")
void Fx_MOD_Chorus(uw *S, uw *ctx)
{
    float *P    = (float *)S[1];
    uw    *dl   = (uw *)S[3];
    float *buf  = (float *)ctx[1];
    u32   *psrc = (u32 *)ctx[8];
    u32   *pdst = *(u32 **)ctx[7];
    int    i;

    for (i = 0; i < 16; i++) *pdst = psrc[i];                       /* sample shuttle (as stock) */

    {
        float fade = P[0];
        if (!init_ok(P) || !(fade >= 0.0f && fade <= 1.0f)) { for (i = 0; i < 32; i++) buf[i] = 0.0f; return; }
        if (fade < 0.001f) return;                                   /* bypassed */
    }
    {
        uw base = dl[0], end = dl[1], span = dl[2], bytes = end - base;
        typedef int (*GetParamFn)(uw obj, int idx);
        GetParamFn gp = (GetParamFn)ctx[40];
        (void)gp;
        Arena *a;
        Zmt z;
        int hits, stepk, gatek;
        u32 cps, period;
        float inv_cps, duty, depth, fade, bpm100, cpsamp, rate, pos, g, floor_g;

        if (base == 0 || end <= base || ((base | end | span) & 3u) != 0u ||
            bytes < MIN_ARENA || span < bytes || bytes > MAX_ARENA)
            return;                                                  /* bus untouched */
#ifndef HOST_TEST
        if (gp == 0 || S[0] > 4u) return;                            /* S[0] is the slot number 0..4 */
#endif
        a = (Arena *)base;
        if (a->magic != ARENA_MAGIC || a->version != ARENA_VER) {
            a->magic = ARENA_MAGIC; a->version = ARENA_VER;
            a->last_total = 0; a->since = FRESH_SAMPLES; a->freepos = 0.0f; a->g = 1.0f; a->seen = 0; a->offset = 0;
        }

        /* ---- knobs ---- */
#ifdef HOST_TEST
        hits = (int)S[4]; stepk = (int)S[5]; gatek = (int)S[6];
#else
        hits = gp(S[0], 2); stepk = gp(S[0], 3); gatek = gp(S[0], 4);
#endif
        if (hits < 0 || hits > 15) hits = 7;
        hits += 1;                                                   /* 1..16 open steps */
        if (stepk < 0 || stepk > 5) stepk = 2;
        if (gatek < 0 || gatek > 10) gatek = 5;
        /* clocks per step: 1/4 24, 1/8 12, 1/16 6, 1/32 3, 1/8T 8, 1/16T 4 */
        cps     = stepk == 0 ? 24u : stepk == 1 ? 12u : stepk == 2 ? 6u : stepk == 3 ? 3u : stepk == 4 ? 8u : 4u;
        inv_cps = stepk == 0 ? (1.0f / 24.0f) : stepk == 1 ? (1.0f / 12.0f) : stepk == 2 ? (1.0f / 6.0f) :
                  stepk == 3 ? (1.0f / 3.0f) : stepk == 4 ? 0.125f : 0.25f;
        period  = cps << 4;                                          /* 16 steps */
        duty    = (float)(gatek + 1) * (1.0f / 11.0f);
        fade    = P[0];
        depth   = clamp01(P[29]) * clamp01(P[3]) * fade;             /* P[3]: host's wet gate on knob moves */
        floor_g = 1.0f - depth;

        /* ---- transport ---- */
        zmt_read(&z);
        if (z.ok) {
            if (z.total != a->last_total) { a->last_total = z.total; a->since = 0; a->seen = 1; }
            else if (a->since < FRESH_SAMPLES) a->since += 16u;
        } else {
            a->since = FRESH_SAMPLES;
        }
        bpm100 = (z.ok && z.tempo100 >= 4000u && z.tempo100 <= 25000u) ? (float)(int)z.tempo100 : 0.0f;
        if (bpm100 == 0.0f) {
            int t = MASTER_TEMPO;
            bpm100 = (t >= 40 && t <= 250) ? (float)(t * 100) : 12000.0f;
        }
        cpsamp = bpm100 * (24.0f / (60.0f * 44100.0f * 100.0f));     /* clocks per sample */
        rate = cpsamp;

        /* position in clocks within the pattern period */
        {
            float per = (float)(int)period, prev = a->freepos;
            if (!(prev >= 0.0f && prev < per)) prev = 0.0f;
            pos = prev;                                              /* free: run on from here */
            if (z.ok && a->seen && a->since < FRESH_SAMPLES) {
                u32 anchor;
                float target, delta;
                if (z.run && z.clocks == 0u) {                       /* Start, first clock not here yet */
                    pos = 0.0f;
                    rate = 0.0f;
                } else {
                    if (z.run) {                                     /* locked to the DAW's bar */
                        anchor = z.clocks - 1u;
                        a->offset = anchor - z.total;                /* keeps the bar if Stop leaves the clock on */
                    } else {                                         /* clock without Start */
                        anchor = z.total + a->offset;
                    }
                    target = (float)(int)umod(anchor, period) + (float)(int)a->since * cpsamp;
                    while (target >= per) target -= per;
                    delta = target - prev;                           /* wrapped to -per/2 .. per/2 */
                    if (delta > 0.5f * per) delta -= per;
                    if (delta < -0.5f * per) delta += per;
                    /* a late clock must not step the gate back (no double chop): run at half speed
                     * until it catches up (a 2 ms lead is gone 4 ms later) */
                    if (delta < 0.0f && delta > -1.5f) { pos = prev; rate = 0.5f * cpsamp; }
                    else pos = target;
                }
            }
        }

        /* ---- 16 frames ---- */
        g = a->g;
        if (!(g >= 0.0f && g <= 1.0f)) g = 1.0f;
        for (i = 0; i < 16; i++) {
            float sf = pos * inv_cps, f, target;
            int   step = (int)sf, on;
            f = sf - (float)step;
            step &= 15;
            on = ((step * hits) & 15) < hits;                        /* Euclidean spread */
            target = (on && f < duty) ? 1.0f : floor_g;
            g += (target - g) * 0.0225f;                             /* ~1 ms */
            buf[i]      = scrub(buf[i] * g);
            buf[16 + i] = scrub(buf[16 + i] * g);
            pos += rate;
            if (pos >= (float)(int)period) pos -= (float)(int)period;
        }
        a->g = g;
        a->freepos = pos;
    }
}
