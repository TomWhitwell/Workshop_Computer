@ crt0.s — Minimal GBA multiboot startup + ROM header.
@
@ The first 0xC0 bytes are the GBA header the BIOS requires (it compares the Nintendo logo
@ against its own copy and checks the header checksum, locking up if either is wrong — so
@ these are filled in post-link by gbafix). The image runs from EWRAM (0x02000000).
@
@ ─────────────────────────────────────────────────────────────────────────────────────────
@ THE MULTIBOOT ENTRY AT 0xC0 IS A BRANCH, NOT CODE. THIS IS NOT A STYLE CHOICE.
@
@ GBATEK, "Additional Multiboot Header Entries":
@
@   0C0h 4   RAM Entry Point  (32bit ARM branch opcode, eg. "B rom_start")
@   0C4h 1   Boot mode        (init as 00h - BIOS overwrites this value!)
@   0C5h 1   Slave ID Number  (init as 00h - BIOS overwrites this value!)
@   0C6h 26  Not used
@   0E0h 4   JOYBUS Entry Pt.
@
@ The BIOS WRITES TWO BYTES INTO THE LOADED IMAGE, at 0xC4 and 0xC5. Put real instructions
@ there and the BIOS corrupts them after loading, every single time.
@
@ This bit us, and it is worth the space to record how. The original version put executable
@ code directly at 0xC0, so the SECOND instruction — `ldr r0, =__bss_start` — had its offset
@ field overwritten and loaded garbage into r0. The .bss zeroing loop then ran from that
@ garbage address up to __bss_end.
@
@ It appeared to work for the whole of bring-up purely because the garbage happened to land
@ ABOVE __bss_end, so the loop did nothing at all — leaving .bss un-zeroed, which nothing had
@ yet noticed. When the payload grew from 5 KB to 16 KB the garbage landed BELOW instead, and
@ the loop zeroed the payload's own code on its way up. White screen, before a single pixel,
@ and nothing in the synth or the link had anything to do with it.
@
@ The lesson is this project's usual one wearing a new hat: it was never working, it was
@ getting away with it, and the thing that changed was not the thing that broke.
@
@ Header field map (0x00..0xBF):
@   0x00 : b _start             (cartridge-boot branch; unused by multiboot)
@   0x04 : 156-byte Nintendo logo   (gbafix)
@   0xA0 : 12-byte title
@   0xAC : 4-byte game code
@   0xB0 : 2-byte maker code
@   0xB2 : 0x96 fixed
@   0xB3..0xBC : unit/device/reserved/version (0)
@   0xBD : header checksum          (gbafix)
@   0xBE : 2 reserved bytes (0)

    .section .crt0, "ax"
    .arm
    .global _headerstart
_headerstart:
    b _start                        @ 0x00

    .space 156, 0x00                @ 0x04: Nintendo logo (gbafix fills this)

    .byte 'G','B','A','L','I','N','K',' ',' ',' ',' ',' '   @ 0xA0 title (12)
    .byte 'C','G','B','A'                                   @ 0xAC game code (4)
    .byte '0','0'                                           @ 0xB0 maker code (2)
    .byte 0x96                                              @ 0xB2 fixed
    .byte 0x00                                              @ 0xB3 main unit
    .byte 0x00                                              @ 0xB4 device type
    .space 7, 0x00                                          @ 0xB5 reserved
    .byte 0x00                                              @ 0xBC version
    .byte 0x00                                              @ 0xBD header checksum (gbafix)
    .space 2, 0x00                                          @ 0xBE reserved

    @ ---- 0xC0: MULTIBOOT RAM ENTRY POINT ----
    @ A branch, so the two BIOS-written bytes below land on padding instead of on code.
    b _start                        @ 0xC0
    .byte 0x00                      @ 0xC4 boot mode  — BIOS OVERWRITES THIS
    .byte 0x00                      @ 0xC5 slave ID   — BIOS OVERWRITES THIS
    .space 26, 0x00                 @ 0xC6 not used
    b _start                        @ 0xE0 JOYBUS entry point

    @ ---- 0xE4: the real entry ----
    .global _start
_start:
_start_cart:
    @ ---- BOOT PROOF, in assembly, before the C runtime exists ----
    @ Mode 3 and a green screen as the very first thing the image does. The payload already
    @ draws its title before touching the link, so a blank screen means the image never ran.
    @ This goes one level deeper and separates "the image never ran" from "the image ran and
    @ the C startup died" — exactly the distinction that was missing while the fault above was
    @ being hunted, when both looked like an identical white screen.
    @
    @   still WHITE  -> the image never ran: multiboot, entry point, or header
    @   stays GREEN  -> we got here, and .bss zeroing or main() died
    @   title drawn  -> normal boot (the green flash is overdrawn within a frame)
    mov r0, #0x04000000
    mov r1, #0x0400                 @ BG2 on
    orr r1, r1, #0x0003             @ mode 3
    strh r1, [r0]

    mov r0, #0x06000000             @ VRAM
    ldr r1, =0x03E003E0             @ two green pixels per word
    ldr r2, =(240 * 160 / 2)
1:  str r1, [r0], #4
    subs r2, r2, #1
    bne 1b

    @ IWRAM (system) stack. NOT the conventional 0x03007F00: the BIOS hands our serial IRQ
    @ handler the IRQ stack at 0x03007FA0, and at 0x03007F00 that leaves it only 160 bytes
    @ before it collides with this one. link_pump() calls two levels deeper than a typical
    @ handler, so we move ours down and give the IRQ stack 416 bytes instead. Nothing else
    @ uses IWRAM at all — the whole image is linked for EWRAM — so this costs nothing.
    ldr sp, =0x03007E00

    @ Zero .bss. The section is NOLOAD, so it is NOT part of the multiboot image — whatever
    @ EWRAM happened to contain is still sitting there. C guarantees zero-initialised statics,
    @ so without this any static/global in the payload starts as garbage. Cheap insurance.
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
2:  cmp r0, r1
    strlo r2, [r0], #4
    blo 2b

    ldr r0, =main
    bx  r0                          @ enter main() (ARM)

.pool
