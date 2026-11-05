/* Diag A for the G1 FOUR v2.00 OS: do MIDI clock bytes reach Task_MIDI, and does
 * set_tempo from Task_MIDI change the master tempo (rhythm, looper, effects)?
 *
 *   every 24 MIDI clocks (0xF8)  -> master tempo + 1 BPM (250 wraps to 40)
 *   CC 80 (any channel), value v -> master tempo = 40 + v
 *
 * Called from entry.asm once per byte Task_MIDI takes from its ring. Built
 * without any B14-relative data: B14 is the firmware's data pointer, so
 * everything here is an absolute address. No runtime-library calls.
 */

#define VARS      ((volatile unsigned int *)0x1181F400) /* see link.cmd */
#define V_TICK    VARS[0]   /* clocks since the last +1 */
#define V_PREV1   VARS[1]   /* previous non-realtime byte */
#define V_PREV2   VARS[2]   /* the one before */
#define V_CLOCKS  VARS[3]   /* every 0xF8 seen (debug) */
#define V_BYTES   VARS[4]   /* every byte seen (debug) */

#define TEMPO     (*(volatile int *)0x11812F78)          /* master tempo, 40..250 */
typedef int (*set_tempo_fn)(int bpm);
#define SET_TEMPO ((set_tempo_fn)0xC015E6D8)             /* same path as the TEMPO knob */

void diag_midi_byte(unsigned int b)
{
    V_BYTES = V_BYTES + 1;

    if (b == 0xF8) {
        unsigned int n = V_TICK + 1;
        V_CLOCKS = V_CLOCKS + 1;
        if (n >= 24) {
            int t = TEMPO + 1;
            if (t > 250 || t < 40)
                t = 40;
            V_TICK = 0;
            SET_TEMPO(t);
        } else {
            V_TICK = n;
        }
        return;
    }
    if (b >= 0xF8)          /* other realtime bytes: ignore, keep the CC state */
        return;

    if (b < 0x80 && V_PREV1 == 80 && (V_PREV2 & 0xF0) == 0xB0)
        SET_TEMPO(40 + (int)b);

    V_PREV2 = V_PREV1;
    V_PREV1 = b;
}
