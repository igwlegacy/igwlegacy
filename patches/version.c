#include "patches.h"
#include "version.h"

#include <string.h>

/* The original release string has only 16 bytes including padding. Store
 * the Git label in the final 256 zero-filled, file-backed bytes of .rdata
 * instead. Extend VirtualSize to include that storage explicitly; its raw
 * size, section alignment and the following section's RVA stay unchanged.
 */
static const char release_version[] = "Release " IGW_GIT_VERSION;
_Static_assert(sizeof(release_version) <= 256, "Git release version exceeds .rdata storage");

static const struct {
    module_t module;
    uint32_t rva;
} version_storage[] = {
    { GAME,   0xb2500u },
    { CGAME,  0xbf700u },
    { UI,     0x6a500u },
    { ENGINE, 0x1fcf00u },
};

enum { VERSION_CHUNKS = (sizeof(release_version) + PATCH_MAX - 1) / PATCH_MAX };
static code_rule_t patch_version_storage[ARRAY_SIZE(version_storage) * VERSION_CHUNKS];

/* Split the label, including its terminator, into guarded patch-sized units. */
PATCH_CONSTRUCTOR(configure_version_storage)
{
    for (size_t i = 0; i < ARRAY_SIZE(version_storage); ++i) {
        for (size_t j = 0; j < VERSION_CHUNKS; ++j) {
            size_t offset = j * PATCH_MAX;
            size_t size = sizeof(release_version) - offset;
            if (size > PATCH_MAX)
                size = PATCH_MAX;
            code_rule_t *rule = &patch_version_storage[i * VERSION_CHUNKS + j];
            *rule = (code_rule_t){ version_storage[i].module, true,
                                  "git version: label storage",
                                  version_storage[i].rva + (uint32_t)offset,
                                  size, { 0 }, { 0 } };
            memcpy(rule->replacement, release_version + offset, size);
        }
    }
}

/* Replace complete instructions, leaving absolute operands in their existing
 * HIGHLOW relocation slots so every reference still works after rebasing.
 */
#define VERSION_REF(module, label, where, opcode, before, after) \
    { module, true, "git version: " label, where, 5, \
      { opcode, B4(before) }, { opcode, B4(after) } }

static const code_rule_t patch_version_references[] = {
    { GAME, true, "git version: map server label storage", 0x218, 4,
      { B4(0x18438u) }, { B4(0x18600u) } },
    { CGAME, true, "git version: map client label storage", 0x210, 4,
      { B4(0x1a6d9u) }, { B4(0x1a800u) } },
    { UI, true, "git version: map menu label storage", 0x210, 4,
      { B4(0xc456u) }, { B4(0xc600u) } },
    { ENGINE, true, "git version: map engine label storage", 0x220, 4,
      { B4(0x52e98u) }, { B4(0x53000u) } },

    VERSION_REF(GAME, "server configstring", 0x2bb17, 0xb8, 0x200a2330u, 0x200b2500u),
    VERSION_REF(GAME, "server crash report", 0x629ba, 0x68, 0x200a2330u, 0x200b2500u),
    VERSION_REF(CGAME, "client version check", 0x27ecf, 0xba, 0x300ad99cu, 0x300bf700u),
    VERSION_REF(CGAME, "client mismatch message", 0x27f00, 0x68, 0x300ad99cu, 0x300bf700u),
    VERSION_REF(CGAME, "client crash report", 0x82c0a, 0x68, 0x300ad99cu, 0x300bf700u),

    { UI, true, "git version: main-menu label", 0x1fb02, 10,
      { 0xc7, 0x05, B4(0x101029b0u), B4(0x100628dcu) },
      { 0xc7, 0x05, B4(0x101029b0u), B4(0x1006a500u) } },

    /* The console measures the formatted label before right-aligning it. */
    VERSION_REF(ENGINE, "console right prompt", 0x7442, 0x68, 0x005cbe88u, 0x005fcf00u),
    VERSION_REF(ENGINE, "startup banner", 0x22f10, 0x68, 0x005cbe88u, 0x005fcf00u),
    VERSION_REF(ENGINE, "version cvar", 0x23299, 0x68, 0x005cbe88u, 0x005fcf00u),
    VERSION_REF(ENGINE, "engine crash report", 0x50eea, 0x68, 0x005cbe88u, 0x005fcf00u),
};

REGISTER_CODE_RULES(patch_version_storage)
REGISTER_CODE_RULES(patch_version_references)
