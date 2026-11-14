#ifndef IGW_PATCHER_H
#define IGW_PATCHER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define PATCH_MAX 16
#define PAYLOAD_MAX 65536

typedef enum { GAME, CGAME, UI } module_t;
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
} image_t;

typedef struct {
    unsigned char data[PAYLOAD_MAX];
    size_t size;
    uint32_t entry;
} payload_t;

extern const cvar_rule_t *const *cvar_rules;
extern size_t cvar_rule_count;
extern const code_rule_t *const *code_rules;
extern size_t code_rule_count;

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
