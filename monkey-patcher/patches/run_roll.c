#include "patches.h"

/* Sideways speed rolls the first-person view. */
const cvar_rule_t patch_run_roll =
    { CGAME, "cg_runroll",  "0", CVAR_FORCE };
