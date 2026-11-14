#include "patches.h"

/* Walking bobs the roll of the first-person view. */
static const cvar_rule_t patch_bob_roll =
    { CGAME, "cg_bobroll",  "0", CVAR_FORCE };

REGISTER_CVAR_RULE(patch_bob_roll)
