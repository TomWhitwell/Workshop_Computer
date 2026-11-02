/* multiboot.ld — GBA multiboot link script: image runs from EWRAM at 0x02000000.
 *
 * The drawing primitives and the control tick run from IWRAM (32-bit, zero wait state). Multiboot
 * can only load into EWRAM, so .iwram is LINKED at 0x03000000 but LOADED right after .data inside
 * the image, and crt0.s copies it across before main(). objcopy -O binary works from load
 * addresses, so the copy rides inside payload.mb with no gap.
 *
 * IWRAM is shared with the stacks at its top (user 0x03007E00, IRQ 0x03007FA0, both growing down);
 * the IWRAM sections grow up from the bottom, so the two never meet. */

OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_headerstart)

MEMORY
{
    EWRAM (rwx) : ORIGIN = 0x02000000, LENGTH = 256K
    IWRAM (rwx) : ORIGIN = 0x03000000, LENGTH = 32K
}

SECTIONS
{
    . = ORIGIN(EWRAM);

    .crt0 : { KEEP(*(.crt0)) } > EWRAM

    .text : {
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > EWRAM

    .data : {
        *(.data .data.*)
        . = ALIGN(4);
    } > EWRAM

    .iwram : {
        __iwram_start = .;
        *(.iwram .iwram.*)
        *(.iwram_rodata .iwram_rodata.*)
        . = ALIGN(4);
        __iwram_end = .;
    } > IWRAM AT > EWRAM
    __iwram_lma = LOADADDR(.iwram);

    .iwram_bss (NOLOAD) : {
        __iwram_bss_start = .;
        *(.iwram_bss .iwram_bss.*)
        . = ALIGN(4);
        __iwram_bss_end = .;
    } > IWRAM

    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } > EWRAM

    /* Multiboot images must be a multiple of 16 bytes; the host also rounds up. */
    . = ALIGN(16);
    __rom_end = .;

    /DISCARD/ : { *(.comment) *(.note.*) *(.ARM.*) }
}
