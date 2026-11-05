/* MIDI clock -> master tempo, and the MIDI transport block, for the G1 FOUR v2.00 OS
 * (docs/midi-clock-port.md, transport.h).
 *
 * Called from entry.asm once per byte Task_MIDI takes from its ring:
 *   0xF8 clock  -> transport counts, then the follower (follower.h, tested on the host by sim.c)
 *   0xFA start  -> running, start counter +1, clock count 0
 *   0xFC stop   -> not running
 * When the follower settles on a new tempo, it is set and the screen refreshed with the same calls the
 * firmware makes when an editor (Guitar Lab) changes the tempo over SysEx, also from Task_MIDI (the
 * parameter dispatcher 0xC0134650: set_tempo at 0xC01349C8, then its common tail 0xC01349EC).
 *
 * No B14-relative data (B14 is the firmware's data pointer) and no runtime-library calls: state and
 * block live at fixed L2 addresses loaded by the AIS image.
 */
#include <c6x.h>
#include "transport.h"

#define STATE      ((fol_state *)0x1181F000)               /* see link_clock.cmd */
#define ZMT        ((volatile unsigned int *)ZMT_ADDR)

#define TEMPO      (*(volatile int *)ZMT_MASTER_TEMPO_ADDR) /* master tempo, 40..250 */
#define GET_TICKS  ((unsigned int (*)(void))0xC016B540)    /* SYS/BIOS Clock_getTicks, 1 ms */
#define SET_TEMPO  ((int (*)(int))0xC015E6D8)              /* store + editor notify + OS notify */
#define AFTER_SET1 ((void (*)(void))0xC016F3F0)            /* dispatcher tail, 1st call */
#define AFTER_SET2 ((void (*)(void))0xC016F3E0)            /* 2nd call */
/* Ask the UI task to redraw: 0x1180AF34(1) = Event_post(B14[139], 1). MainApp calls exactly this
 * (0xC0134C64) after a screen's knob handler reports a change, e.g. the RHYTHM screen's BPM knob.
 * (Builds up to t2.00 called 0xC0156C94 here, which only acts on a pending mode change.) */
#define UI_REDRAW  ((void (*)(int))0xC016F690)

/* every block update: interrupts off, sequence odd while writing */
#define PUB_BEGIN  { unsigned int csr_ = _disable_interrupts(); ZMT[ZMT_SEQ] = ZMT[ZMT_SEQ] + 1u;
#define PUB_END      ZMT[ZMT_SEQ] = ZMT[ZMT_SEQ] + 1u; _restore_interrupts(csr_); }

static void fol_measured(unsigned int bpm100)
{
    PUB_BEGIN
    ZMT[ZMT_TEMPO100] = bpm100;
    PUB_END
}

#include "follower.h"

static void fol_apply(unsigned int bpm)
{
    if ((int)bpm == TEMPO)
        return;
    SET_TEMPO((int)bpm);
    AFTER_SET1();
    AFTER_SET2();
    UI_REDRAW(1);
}

void diag_midi_byte(unsigned int b)
{
    if (b == 0xF8) {
        PUB_BEGIN
        ZMT[ZMT_TOTAL] = ZMT[ZMT_TOTAL] + 1u;
        if (ZMT[ZMT_RUN])
            ZMT[ZMT_CLOCKS] = ZMT[ZMT_CLOCKS] + 1u;
        PUB_END
        fol_tick(STATE, GET_TICKS());
    } else if (b == 0xFA) {
        PUB_BEGIN
        ZMT[ZMT_STARTS] = ZMT[ZMT_STARTS] + 1u;
        ZMT[ZMT_CLOCKS] = 0u;
        ZMT[ZMT_RUN] = 1u;
        PUB_END
    } else if (b == 0xFC) {
        PUB_BEGIN
        ZMT[ZMT_RUN] = 0u;
        PUB_END
    }
}
