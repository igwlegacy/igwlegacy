#include "patches.h"

/* Forward and backward speed pitch the first-person view. */
static const cvar_rule_t patch_run_pitch =
    { CGAME, "cg_runpitch", "0", CVAR_FORCE };

REGISTER_CVAR_RULE(patch_run_pitch)
