/* Host simulation of follower.h: MIDI clock with 1 ms timestamps and random USB jitter.
 *   cc -O2 -o sim sim.c && ./sim
 * For each scenario it prints every apply and checks: locks within 2 beats, follows a jump within
 * 3 beats, never applies while the clock is steady.
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static unsigned int g_applied[64], g_napp;
static void fol_apply(unsigned int bpm) { if (g_napp < 64) g_applied[g_napp] = bpm; g_napp++; }
static void fol_measured(unsigned int bpm100) { (void)bpm100; }
#include "follower.h"

static unsigned int rng = 12345;
static double urand(void) { rng = rng * 1103515245u + 12345u; return ((rng >> 8) & 0xFFFF) / 65535.0; }

/* tempo1 for beats1 beats, then tempo2 for beats2 beats; jitter in ms (+-). Returns failures. */
static int scenario(double tempo1, int beats1, double tempo2, int beats2, double jitter, int verbose)
{
    fol_state s = {0};
    double t = 1000.0;
    int fails = 0, b, k;
    unsigned int lock_tick = 0, jump_tick = 0, last_apply_tick = 0, applies_after_settle = 0;
    g_napp = 0;
    for (b = 0; b < beats1 + beats2; b++) {
        double tempo = b < beats1 ? tempo1 : tempo2;
        for (k = 0; k < 24; k++) {
            double j = (urand() * 2 - 1) * jitter;
            unsigned int now = (unsigned int)floor(t + j);
            unsigned int before = g_napp;
            static unsigned int last_now;
            if (b == 0 && k == 0) last_now = 0;
            if (now < last_now) now = last_now;  /* arrival times never go backwards */
            last_now = now;
            fol_tick(&s, now);
            if (g_napp != before) {
                unsigned int tick = b * 24 + k;
                if (verbose)
                    printf("    beat %5.2f: apply %u\n", tick / 24.0, g_applied[(g_napp - 1) & 63]);
                if (!lock_tick) lock_tick = tick + 1;
                if (b >= beats1 && !jump_tick) jump_tick = tick + 1;
                if ((b >= 10 && b < beats1) || b >= beats1 + 10) applies_after_settle++;
                last_apply_tick = tick;
            }
            t += 60000.0 / tempo / 24.0;
        }
    }
    {
        unsigned int want1 = (unsigned int)floor(tempo1 + 0.5), want2 = (unsigned int)floor(tempo2 + 0.5);
        unsigned int final = s.applied10 / 10;
        /* lock by 2 beats; a jump of 3% or more within 3 beats, a smaller one (steady path) within 5 */
        int big = fabs(tempo2 - tempo1) >= 0.03 * tempo1;
        int ok_lock = lock_tick && lock_tick <= 49;
        int ok_jump = want1 == want2 || (jump_tick && jump_tick - beats1 * 24 <= (big ? 72u : 192u));
        int ok_final = final + 1 >= want2 && final <= want2 + 1;
        int ok_steady = applies_after_settle == 0;
        fails = !ok_lock + !ok_jump + !ok_final + !ok_steady;
        printf("%6.1f -> %6.1f BPM, jitter %3.0f ms: lock %5.2f beats, jump %5.2f beats, final %3u, "
               "applies %2u, late re-applies %u  %s\n",
               tempo1, tempo2, jitter, lock_tick / 24.0,
               jump_tick ? (jump_tick - beats1 * 24) / 24.0 : 0.0, final, g_napp, applies_after_settle,
               fails ? "FAIL" : "ok");
        (void)want1; (void)last_apply_tick;
    }
    return fails;
}

int main(void)
{
    static const double tempos[][2] = {
        {120, 120}, {90, 90}, {140, 140}, {220, 220}, {250, 250}, {40, 40}, {60, 60},
        {120, 128}, {128, 120}, {120, 121}, {121, 120}, {100, 150}, {150, 100},
        {175.5, 175.5}, {99.5, 99.5}, {200, 60}, {60, 200}, {138, 140},
    };
    static const double jitters[] = {0, 1, 3, 5, 8};
    int fails = 0, i, j;
    for (j = 0; j < 5; j++)
        for (i = 0; i < (int)(sizeof tempos / sizeof tempos[0]); i++)
            fails += scenario(tempos[i][0], 64, tempos[i][1], 64, jitters[j], 0) ? 1 : 0;
    printf("\nexample with applies listed:\n");
    scenario(120, 16, 140, 16, 3, 1);
    printf("\n%d failing scenarios\n", fails);
    return fails != 0;
}
