#include "patches.h"

/* Sideways speed rolls the first-person view. */
static const cvar_rule_t patch_run_roll =
    { CGAME, "cg_runroll",  "0", CVAR_FORCE };

REGISTER_CVAR_RULE(patch_run_roll)
