#include "patches.h"

/* Forward and backward speed pitch the first-person view. */
const cvar_rule_t patch_run_pitch =
    { CGAME, "cg_runpitch", "0", CVAR_FORCE };
