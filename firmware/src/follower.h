/* MIDI clock -> tempo follower, shared by the pedal build (clock.c) and the host simulation (sim.c).
 *
 * Feed it one call per MIDI clock (0xF8, 24 per quarter note) with a 1 ms timestamp. It calls
 * fol_apply(bpm) (40..250, whole BPM like the pedal's master tempo) only when the tempo really changed:
 * every apply retunes delays, so jitter must never flip it back and forth.
 *
 *   lock     two beats after the clock starts the tempo is set at once
 *   fast     a one-beat measurement (24-timestamp sums, so jitter is smoothed) more than 3% from the
 *            applied tempo for 12 ticks in a row, all within 3% of each other: apply their average
 *            (follows a jump in about 2 beats)
 *   steady   once per beat: mean of the last 24 timestamps minus the mean of 24 timestamps up to
 *            3 beats earlier (sums cut the 1 ms quantisation and USB jitter ~5x); applied only when
 *            1.0 BPM or more from the applied tempo
 *   gap      no clock for 250 ms: forget everything, the next clock locks again
 *
 * Same idea as MIDISTOMP's clock.S (references/midistomo/asm/midi_clock), rewritten for whole-BPM
 * output and one timestamp ring. No division by the hardware (C674x has none): fol_divu() below.
 */
#ifndef FOLLOWER_H
#define FOLLOWER_H

#define FOL_N        128u           /* timestamp ring, power of two */
#define FOL_GAP_MS   250u
#define FOL_FAST_N   12u
#define FOL_FAST_REL 33u            /* 1/33 = 3% */
#define FOL_HYST10   10u            /* 1.0 BPM: a tempo of exactly x.5 must not flip */
#define FOL_MIN10    400u
#define FOL_MAX10    2500u

typedef struct {
    unsigned int t[FOL_N];          /* tick timestamps (ms) */
    unsigned int i;                 /* index of the newest */
    unsigned int n;                 /* valid timestamps (saturates at FOL_N) */
    unsigned int locked;            /* a tempo was applied since the clock started */
    unsigned int applied10;         /* 10 x the BPM applied last */
    unsigned int run, run_ref, run_sum;
    unsigned int beat;              /* ticks since the last steady check */
    unsigned int applies, ticks;    /* statistics */
} fol_state;

static void fol_apply(unsigned int bpm);       /* provided by the includer: set the master tempo */
static void fol_measured(unsigned int bpm100); /* provided by the includer: precise tempo, every beat */

static unsigned int fol_divu(unsigned int num, unsigned int den)
{
    unsigned int q = 0, r = 0;
    int b;
    if (den == 0)
        return 0xFFFFFFFFu;
    for (b = 31; b >= 0; b--) {
        r = (r << 1) | ((num >> b) & 1u);
        if (r >= den) {
            r -= den;
            q |= 1u << b;
        }
    }
    return q;
}

static unsigned int fol_absdiff(unsigned int a, unsigned int b) { return a > b ? a - b : b - a; }

static unsigned int fol_clamp10(unsigned int v)
{
    return v < FOL_MIN10 ? FOL_MIN10 : v > FOL_MAX10 ? FOL_MAX10 : v;
}

static unsigned int fol_at(const fol_state *s, unsigned int back)
{
    return s->t[(s->i - back) & (FOL_N - 1u)];
}

static void fol_set(fol_state *s, unsigned int bpm10)
{
    unsigned int bpm = fol_divu(fol_clamp10(bpm10) + 5u, 10u);
    s->applied10 = bpm * 10u;
    s->locked = 1;
    s->run = 0;
    s->beat = 0;
    s->applies++;
    fol_apply(bpm);
}

static void fol_reset(fol_state *s)
{
    s->n = 0;
    s->locked = 0;
    s->run = 0;
    s->beat = 0;
}

/* 10 x bpm from the 24 newest timestamps against the 24 that are `sep` ticks older (sep a multiple
 * of 24). ds = 24 x (time of sep intervals) = 24 x (sep/24 beats), so 10 x bpm = 600000 x sep / ds.
 * Needs n >= sep + 24. */
static unsigned int fol_rate100(const fol_state *s, unsigned int sep)   /* 100 x bpm, 4000..25000 */
{
    unsigned int ds = 0, k, v;
    for (k = 0; k < 24u; k++)
        ds += fol_at(s, k) - fol_at(s, k + sep);
    if (ds == 0)
        ds = 1;
    v = fol_divu(6000000u * sep + ds / 2u, ds);      /* sep <= 72: 432e6 fits */
    return v < FOL_MIN10 * 10u ? FOL_MIN10 * 10u : v > FOL_MAX10 * 10u ? FOL_MAX10 * 10u : v;
}

static unsigned int fol_to10(unsigned int v100) { return fol_divu(v100 + 5u, 10u); }

static void fol_tick(fol_state *s, unsigned int now)
{
    unsigned int q10, q100;

    s->ticks++;
    if (s->n) {
        unsigned int last = fol_at(s, 0);
        if ((int)(now - last) < 0)          /* never step back in time */
            now = last;
        else if (now - last > FOL_GAP_MS)
            fol_reset(s);
    }
    s->i = (s->i + 1u) & (FOL_N - 1u);
    s->t[s->i] = now;
    if (s->n < FOL_N)
        s->n++;
    if (s->n < 48u)                         /* two beats of timestamps */
        return;

    q100 = fol_rate100(s, 24u);             /* the last beat, smoothed */
    q10 = fol_to10(q100);

    if (!s->locked) {
        fol_measured(q100);
        fol_set(s, q10);
        return;
    }

    /* fast path */
    if (fol_absdiff(q10, s->applied10) * FOL_FAST_REL > s->applied10) {
        if (s->run == 0 || fol_absdiff(q10, s->run_ref) * FOL_FAST_REL > s->run_ref) {
            s->run = 1;
            s->run_ref = q10;
            s->run_sum = q10;
        } else {
            s->run++;
            s->run_sum += q10;
        }
        if (s->run >= FOL_FAST_N) {
            unsigned int avg = fol_divu(s->run_sum + FOL_FAST_N / 2u, FOL_FAST_N);
            fol_measured(avg * 10u);
            fol_set(s, avg);
            s->n = 1;                       /* measure the new tempo from here */
        }
        return;
    }
    s->run = 0;

    /* steady path, once per beat */
    if (++s->beat < 24u)
        return;
    s->beat = 0;
    {
        unsigned int sep = (s->n >= 96u ? 96u : s->n) - 24u;   /* 24..72 ticks */
        unsigned int l100, l10;
        sep -= sep % 24u;                   /* whole beats */
        l100 = fol_rate100(s, sep);
        l10 = fol_to10(l100);
        fol_measured(l100);
        if (fol_absdiff(l10, s->applied10) >= FOL_HYST10)
            fol_set(s, l10);
    }
}

#endif
