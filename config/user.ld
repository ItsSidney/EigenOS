/* user.ld — linker script for API81 userland applications.
 * Apps are freestanding ELF executables loaded by the kernel at 0x400000.
 */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

SECTIONS
{
    . = 0x400000;

    .text : {
        *(.text .text.*)
    }

    . = ALIGN(4096);

    .rodata : {
        *(.rodata .rodata.*)
    }

    . = ALIGN(4096);

    .data : {
        *(.data .data.*)
    }

    . = ALIGN(4096);

    .bss : {
        *(COMMON)
        *(.bss .bss.*)
    }

    . = ALIGN(4096);
    _end = .;
}
