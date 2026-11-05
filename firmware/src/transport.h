/* MIDI transport block for G1 FOUR custom effects (firmware side: clock.c, reader: effects).
 *
 * Fixed address 0x1181FF00 in L2 SRAM, 16 words, loaded at boot by the AIS image. Words 0-7 follow
 * MIDISTOMP's layout (references/midistomo/docs/dev/transport-block.md) so effect logic carries over;
 * words 8-9 are G1 additions. Differences from MIDISTOMP are marked (G1).
 *
 *   0  magic 0x5A4D5401 ("ZMT", version 1). Anything else: no transport (stock firmware)
 *   1  sequence: +1 before and +1 after each update (odd = mid-write)
 *   2  running: 1 after Start (0xFA), 0 after Stop (0xFC).
 *      (G1) not cleared when the clock just stops without a Stop: readers treat "no new clock for
 *      0.5 s" as stopped themselves (this firmware has no periodic task)
 *   3  start counter: +1 on every Start
 *   4  clocks since the last Start, counted only while running; the first clock after Start (1) is
 *      the downbeat, so the position in 24ths of a quarter note is value - 1
 *   5  tempo x 100 measured from the clock (updated every beat while it runs).
 *      (G1) 0 until the first measurement: then use the master tempo (int BPM at 0x11812F78) x 100
 *   6  0 (MIDISTOMP: footswitch down; not implemented)
 *   7  0 (MIDISTOMP: hold claim; not implemented)
 *   8  (G1) every MIDI clock ever received, running or not (a free beat grid when the DAW sends
 *      clock without Start)
 *   9  (G1) 0x47310001: G1 block layout version 1
 *
 * Continue (0xFB) and Song Position are ignored, like MIDISTOMP.
 * Updates are written with interrupts off; the pedal has one CPU core, so an effect never sees half an
 * update. Readers still check the sequence word.
 */
#ifndef TRANSPORT_H
#define TRANSPORT_H

#define ZMT_ADDR      0x1181FF00u
#define ZMT_MAGIC     0x5A4D5401u
#define ZMT_G1_TAG    0x47310001u
#define ZMT_MASTER_TEMPO_ADDR 0x11812F78u

#define ZMT_MAGIC_W   0
#define ZMT_SEQ       1
#define ZMT_RUN       2
#define ZMT_STARTS    3
#define ZMT_CLOCKS    4
#define ZMT_TEMPO100  5
#define ZMT_TOTAL     8
#define ZMT_TAG       9
#define ZMT_WORDS     16

#endif
