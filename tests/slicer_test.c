/* Host test for src/clockslicer.c: is the gate in phase with a DAW's MIDI clock?
 *   cc -O2 -DHOST_TEST -o build/slicer_test tests/slicer_test.c -lm && build/slicer_test
 *
 * A simulated DAW sends Start, then 24 clocks per beat with USB jitter, at a non-integer tempo; the
 * pedal's audio clock runs 100 ppm off the DAW's. The firmware's transport block is updated at each
 * clock's arrival time; the slicer runs every 16 samples on a constant input. With Hits 1 and Step 1/16
 * the gate opens once per bar, on the downbeat: we measure how far each opening is from the DAW's bar
 * (in ms) for 5 minutes. Then the clock stops (free-run must continue without a jump), and a second
 * Start must land on the new downbeat. Last, a fuzz run with garbage knobs/block must stay finite.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

volatile unsigned int zmt_mem[16];
volatile int master_tempo_mem = 120;
volatile unsigned int *g_zmt = zmt_mem;
volatile int *g_master_tempo = &master_tempo_mem;

#include "../src/clockslicer.c"

static unsigned int rng = 1;
static double urand(void) { rng = rng * 1103515245u + 12345u; return ((rng >> 8) & 0xFFFF) / 65535.0; }

typedef struct {
    uw S[8], ctx[48], dl[3];
    float P[30], buf[32];
    unsigned int src[16], dst, *pdst;
    unsigned char *arena;
} Inst;

static void inst_init(Inst *x, int hits, int step, int gate, float depth)
{
    memset(x, 0, sizeof *x);
    x->arena = calloc(1, 8192);
    x->dl[0] = (uw)x->arena; x->dl[1] = (uw)(x->arena + 8192); x->dl[2] = 8192;
    x->P[0] = 1.0f; x->P[3] = 1.0f; x->P[5] = x->P[26] = x->P[28] = 1.0f; x->P[29] = depth;
    x->S[0] = 0; x->S[1] = (uw)x->P; x->S[3] = (uw)x->dl;
    x->S[4] = (uw)hits; x->S[5] = (uw)step; x->S[6] = (uw)gate;
    x->pdst = &x->dst;
    x->ctx[1] = (uw)x->buf; x->ctx[7] = (uw)&x->pdst; x->ctx[8] = (uw)x->src; x->ctx[40] = 1;
}

static void run_block(Inst *x, float in)
{
    int i;
    for (i = 0; i < 32; i++) x->buf[i] = in;
    Fx_MOD_Chorus(x->S, x->ctx);
}

static void block_reset(void)
{
    memset((void *)zmt_mem, 0, sizeof zmt_mem);
    zmt_mem[0] = ZMT_MAGIC; zmt_mem[9] = ZMT_G1_TAG;
}

/* firmware model */
static void fw_clock(void) { zmt_mem[8]++; if (zmt_mem[2]) zmt_mem[4]++; }
static void fw_start(void) { zmt_mem[3]++; zmt_mem[4] = 0; zmt_mem[2] = 1; }
static void fw_stop(void)  { zmt_mem[2] = 0; }

int main(int argc, char **argv)
{
    const double bpm = argc > 1 ? atof(argv[1]) : 123.37, jitter_ms = argc > 2 ? atof(argv[2]) : 2.0,
                 ppm = argc > 3 ? atof(argv[3]) : 100.0;
    const double fs_pedal = 44100.0 * (1.0 + ppm * 1e-6);   /* pedal samples per DAW second */
    const double clk_s = 60.0 / bpm / 24.0;
    double t = 0.0, next_clock = 0.5, start_at = 0.5, bar0 = 0.5, max_err = 0.0, sum_err = 0.0;
    double stop_at = 300.0, restart_at = 305.0;
    int nclk = 0, n_edges = 0, fails = 0, prev_open = 1, stopped = 0, restarted = 0, k;
    long blk = 0;
    double free_err = 0.0, re_err = 0.0;
    int n_free = 0, n_re = 0;
    Inst x;

    block_reset();
    zmt_mem[5] = (unsigned)(bpm * 100.0 + 0.5);              /* firmware measures it after 2 beats */
    inst_init(&x, 0 /* 1 hit */, 2 /* 1/16 */, 2 /* gate 27 % */, 1.0f);
    fw_start();                                             /* Start just before the first clock */

    for (; t < 320.0; blk++) {
        double t_next = t + 16.0 / fs_pedal;
        /* deliver every clock that arrived before this block runs */
        while (!stopped && next_clock <= t_next) {
            if (restarted == 1) { fw_start(); restarted = 2; }
            fw_clock(); nclk++;
            next_clock = (restarted ? restart_at : start_at) + (restarted ? nclk : nclk) * clk_s
                         + (urand() * 2 - 1) * jitter_ms * 1e-3;
            if (next_clock > stop_at && !restarted) stopped = 1;
        }
        if (stopped && !restarted && t > stop_at) fw_stop();
        if (stopped && !restarted && t >= restart_at - 0.001) {
            stopped = 0; restarted = 1; nclk = 0; next_clock = restart_at; bar0 = restart_at;
        }
        run_block(&x, 1.0f);
        for (k = 0; k < 16; k++) {
            int open = x.buf[k] > 0.5f;
            if (open && !prev_open) {                        /* rising edge: should be a DAW downbeat */
                double te = t + k / fs_pedal, bar = 96.0 * clk_s, nb = floor((te - bar0) / bar + 0.5);
                double err = (te - (bar0 + nb * bar)) * 1000.0;
                int phase = restarted ? 2 : (te < stop_at ? 0 : 1);
                if (phase == 1) {                            /* free-running after the clock stopped */
                    if (fabs(err) > free_err) free_err = fabs(err);
                    n_free++;
                } else if (phase == 2) {
                    if (fabs(err) > re_err) re_err = fabs(err);
                    n_re++;
                } else {
                    if (fabs(err) > max_err) max_err = fabs(err);
                    sum_err += err; n_edges++;
                }
            }
            prev_open = open;
        }
        t = t_next;
    }
    printf("locked: %d bar edges, max |error| %.2f ms, mean %+.2f ms (tempo %.2f, jitter +-%.0f ms, audio clock %+g ppm)\n",
           n_edges, max_err, n_edges ? sum_err / n_edges : 0.0, bpm, jitter_ms, ppm);
    printf("free-run after Stop: %d edges, max |error| %.2f ms\n", n_free, free_err);
    printf("after a new Start: %d edges, max |error| %.2f ms\n", n_re, re_err);
    {   /* one opening per DAW bar: no missing and no double chops */
        double bar = 96.0 * clk_s, lim = jitter_ms + 1.5;
        int want = (int)floor((stop_at - start_at) / bar) + 1, want_re = (int)floor((320.0 - restart_at - 0.1) / bar) + 1;
        if (abs(n_edges - want) > 1 || max_err > lim) { printf("FAIL: locked phase (%d edges, want %d)\n", n_edges, want); fails++; }
        if (n_free < 1 || free_err > lim + 4.0) { printf("FAIL: free-run jumped or drifted\n"); fails++; }
        if (abs(n_re - want_re) > 1 || re_err > lim) { printf("FAIL: restart (%d edges, want %d)\n", n_re, want_re); fails++; }
    }

    /* fuzz: garbage knobs and block words must never produce non-finite or loud output */
    {
        Inst y; long n; float peak = 0.0f; int bad = 0;
        inst_init(&y, 0, 0, 0, 1.0f);
        for (n = 0; n < 200000; n++) {
            int i;
            if ((n & 63) == 0) {
                y.S[4] = (uw)(rng % 40) - 10; y.S[5] = (uw)(rng % 20) - 5; y.S[6] = (uw)(rng % 30) - 10;
                y.P[29] = (float)(urand() * 3 - 1); y.P[3] = (float)(urand() * 3 - 1);
                zmt_mem[2] = rng & 1; zmt_mem[4] = rng; zmt_mem[5] = rng ^ 0x5555; zmt_mem[8] += rng & 3;
                zmt_mem[1] = (rng & 7) == 0 ? 1 : 0;
                master_tempo_mem = (int)(rng % 400) - 50;
                if ((n & 1023) == 0) zmt_mem[0] = rng;       /* sometimes no block at all */
                else zmt_mem[0] = ZMT_MAGIC;
                urand();
            }
            run_block(&y, (float)(urand() * 2 - 1));
            for (i = 0; i < 32; i++) {
                if (!isfinite(y.buf[i])) bad++;
                if (fabsf(y.buf[i]) > peak) peak = fabsf(y.buf[i]);
            }
        }
        printf("fuzz: %d non-finite, peak %.3f\n", bad, peak);
        if (bad || peak > 1.0001f) { printf("FAIL: fuzz\n"); fails++; }
    }
    printf("%s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
