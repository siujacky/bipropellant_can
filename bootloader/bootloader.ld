/* bootloader.ld — Linker script for hoverboard CAN bootloader
 * Flash: 0x08000000 – 0x08007FFF  (32 KB)
 * RAM:   0x20000000 – 0x20001FFF  ( 8 KB)
 */

ENTRY(Reset_Handler)

_estack = 0x20002000;   /* top of RAM (8 KB above ORIGIN) */

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 32K
    RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 8K
}

SECTIONS
{
    /* ------------------------------------------------------------------ */
    /* Vector table — must be first in flash                               */
    /* ------------------------------------------------------------------ */
    .isr_vector :
    {
        . = ALIGN(4);
        KEEP(*(.isr_vector))
        . = ALIGN(4);
    } >FLASH

    /* ------------------------------------------------------------------ */
    /* Code + read-only data                                               */
    /* ------------------------------------------------------------------ */
    .text :
    {
        . = ALIGN(4);
        *(.text)
        *(.text*)
        *(.rodata)
        *(.rodata*)
        *(.glue_7)
        *(.glue_7t)
        *(.eh_frame)

        KEEP(*(.init))
        KEEP(*(.fini))

        . = ALIGN(4);
        _etext = .;
    } >FLASH

    /* ------------------------------------------------------------------ */
    /* Initialised data — lives in flash (LMA), copied to RAM (VMA)        */
    /* ------------------------------------------------------------------ */
    .data :
    {
        . = ALIGN(4);
        _sdata = .;         /* VMA start (RAM address) */
        *(.data)
        *(.data*)
        . = ALIGN(4);
        _edata = .;         /* VMA end */
    } >RAM AT> FLASH

    _ldata = LOADADDR(.data);   /* LMA start (flash address of initialiser) */

    /* ------------------------------------------------------------------ */
    /* Zero-initialised data                                               */
    /* ------------------------------------------------------------------ */
    .bss (NOLOAD) :
    {
        . = ALIGN(4);
        _sbss = .;
        *(.bss)
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } >RAM

    /* ------------------------------------------------------------------ */
    /* Heap / stack headroom check                                         */
    /* ------------------------------------------------------------------ */
    ._user_heap_stack :
    {
        . = ALIGN(8);
        PROVIDE(end = .);
        PROVIDE(_end = .);
        . = . + 0x200;      /* 512 B minimum heap  */
        . = . + 0x400;      /* 1 KB  minimum stack */
        . = ALIGN(8);
    } >RAM

    /* ------------------------------------------------------------------ */
    /* Discard C++ unwinding tables (unused in bare-metal bootloader)      */
    /* ------------------------------------------------------------------ */
    /DISCARD/ :
    {
        *(.ARM.exidx)
        *(.ARM.exidx*)
        *(.ARM.extab*)
        *(.gcc_except_table)
        *(.eh_frame)
        *(.eh_frame*)
    }

    /* ------------------------------------------------------------------ */
    /* ARM-specific output sections (kept for linker compatibility)        */
    /* ------------------------------------------------------------------ */
    .ARM :
    {
        __exidx_start = .;
        __exidx_end   = .;
    } >FLASH
}
