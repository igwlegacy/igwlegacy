#include "patcher.h"

#include <stdio.h>
#include <string.h>

static unsigned char ascii_lower(unsigned char c)
{
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + 'a' - 'A') : c;
}

/* Match the injected dispatcher's comparison when rejecting duplicate keys. */
static bool same_name(const char *a, const char *b)
{
    while (*a && ascii_lower((unsigned char)*a) == ascii_lower((unsigned char)*b)) {
        ++a;
        ++b;
    }
    return ascii_lower((unsigned char)*a) == ascii_lower((unsigned char)*b);
}

bool rules_validate(void)
{
    for (size_t i = 0; i < cvar_rule_count; ++i) {
        const cvar_rule_t *rule = &cvar_rules[i];
        if ((rule->module != GAME && rule->module != CGAME) ||
            !rule->name || !rule->name[0] || !rule->value ||
            strlen(rule->name) > 255 || strlen(rule->value) > 255 ||
            (rule->mode != CVAR_DEFAULT && rule->mode != CVAR_FORCE)) {
            fprintf(stderr, "Invalid CVAR rule %zu\n", i);
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (rule->module == cvar_rules[j].module &&
                same_name(rule->name, cvar_rules[j].name)) {
                fprintf(stderr, "Duplicate CVAR rule: %s\n", rule->name);
                return false;
            }
        }
    }
    for (size_t i = 0; i < code_rule_count; ++i) {
        const code_rule_t *rule = &code_rules[i];
        if ((rule->module != GAME && rule->module != CGAME) ||
            !rule->size || rule->size > PATCH_MAX) {
            fprintf(stderr, "Invalid code rule %zu\n", i);
            return false;
        }
        for (size_t j = 0; rule->enabled && j < i; ++j) {
            const code_rule_t *other = &code_rules[j];
            if (other->enabled && rule->module == other->module &&
                (uint64_t)rule->rva < (uint64_t)other->rva + other->size &&
                (uint64_t)other->rva < (uint64_t)rule->rva + rule->size) {
                fprintf(stderr, "Overlapping code rules: %s / %s\n", rule->name, other->name);
                return false;
            }
        }
    }
    return true;
}
