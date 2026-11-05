/* Diag A code lives in L2 SRAM above every OS section (the last one ends at
 * 0x1181A1A0; nothing in code, data or .cinit refers to 0x1181A1A0-0x1183FFFF).
 * Variables at 0x1181F400 (32 B) are loaded as zeros by the AIS image.
 * The two words patched into the stock OS (hook site, trampoline) are encoded
 * by build_hook.py diag_a and checked by disassembling the result. */
-e diag_entry
--retain=diag_entry
MEMORY
{
    DIAG:  o = 0x1181F000, l = 0x00000400
}
SECTIONS
{
    .text:     > DIAG
    .diagtext: > DIAG
}
