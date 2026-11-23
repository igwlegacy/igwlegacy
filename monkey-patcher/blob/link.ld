/* One flat, writable image at address 0: blob.py takes the load segment's
 * file-backed bytes, which the patcher appends to the payload section. The
 * bss comes last, so that it lies in the section's virtual size beyond its
 * file data, which the loader zero-fills.
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
