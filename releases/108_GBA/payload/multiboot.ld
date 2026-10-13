/* multiboot.ld — GBA multiboot link script: image runs from EWRAM at 0x02000000. */

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
