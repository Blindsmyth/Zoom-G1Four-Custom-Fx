/*
 * Tape Chorus for Zoom G1 Four  --  wow & flutter modulation after
 * Airwindows ToTape9 ("Flutter" section, MIT licence, (c) airwindows),
 * with a dry/wet crossfade added.
 *
 * Replaces Fx_MOD_Chorus (audio) of the stock Chorus.ZD2 shell (patched in as a byte-level
 * graft, see scripts/build_tape.sh).  Safety rules applied here come from the hardware notes in
 * references/ (stomphacks SAFETY.md + docs/zd2-abi.md, ZoomMultistompZDL SAFE-DSP-RULES.md):
 *
 *   - the pedal LIVE-PREVIEWS every installed effect while you scroll the effect browser, so the
 *     audio function can run before init has run:
 *       * "init has run" is inferred from ROM defaults the stock init copies into the coefficient
 *         table; if absent -> write silence and do nothing else
 *       * every coefficient is range-checked (NaN fails every comparison)
 *       * every written sample is clamped / NaN-scrubbed
 *   - all of OUR state lives in the host delay-line arena (S[3]), behind a magic number, and is
 *     cleared lazily in small chunks; nothing relies on uninitialised memory and nothing indexes
 *     outside a 256-word ring
 *   - no libc/RTS, no division, no switch, no static data, no double
 *
 * Build with -DMONO for the cheap mono version (mono sum -> one delay line, one sweep, one tone
 * chain, identical wet signal in both channels).
 *
 * Signal flow (STAGE >= 2), as ToTape9: each channel has its OWN delay line, its OWN flutter sweep
 * (random speed per cycle, each channel's choice depends on the other's sweep -> stereo movement),
 * linear interpolation.  The stock Chorus Tone chain (high-pass/shelf then low-pass) is applied to
 * the delay-line input per channel.  Dry/wet is a crossfade.
 *
 * Instance layout (array of pointers, argument A4):
 *   S[0]  host parameter object (unused)
 *   S[1]  float coefficient table, filled by the stock edit handlers:
 *           [0]  bypass crossfade (1 = active)      [2] output level
 *           [3]  wet gate (host mutes it ~20ms while Depth changes)
 *           [7..9] tone high-pass/shelf, [10..12] tone low-pass:  y = b0*x + b1*x1 + a1*y1
 *           [15] (int) Rate knob   (9739 .. 545392)
 *           [22] (int) Depth knob  (423 at Depth 0 .. 264 at Depth 100, inverted)
 *           [29] Mix 0..1
 *   S[2]  56 zeroed bytes of state (stock init), unused by us
 *   S[3]  delay-line arena descriptor {base, end, span_bytes}
 * Context (argument B4): ctx[1] = effect bus, 16 frames ch.A then 16 ch.B
 * (floats, in place); ctx[7] -> pointer to a word, ctx[8] -> 16 input words
 * (the "current sample" shuttle the stock effect performs).
 */

typedef unsigned int u32;
#ifdef HOST_TEST            /* 64-bit host build for desktop testing */
#include <stdint.h>
typedef uintptr_t uw;
#else
typedef u32 uw;             /* firmware: pointers and ints are 32-bit words */
#endif

#ifndef STAGE
#define STAGE 3                       /* 1 = fixed short delay + Mix (arena proof), 2 = clamped flutter,
                                         3 = full ToTape9 ranges */
#endif
#define ARENA_MAGIC  0x54415045u      /* "TAPE" */
#define ARENA_VER    2u
#define RING_WORDS   256              /* per channel, power of two */
#define RING_MASK    (RING_WORDS - 1)
#define TOTAL_WORDS  (2 * RING_WORDS)
#ifdef MONO                           /* MONO build: one delay line, one sweep, one tone chain */
#define CLR_WORDS    RING_WORDS
#else
#define CLR_WORDS    TOTAL_WORDS
#endif
#define HDR_WORDS    24
#define CLEAR_STEP   128
#define MIN_ARENA    4096u            /* bytes we insist on (we use 96 + 2048) */
#define MAX_ARENA    0x00800000u

#define TWO_PI   6.28318531f
#define PI_F     3.14159265f

typedef struct {
    u32   magic, version, clr, ready;          /* 0..3   */
    int   w;                                   /* 4      */
    float sweepL, nmL, sweepR, nmR;            /* 5..8   */
    u32   rng;                                 /* 9      */
    float dsm, msm;                            /* 10,11  */
    float hx[2], hy[2], lx[2], ly[2];          /* 12..19 tone-chain state, [0]=L [1]=R */
} Arena;

static inline float fabs_f(float x) { return x < 0.0f ? -x : x; }

/* sine, x in [0, 2pi) */
static inline float fsin(float x)
{
    float y, ay;
    if (x > PI_F) x -= TWO_PI;
    y  = 1.27323954f * x - 0.40528473f * x * fabs_f(x);
    ay = fabs_f(y);
    return 0.225f * (y * ay - y) + y;
}

static inline u32 xorshift(u32 r)
{
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    return r;
}

/* uniform 0.24 .. 0.98 like Tape9's flutA / flutB */
static inline float flut_rand(u32 r)
{
    return 0.24f + (float)(int)(r >> 1) * (0.74f / 2147483648.0f);
}

static inline float clamp01(float x)       /* NaN -> 0 */
{
    return (x >= 0.0f) ? (x > 1.0f ? 1.0f : x) : 0.0f;
}

static inline float scrub(float x)         /* clamp + NaN/Inf scrub */
{
    return (x > -8.0f && x < 8.0f) ? x : 0.0f;
}

/* The stock init copies the ROM coefficient defaults into the live table; words 5, 26 and 28 are
 * 1.0f there and no edit handler ever writes them.  Before init has run they are garbage. */
static inline int init_ok(const float *P)
{
    return P[5] == 1.0f && P[26] == 1.0f && P[28] == 1.0f;
}

static void reset_arena(Arena *a)
{
    int i;
    a->magic = ARENA_MAGIC;  a->version = ARENA_VER;
    a->clr = 0;  a->ready = 0;  a->w = 0;
    a->sweepL = 0.0f;  a->nmL = 0.5f;
    a->sweepR = 2.1f;  a->nmR = 0.7f;
    a->rng = 0x1234567u;
    a->dsm = 0.0f;  a->msm = 0.0f;
    for (i = 0; i < 2; i++) a->hx[i] = a->hy[i] = a->lx[i] = a->ly[i] = 0.0f;
}

/* one tone-chain step (HP then LP) on the delay-line input; returns the filtered sample */
static inline float tone_step(float x, int c, Arena *a,
                              float h0, float h1, float ha, float l0, float l1, float la)
{
    float hy = h0 * x + h1 * a->hx[c] + ha * a->hy[c];
    float xf;
    a->hx[c] = x;  a->hy[c] = hy;
    xf = l0 * hy + l1 * a->lx[c] + la * a->ly[c];
    a->lx[c] = hy; a->ly[c] = xf;
    if (!(xf > -8.0f && xf < 8.0f)) {              /* never let a blown-up filter reach the ring */
        xf = 0.0f;
        a->hx[c] = a->hy[c] = a->lx[c] = a->ly[c] = 0.0f;
    }
    return xf;
}

/* ---- audio ---------------------------------------------------------------- */
#pragma CODE_SECTION(Fx_MOD_Chorus, ".text")
void Fx_MOD_Chorus(uw *S, uw *ctx)
{
    float *P    = (float *)S[1];
    uw    *dl   = (uw *)S[3];
    float *buf  = (float *)ctx[1];
    u32   *psrc = (u32 *)ctx[8];
    u32   *pdst = *(u32 **)ctx[7];
    int    i;

    /* current-sample shuttle: always performed, exactly like the stock code */
    for (i = 0; i < 16; i++)
        *pdst = psrc[i];
#ifdef VARIANT_STUB                 /* bisect build: shuttle only, no DSP */
    return;
#endif

    {   /* ---- guard prologue: fail closed to silence ---- */
        float fade = P[0];
        if (!init_ok(P) || !(fade >= 0.0f && fade <= 1.0f)) {
            for (i = 0; i < 32; i++) buf[i] = 0.0f;
            return;
        }
        if (fade < 0.001f)                       /* bypassed: leave bus alone */
            return;
    }

    {   /* ---- validate the host arena, carve our header + rings out of it ---- */
        uw    base = dl[0], end = dl[1], span = dl[2];
        uw    bytes = end - base;
        Arena *a;
        float *ringL, *ringR;
        float  pd, pr, mixT, gate, lvlE, dmax, wbase, fade, depthT, mixTarget;
        float  h0, h1, ha, l0, l1, la;            /* tone: high-pass stage, then low-pass stage */
        float  sweepL, sweepR, nmL, nmR, dsm, msm;
        u32    rng;
        int    w;

        if (base == 0 || end <= base || ((base | end | span) & 3u) != 0u ||
            bytes < MIN_ARENA || span < bytes || bytes > MAX_ARENA)
            return;                                  /* bus untouched */

        a     = (Arena *)base;
        ringL = (float *)(base + HDR_WORDS * 4);
        ringR = ringL + RING_WORDS;

        if (a->magic != ARENA_MAGIC || a->version != ARENA_VER)
            reset_arena(a);

        if (!a->ready) {                             /* lazy, chunked clear of both rings */
            volatile float *z = (volatile float *)ringL;
            int e = (int)a->clr + CLEAR_STEP;
            int k;
            if (e > CLR_WORDS || a->clr >= (u32)CLR_WORDS) e = CLR_WORDS;
            for (k = (int)a->clr; k < e; k++) z[k] = 0.0f;
            a->clr = (u32)e;
            if (e >= CLR_WORDS) a->ready = 1;
            return;                                  /* bus untouched */
        }

        /* ---- sanitised controls ---- */
        fade = P[0];
        pd   = clamp01((float)(423 - ((int *)P)[22]) * (1.0f / 159.0f));   /* host: 423 (Depth 0) .. 264 (Depth 100) */
#if STAGE >= 3
        /* ToTape9 law: flutDepth = D^6 * 50 samples.  The host's Depth curve already gives p ~ D^2,
         * so D^6 = p^3. */
        pd   = pd * pd * pd;
#else
        pd   = pd * (2.0f - pd);                     /* more resolution at low Depth */
#endif
        pr   = clamp01((float)(((int *)P)[15] - 9739) * (1.0f / 535653.0f));
        mixT = clamp01(P[29]);
        gate = clamp01(P[3]);
        lvlE = P[2];
        if (!(lvlE >= 0.0f && lvlE <= 2.0f)) lvlE = 1.0f;
        lvlE = 1.0f + (lvlE - 1.0f) * fade;

        /* Tone = the stock two-stage chain: y = HP(x) then LP(y); each  y = b0*x + b1*x1 + a1*y1.
         * Stock Tone 0..6 sweeps the LP 250 Hz..10 kHz; 7..10 opens the LP fully and raises the HP
         * stage to a treble shelf (b0 up to 1.75), so b0/b1 may exceed 1 and a1 may be negative. */
        h0 = P[7];  h1 = P[8];  ha = P[9];
        l0 = P[10]; l1 = P[11]; la = P[12];
        if (!(h0 > -2.5f && h0 < 2.5f && h1 > -2.5f && h1 < 2.5f && ha >= 0.0f && ha < 0.9999f &&
              l0 > -1.5f && l0 < 1.5f && l1 > -1.5f && l1 < 1.5f && la > -0.9999f && la < 0.9999f)) {
            h0 = 1.0f; h1 = 0.0f; ha = 0.0f;         /* tone chain off (flat) */
            l0 = 1.0f; l1 = 0.0f; la = 0.0f;
        }

#if STAGE >= 3
        /* ToTape9 law: flutFrequency = 0.02 * E^3 radians/sample (44.1 kHz); depth up to 50 samples.
         * Extreme settings modulate the pitch by up to roughly +-100 % -- that is the original. */
        wbase = 0.02f * pr * pr * pr;
        dmax  = 50.0f;
#else
        /* LFO base rate: ~0.3 Hz (wow) .. ~30 Hz (flutter), 44.1 kHz */
        wbase = (0.3f + 30.0f * pr * pr) * 1.42476e-4f;
        /* keep the worst-case pitch deviation around +-2% at any rate */
        dmax  = 56.0f * (1.0f - pr) * (1.0f - pr) + 5.0f;
#endif
        depthT    = pd;
        mixTarget = mixT * gate * fade;

        /* ---- load + sanitise state ---- */
        w = a->w & RING_MASK;
        sweepL = a->sweepL;  nmL = a->nmL;
        sweepR = a->sweepR;  nmR = a->nmR;
        rng = a->rng ? a->rng : 0x1234567u;
        dsm = a->dsm;  msm = a->msm;
        if (!(sweepL >= 0.0f && sweepL <= TWO_PI)) sweepL = 0.0f;
        if (!(sweepR >= 0.0f && sweepR <= TWO_PI)) sweepR = 2.1f;
        if (!(nmL >= 0.2f && nmL <= 1.0f)) nmL = 0.5f;
        if (!(nmR >= 0.2f && nmR <= 1.0f)) nmR = 0.7f;
        if (!(dsm >= 0.0f && dsm <= 1.0f)) dsm = depthT;
        if (!(msm >= 0.0f && msm <= 1.0f)) msm = mixTarget;
        for (i = 0; i < 2; i++)
            if (!(a->hx[i] > -8.0f && a->hx[i] < 8.0f && a->hy[i] > -8.0f && a->hy[i] < 8.0f &&
                  a->lx[i] > -8.0f && a->lx[i] < 8.0f && a->ly[i] > -8.0f && a->ly[i] < 8.0f))
                a->hx[i] = a->hy[i] = a->lx[i] = a->ly[i] = 0.0f;

        for (i = 0; i < 16; i++) {
            float L = scrub(buf[i]), R = scrub(buf[16 + i]);
            float d, off, fr, yl, yr, wl, wr, m, t, oL, oR;
            int   k, i0, i1;

            dsm += (depthT - dsm) * 0.002f;
            msm += (mixTarget - msm) * 0.004f;
            d = dsm * dmax;

            w = (w - 1) & RING_MASK;
#if STAGE == 1
            ringL[w] = L;                            /* proof stage: no tone chain */
            ringR[w] = R;
            wl = ringL[(w + 40) & RING_MASK];        /* fixed taps ~0.9 ms / ~1.2 ms, no modulation */
            wr = ringR[(w + 52) & RING_MASK];
#else
#ifdef MONO
            ringL[w] = tone_step(0.5f * (L + R), 0, a, h0, h1, ha, l0, l1, la);   /* mono sum, one line */
#else
            ringL[w] = tone_step(L, 0, a, h0, h1, ha, l0, l1, la);   /* per-channel delay lines */
            ringR[w] = tone_step(R, 1, a, h0, h1, ha, l0, l1, la);
#endif

            /* ---- left flutter tap ---- */
            off = d + d * fsin(sweepL);
            sweepL += nmL * wbase;
            if (sweepL > TWO_PI) {
                float fa, fb, sx;
                sweepL -= TWO_PI;
                fa = flut_rand(rng);  rng = xorshift(rng);
                fb = flut_rand(rng);  rng = xorshift(rng);
#ifdef MONO
                t = sweepL + nmL;  if (t > TWO_PI) t -= TWO_PI;   /* no second channel: compare with itself */
#else
                t = sweepR + nmR;  if (t > TWO_PI) t -= TWO_PI;
#endif
                sx = fsin(t);
                nmL = (fabs_f(fa - sx) < fabs_f(fb - sx)) ? fa : fb;
            }
            k = (int)off;  fr = off - (float)k;
            k &= 127;                               /* offset <= 101 samples: always inside the ring */
            i0 = (w + k) & RING_MASK;
            i1 = (i0 + 1) & RING_MASK;
            yl = ringL[i0] * (1.0f - fr) + ringL[i1] * fr;

#ifdef MONO
            yr = yl;                                 /* same wet signal in both channels */
#else
            /* ---- right flutter tap ---- */
            off = d + d * fsin(sweepR);
            sweepR += nmR * wbase;
            if (sweepR > TWO_PI) {
                float fa, fb, sx;
                sweepR -= TWO_PI;
                fa = flut_rand(rng);  rng = xorshift(rng);
                fb = flut_rand(rng);  rng = xorshift(rng);
                t = sweepL + nmL;  if (t > TWO_PI) t -= TWO_PI;
                sx = fsin(t);
                nmR = (fabs_f(fa - sx) < fabs_f(fb - sx)) ? fa : fb;
            }
            k = (int)off;  fr = off - (float)k;
            k &= 127;
            i0 = (w + k) & RING_MASK;
            i1 = (i0 + 1) & RING_MASK;
            yr = ringR[i0] * (1.0f - fr) + ringR[i1] * fr;

#endif

            wl = yl;  wr = yr;
#endif

            /* ---- dry/wet and level, clamped ---- */
            m  = msm;
            oL = (L * (1.0f - m) + wl * m) * lvlE;
            oR = (R * (1.0f - m) + wr * m) * lvlE;
            buf[i]      = scrub(oL);
            buf[16 + i] = scrub(oR);
        }

        a->w = w;
        a->sweepL = sweepL;  a->nmL = nmL;
        a->sweepR = sweepR;  a->nmR = nmR;
        a->rng = rng;
        a->dsm = dsm;  a->msm = msm;
    }
}
