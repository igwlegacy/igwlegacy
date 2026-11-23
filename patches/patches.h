#ifndef IGW_PATCHES_H
#define IGW_PATCHES_H

#include "emit.h"

/* Registration records have static storage; startup only links them together.
 * A record supplies CVAR rules, code rules, injected payload features,
 * dropped imports or relocated addresses.
 */
typedef struct rule_set {
    const char *name;
    const cvar_rule_t *cvars;
    const code_rule_t *code;
    const payload_feature_t *features;
    const import_rule_t *imports;
    const address_rule_t *addresses;
    size_t count;
    struct rule_set *next;
} rule_set_t;

void rules_register(rule_set_t *rules);

/* Run registration before main on both MSVC and GCC-compatible compilers. */
#if defined(_MSC_VER)
#pragma section(".CRT$XCU", read)
#define PATCH_CONSTRUCTOR(name) \
    static void __cdecl name(void); \
    __declspec(allocate(".CRT$XCU")) static void (__cdecl *const name##_initializer)(void) = name; \
    static void __cdecl name(void)
#elif defined(__GNUC__) || defined(__clang__)
#define PATCH_CONSTRUCTOR(name) \
    static void name(void) __attribute__((constructor)); \
    static void name(void)
#else
#error "Patch registration needs startup constructor support"
#endif

#define REGISTER_RULE_SET(name, cvars, code, features, imports, addresses, count) \
    static rule_set_t name##_rules = { #name, cvars, code, features, imports, addresses, count, NULL }; \
    PATCH_CONSTRUCTOR(name##_register) \
    { \
        rules_register(&name##_rules); \
    }

/* Scalars and arrays share the same registry, with no external declarations. */
#define REGISTER_CVAR_RULE(rule) REGISTER_RULE_SET(rule, &(rule), NULL, NULL, NULL, NULL, 1)
#define REGISTER_CVAR_RULES(rules) REGISTER_RULE_SET(rules, rules, NULL, NULL, NULL, NULL, ARRAY_SIZE(rules))
#define REGISTER_CODE_RULE(rule) REGISTER_RULE_SET(rule, NULL, &(rule), NULL, NULL, NULL, 1)
#define REGISTER_CODE_RULES(rules) REGISTER_RULE_SET(rules, NULL, rules, NULL, NULL, NULL, ARRAY_SIZE(rules))
#define REGISTER_PAYLOAD_FEATURE(rule) REGISTER_RULE_SET(rule, NULL, NULL, &(rule), NULL, NULL, 1)
#define REGISTER_IMPORT_RULES(rules) REGISTER_RULE_SET(rules, NULL, NULL, NULL, rules, NULL, ARRAY_SIZE(rules))
#define REGISTER_ADDRESS_RULES(rules) REGISTER_RULE_SET(rules, NULL, NULL, NULL, NULL, rules, ARRAY_SIZE(rules))

/* Redirect an x87 operand to the existing zero float. Its position remains
 * covered by the DLL's original HIGHLOW relocation when the image rebases.
 */
#define READ_ZERO(label, where, op0, op1, address) \
    { CGAME, true, label, where, 6, \
      { op0, op1, B4(address) }, { op0, op1, B4(0x300a5344u) } }

#endif
