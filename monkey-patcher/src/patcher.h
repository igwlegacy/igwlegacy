#ifndef IGW_PATCHER_H
#define IGW_PATCHER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define PATCH_MAX 16
/* Upper bound for one module's injected section, code and data together.
 * Marker features reserve sizable zero-filled caches and file buffers.
 */
#define PAYLOAD_MAX 0x40000

/* ENGINE is the client's igwarlord.exe, SERVER the different igwarlord.exe
 * of the dedicated server, which ships in the server directory.
 */
typedef enum { GAME, CGAME, UI, ENGINE, SERVER } module_t;
typedef enum { CVAR_DEFAULT, CVAR_FORCE } cvar_mode_t;

typedef struct {
    module_t module;
    const char *name;
    const char *value;
    cvar_mode_t mode;
} cvar_rule_t;

/* Expected bytes guard the complete instruction even for a one-byte ret. */
typedef struct {
    module_t module;
    bool enabled;
    const char *name;
    uint32_t rva;
    size_t size;
    unsigned char expected[PATCH_MAX];
    unsigned char replacement[PATCH_MAX];
} code_rule_t;

/* Bytes written over IMPORT_STUB_SIZE int3s: xor eax,eax; ret. */
#define IMPORT_STUB_SIZE 3
/* Upper bound for the functions of one dropped import. */
#define IMPORT_FUNCTIONS_MAX 64

/* Drop an imported DLL from the import directory, so the loader no longer
 * needs it. Every IAT slot of its descriptor is bound on disk to one stub
 * at stub_rva that returns 0 and leaves the arguments to the caller. That
 * only suits cdecl functions whose callers accept 0, NULL, false or no
 * result. New base relocations keep the slots valid if the image is ever
 * rebased.
 */
typedef struct {
    module_t module;
    const char *name;
    const char *dll;
    /* Expected count of imported functions; guards the descriptor. */
    size_t functions;
    /* int3 padding inside an executable section that receives the stub. */
    uint32_t stub_rva;
} import_rule_t;

/* Store an absolute address in a dword that no base relocation covers yet,
 * such as a pointer table's zero terminator, and add a HIGHLOW relocation
 * for it, so the address stays valid if the image is ever rebased. The
 * address must lie inside the image, which includes the payload section.
 */
typedef struct {
    module_t module;
    const char *name;
    uint32_t rva;
    uint32_t expected;
    uint32_t address;
} address_rule_t;

typedef struct {
    module_t module;
    const char *name;
    size_t size;
    uint64_t fingerprint;
    uint32_t timestamp, image_base, dll_entry, syscall_rva;
    uint32_t cvar_register, cvar_update, cvar_set;
} profile_t;

typedef struct {
    unsigned char *data;
    size_t size, pe, optional, sections;
    uint16_t section_count;
    const profile_t *profile;
    /* Permission bits of the input, reused for its patched output. */
    unsigned int mode;
} image_t;

/* A module's C code, built by monkey-patcher/blob/blob.py into a flat,
 * self-relocating image, see the generated <module>_blob.h.
 */
typedef struct {
    const unsigned char *data;
    /* File-backed bytes, then zeroed bytes that take no file space. */
    size_t size, bss;
    /* The offset of blob_init(), which relocates the blob once. */
    uint32_t init;
} blob_t;

/* Upper bound for the calls and jumps of one payload into its blob. */
#define BLOB_FIXUPS_MAX 64

/* A rel32 at payload offset at that targets offset symbol of the blob. */
typedef struct {
    uint32_t at, symbol;
} blob_fixup_t;

typedef struct {
    unsigned char data[PAYLOAD_MAX];
    size_t size;
    uint32_t entry;
    /* The blob the module's features call into, appended after them, and
     * the branches into it that its final position resolves.
     */
    const blob_t *blob;
    blob_fixup_t blob_fixups[BLOB_FIXUPS_MAX];
    size_t blob_fixup_count;
    /* Zeroed bytes after data, the blob's bss, for the section's virtual
     * size only.
     */
    size_t bss;
    /* Set by helpers that cannot report a failure themselves. */
    bool failed;
} payload_t;

/* Features chain ahead of the CVAR dispatcher, sharing its EBP argument
 * frame and EBX section base. Unhandled calls jump to the next dispatcher.
 */
typedef struct {
    module_t module;
    const char *name;
    bool (*build)(payload_t *payload, uint32_t rva, uint32_t next, uint32_t *entry);
} payload_feature_t;

extern const cvar_rule_t *const *cvar_rules;
extern size_t cvar_rule_count;
extern const code_rule_t *const *code_rules;
extern size_t code_rule_count;
extern const payload_feature_t *const *payload_features;
extern size_t payload_feature_count;
extern const import_rule_t *const *import_rules;
extern size_t import_rule_count;
extern const address_rule_t *const *address_rules;
extern size_t address_rule_count;

uint16_t get16(const unsigned char *p);
uint32_t get32(const unsigned char *p);
void put16(unsigned char *p, uint16_t value);
void put32(unsigned char *p, uint32_t value);
bool image_open(image_t *image, const char *path);
bool image_offset(const image_t *image, uint32_t rva, size_t size, size_t *offset);
bool image_patch(image_t *image);
bool payload_build(payload_t *payload, const profile_t *profile, uint32_t rva);
bool rules_validate(void);

#endif
