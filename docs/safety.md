# Safety

Custom effects are unsupported by Zoom and can hang or brick a pedal. The rules below come from other
people's hard-won experience (mainly [stomphacks' SAFETY.md](https://github.com/thammer/stomphacks/blob/main/SAFETY.md),
on MS-series pedals) and from our own crashes on a G1 FOUR. We never bricked the G1 FOUR, but we crashed it
many times while bisecting; every crash was recoverable because we followed rule 1.

## Rules

1. **Autosave OFF.** Then a crashing effect only lives in the current, unsaved patch; a power-cycle brings back
   the last saved patch. The brick path is a crashing effect inside a **saved** patch (the pedal loads it on
   boot, crashes, and does so on every boot).
2. **Saved patches contain only official Zoom effects.** Experiment in an unsaved patch.
3. **Back up first** (effects, patches, `FLST_SEQ.ZT2`; `zoomzt2.py` can download them).
4. **Never upload a file you haven't parsed back.** A malformed container hung two pedals in the stomphacks
   project (invalid header constant; a power-cycle during the hung transfer then left them unable to boot).
   Our build prints the parsed ID/name/group/DSP load; the installer reads the ID/version from the file.
5. **If a file transfer hangs, do NOT power-cycle.** Keep the pedal powered and close the file session
   (the two SysEx messages `52 00 6e 60 21 40 00 00 00 00` then `52 00 6e 60 09`; `install_zd2.py` sends them
   on a stall). A reboot mid-transfer can leave the filesystem half written.
6. **Only the live effect list.** Download `FLST_SEQ.ZT2` from the pedal before changing it; never write back a
   stale copy (it would re-add entries for files you deleted).
7. **One change at a time**, and bisect on hardware when something crashes: stock bytes → repack → patched
   passthrough → real DSP.
8. **Uninstall experiments when done.** The browser live-previews every installed effect, so an installed
   effect is *running code* even if no patch uses it.
9. **Make your kernel fail closed**: see [findings.md §3](findings.md) (guard, range checks, clamp, masked
   indices, state in the arena).

## SysEx you must never send

(from stomphacks / zoom-explorer, MS Plus series; assume the same on G-series)

| Message | Effect |
|---|---|
| `5B` | factory reset — wipes user patches |
| `64 20 00 64 09` with values > `0A` | slot move out of range — freezes the pedal |
| `64 05` | effect cycling — crashes the pedal unless the current patch is empty |

## Recovering from a crash

* **Loud high-pitched tone / frozen screen after loading an effect**: power-cycle. If autosave was off the
  pedal boots to its last saved patch. Then remove the effect with the installer.
* **USB-MIDI shows the pedal but it answers nothing** (not even a MIDI identity request): same thing, it is
  frozen. Power-cycle (only when no transfer was running, rule 5).
* **Installer reports a stall**: do nothing drastic; re-run once the pedal answers again. The installer's own clean-close step already ran.
