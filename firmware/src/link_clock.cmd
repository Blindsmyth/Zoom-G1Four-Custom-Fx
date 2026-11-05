/* Clock follower: code at 0x1181E000, state (fol_state, < 1 KB) at 0x1181F000, both in L2 SRAM
 * above every OS section (the last ends at 0x1181A1A0; nothing in code, data or .cinit refers to
 * 0x1181A1A0-0x1183FFFF; the system stack tops out at 0x118164A4). The state is loaded as zeros. */
-e diag_entry
--retain=diag_entry
MEMORY
{
    CODE:  o = 0x1181E000, l = 0x00001000
}
SECTIONS
{
    .text:     > CODE
    .diagtext: > CODE
}
