#include "patches.h"

/* Walking bobs the eye height up and down. */
static const cvar_rule_t patch_bob_up =
    { CGAME, "cg_bobup",    "0", CVAR_FORCE };

REGISTER_CVAR_RULE(patch_bob_up)
