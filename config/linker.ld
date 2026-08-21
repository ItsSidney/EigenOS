/* linker.ld: 64-bit Higher Half Kernel for Limine */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
    text    PT_LOAD    FLAGS((1 << 0) | (1 << 2)) ; /* Execute + Read */
    rodata  PT_LOAD    FLAGS((1 << 2)) ;            /* Read only */
    data    PT_LOAD    FLAGS((1 << 1) | (1 << 2)) ; /* Write + Read */
}

SECTIONS
{
    . = 0xffffffff80200000;

    .text : {
        *(.text .text.*)
    } :text

    . = ALIGN(4096);

    /* Ring-3 user code: kept in its own pages so they can be marked
       user-accessible (U/S) in the page tables */
    __user_text_start = .;
    .utext : {
        *(.utext .utext.*)
    } :text
    __user_text_end = .;

    . = ALIGN(4096);

    /* Ring-3 user read-only data (string literals etc.) — must also be
       marked user-accessible for ring-3 code to read it */
    __user_data_start = .;
    .urodata : {
        *(.urodata .urodata.*)
    } :rodata
    __user_data_end = .;

    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(4096);

    .requests : {
        KEEP(*(.requests_start_marker))
        KEEP(*(.requests))
        KEEP(*(.requests_end_marker))
    } :rodata

    . = ALIGN(4096);

    .data : {
        *(.data .data.*)
    } :data

    . = ALIGN(4096);

    .bss : {
        *(COMMON)
        *(.bss .bss.*)
    } :data

    . = ALIGN(4096);
    _end = .;
}
