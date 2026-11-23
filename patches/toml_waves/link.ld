/* One flat, writable image at address 0: blob.py copies the load segment
 * as is, including the zeroed bss, into the payload section.
 */
PHDRS { blob PT_LOAD FLAGS(7); }

SECTIONS {
  . = 0;
  __blob_start = .;
  .text : { KEEP(*(.blob_header)) *(.text .text.*) } :blob
  .rodata : { *(.rodata .rodata.*) } :blob
  .data : { *(.data .data.*) } :blob
  .got : { *(.got) *(.got.plt) } :blob
  .rel.dyn : { __rel_start = .; *(.rel.dyn) *(.rel.*) __rel_end = .; } :blob
  .bss : { *(.bss .bss.*) *(COMMON) } :blob
  /DISCARD/ : {
    *(.comment) *(.note*) *(.eh_frame*) *(.dynamic) *(.dynsym) *(.dynstr)
    *(.hash) *(.gnu.hash) *(.interp)
  }
}
