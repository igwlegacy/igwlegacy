/* The blob is linked at address 0 as a position independent executable
 * and copied flat into the patcher's payload section. The patcher knows
 * where that section is within the image, but not where Windows loads the
 * DLL, so the blob relocates itself on its first call: it adds its runtime
 * address to each R_386_RELATIVE slot, the only relocation type blob.py
 * accepts.
 */
#include "game.h"

#include <stddef.h>

/* The patcher finds this at offset 0 and stores the blob's RVA after the
 * magic, see patches/toml_waves.c.
 */
typedef struct {
    char magic[8];
    uint32_t rva;
} blob_header_t;

__attribute__((section(".blob_header"), used))
blob_header_t blob_header = { { 'I', 'G', 'W', 'T', 'O', 'M', 'L', '1' }, 0 };

/* Elf32_Rel; link.ld brackets the dynamic relocations with these. */
typedef struct {
    uint32_t offset;
    uint32_t info;
} rel_t;

extern const rel_t __rel_start[] __attribute__((visibility("hidden")));
extern const rel_t __rel_end[] __attribute__((visibility("hidden")));
extern char __blob_start[] __attribute__((visibility("hidden")));

uintptr_t game_base;
static int relocated;

void runtime_init(void)
{
    if (relocated)
        return;
    /* Hidden symbols are reached relative to the GOT, so the code above
     * needs no relocation itself.
     */
    uintptr_t base = (uintptr_t)__blob_start;
    for (const rel_t *r = __rel_start; r < __rel_end; ++r)
        if ((r->info & 0xff) == 8)
            *(uint32_t *)(base + r->offset) += (uint32_t)base;
    game_base = base - blob_header.rva;
    relocated = 1;
}
