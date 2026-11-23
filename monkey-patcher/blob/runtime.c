/* The blob is linked at address 0 as a position independent executable
 * and appended flat to the patcher's payload section. The patcher knows
 * where that section is within the image, but not where Windows loads the
 * DLL, so the blob relocates itself: the payload calls blob_init() ahead
 * of every export, see emit_blob_call(). Its first call adds the blob's
 * runtime address to each R_386_RELATIVE slot, the only relocation type
 * blob.py accepts.
 */
#include "blob.h"

#include <stddef.h>

#pragma GCC visibility push(hidden)

/* The patcher finds this at offset 0 and stores the blob's RVA after the
 * magic, see payload_build().
 */
typedef struct {
    char magic[8];
    uint32_t rva;
} blob_header_t;

__attribute__((section(".blob_header"), used))
blob_header_t blob_header = { { 'I', 'G', 'W', 'B', 'L', 'O', 'B', '1' }, 0 };

/* Elf32_Rel; link.ld brackets the dynamic relocations with these. */
typedef struct {
    uint32_t offset;
    uint32_t info;
} rel_t;

extern const rel_t __rel_start[];
extern const rel_t __rel_end[];
extern char __blob_start[];

uintptr_t blob_image_base;

/* blob.py looks this up by name; nothing in the blob calls it. */
__attribute__((used)) void blob_init(void)
{
    if (blob_image_base)
        return;
    /* Hidden symbols are reached relative to the GOT, so the code above
     * needs no relocation itself.
     */
    uintptr_t base = (uintptr_t)__blob_start;
    for (const rel_t *r = __rel_start; r < __rel_end; ++r)
        if ((r->info & 0xff) == 8)
            *(uint32_t *)(base + r->offset) += (uint32_t)base;
    blob_image_base = base - blob_header.rva;
}

#pragma GCC visibility pop
