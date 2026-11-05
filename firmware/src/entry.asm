; Diag A entry: Task_MIDI -> trampoline (0xC01309F0) -> here, once per byte.
;
; Hook site 0xC012932C was "LDW.D2T2 *+B14[39],B0" (the ring read index, at the
; end of Task_MIDI's byte loop). It is now "CALLP 0xC01309F0,B3". At that point
; only A10, B14 and B15 are live in Task_MIDI (all callee-saved), and B3 is
; reloaded before Task_MIDI uses it again, so calling C code is safe as long as
; the replaced load runs before we return.

        .global diag_entry
        .global diag_midi_byte
        .sect   ".diagtext"

diag_entry:
        STW     .D2T2   B3,*B15--[2]        ; keep B15 8-byte aligned
        LDBU    .D2T1   *+B14(166),A4       ; the byte just taken (Task_MIDI stores it there)
        NOP     4
        CALLP   .S2     diag_midi_byte,B3
        LDW     .D2T2   *++B15[2],B3
        LDW     .D2T2   *+B14(156),B0       ; the replaced instruction (B14[39])
        NOP     4
        B       .S2     B3
        NOP     5
