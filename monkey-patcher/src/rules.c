#include "patches.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const cvar_rule_t *const *cvar_rules;
size_t cvar_rule_count;
const code_rule_t *const *code_rules;
size_t code_rule_count;
const payload_feature_t *const *payload_features;
size_t payload_feature_count;

static rule_set_t *rule_sets;

void rules_register(rule_set_t *rules)
{
    /* Constructor order varies by toolchain. Sort by registration name so
     * the generated CVAR payload layout stays deterministic.
     */
    rule_set_t **position = &rule_sets;
    while (*position && strcmp((*position)->name, rules->name) < 0)
        position = &(*position)->next;
    rules->next = *position;
    *position = rules;
}

static bool rules_collect(void)
{
    static bool collected;
    if (collected)
        return true;

    size_t cvar_count = 0, code_count = 0, feature_count = 0;
    for (const rule_set_t *set = rule_sets; set; set = set->next) {
        if (set->cvars) {
            if (set->count > SIZE_MAX / sizeof(*cvar_rules) - cvar_count)
                goto too_many;
            cvar_count += set->count;
        } else if (set->code) {
            if (set->count > SIZE_MAX / sizeof(*code_rules) - code_count)
                goto too_many;
            code_count += set->count;
        } else if (set->features) {
            if (set->count > SIZE_MAX / sizeof(*payload_features) - feature_count)
                goto too_many;
            feature_count += set->count;
        }
    }

    /* Empty registries need no allocation. Publish only complete tables so
     * allocation failure cannot leave a partially registered patch set.
     */
    const cvar_rule_t **cvars = cvar_count ? malloc(cvar_count * sizeof(*cvars)) : NULL;
    const code_rule_t **code = code_count ? malloc(code_count * sizeof(*code)) : NULL;
    const payload_feature_t **features = feature_count ? malloc(feature_count * sizeof(*features)) : NULL;
    if ((cvar_count && !cvars) || (code_count && !code) || (feature_count && !features)) {
        free(cvars);
        free(code);
        free(features);
        fputs("Could not allocate patch rules\n", stderr);
        return false;
    }
    cvar_count = code_count = feature_count = 0;
    for (const rule_set_t *set = rule_sets; set; set = set->next) {
        for (size_t i = 0; i < set->count; ++i) {
            if (set->cvars)
                cvars[cvar_count++] = &set->cvars[i];
            else if (set->code)
                code[code_count++] = &set->code[i];
            else if (set->features)
                features[feature_count++] = &set->features[i];
        }
    }
    cvar_rules = cvars;
    cvar_rule_count = cvar_count;
    code_rules = code;
    code_rule_count = code_count;
    payload_features = features;
    payload_feature_count = feature_count;
    collected = true;
    return true;

too_many:
    fputs("Too many patch rules\n", stderr);
    return false;
}

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
    if (!rules_collect())
        return false;
    for (size_t i = 0; i < payload_feature_count; ++i) {
        const payload_feature_t *feature = payload_features[i];
        if ((feature->module != GAME && feature->module != CGAME) ||
            !feature->name || !feature->name[0] || !feature->build) {
            fprintf(stderr, "Invalid payload feature %zu\n", i);
            return false;
        }
    }
    for (size_t i = 0; i < cvar_rule_count; ++i) {
        const cvar_rule_t *rule = cvar_rules[i];
        if ((rule->module != GAME && rule->module != CGAME) ||
            !rule->name || !rule->name[0] || !rule->value ||
            strlen(rule->name) > 255 || strlen(rule->value) > 255 ||
            (rule->mode != CVAR_DEFAULT && rule->mode != CVAR_FORCE)) {
            fprintf(stderr, "Invalid CVAR rule %zu\n", i);
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (rule->module == cvar_rules[j]->module &&
                same_name(rule->name, cvar_rules[j]->name)) {
                fprintf(stderr, "Duplicate CVAR rule: %s\n", rule->name);
                return false;
            }
        }
    }
    for (size_t i = 0; i < code_rule_count; ++i) {
        const code_rule_t *rule = code_rules[i];
        if ((rule->module != GAME && rule->module != CGAME && rule->module != UI &&
             rule->module != ENGINE) ||
            !rule->size || rule->size > PATCH_MAX) {
            fprintf(stderr, "Invalid code rule %zu\n", i);
            return false;
        }
        for (size_t j = 0; rule->enabled && j < i; ++j) {
            const code_rule_t *other = code_rules[j];
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
