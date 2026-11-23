/* The runtime of a C blob: C code that blob.py builds into a flat, self
 * relocating image, and that the patcher appends to the payload section of
 * one retail module. See monkey-patcher/docs/README.md, "C blobs".
 */
#pragma once

#include <stdint.h>

/* Functions with this attribute are the blob's exports: blob.py lists their
 * offsets in the generated <module>_blob.h, for payload features to call
 * with emit_blob_call(). Everything else stays hidden and is dropped
 * unless an export reaches it.
 */
#define BLOB_EXPORT __attribute__((visibility("default")))

#pragma GCC visibility push(hidden)

/* The runtime address of the module's image base, which the module SDK
 * headers (sdk/) add retail RVAs to.
 */
extern uintptr_t blob_image_base;

#pragma GCC visibility pop
