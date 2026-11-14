#include "patches.h"

/* Walking bobs the pitch of the first-person view. */
static const cvar_rule_t patch_bob_pitch =
    { CGAME, "cg_bobpitch", "0", CVAR_FORCE };
/* Walking bobs the roll of the first-person view. */
static const cvar_rule_t patch_bob_roll =
    { CGAME, "cg_bobroll",  "0", CVAR_FORCE };
/* Walking bobs the eye height up and down. */
static const cvar_rule_t patch_bob_up =
    { CGAME, "cg_bobup",    "0", CVAR_FORCE };

REGISTER_CVAR_RULE(patch_bob_pitch)
REGISTER_CVAR_RULE(patch_bob_roll)
REGISTER_CVAR_RULE(patch_bob_up)

/* Forward and backward speed pitch the first-person view. */
static const cvar_rule_t patch_run_pitch =
    { CGAME, "cg_runpitch", "0", CVAR_FORCE };
/* Sideways speed rolls the first-person view. */
static const cvar_rule_t patch_run_roll =
    { CGAME, "cg_runroll",  "0", CVAR_FORCE };

REGISTER_CVAR_RULE(patch_run_pitch)
REGISTER_CVAR_RULE(patch_run_roll)
