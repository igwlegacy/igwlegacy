#include "patcher.h"

/* This is the configuration. Defaults only change registration's fallback;
 * forced values also replace saved values and are reapplied on updates.
 * Names are matched without ASCII case, including registrations outside the
 * main CVAR tables (for example, G_RegisterSkillCvars in igw_skill.c).
 */
const cvar_rule_t cvar_rules[] = {
    { CGAME, "cg_runpitch", "0", CVAR_FORCE },
    { CGAME, "cg_runroll",  "0", CVAR_FORCE },
    { CGAME, "cg_bobpitch", "0", CVAR_FORCE },
    { CGAME, "cg_bobroll",  "0", CVAR_FORCE },
    { CGAME, "cg_bobup",    "0", CVAR_FORCE },

    /* Examples, inactive until uncommented. Patch the module that actually
     * registers the CVAR; an engine-only CVAR cannot be targeted here.
     * { GAME, "g_knockback", "600", CVAR_DEFAULT },
     * { GAME, "g_bodytime", "0", CVAR_FORCE },
     * { CGAME, "cg_fov", "100", CVAR_DEFAULT },
     */
    { GAME, NULL, NULL, CVAR_DEFAULT }, /* Sentinel allows an empty mapping. */
};
const size_t cvar_rule_count = ARRAY_SIZE(cvar_rules) - 1;
