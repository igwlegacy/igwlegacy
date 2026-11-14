#include "patches.h"

/* Walking bobs the pitch of the first-person view. */
const cvar_rule_t patch_bob_pitch =
    { CGAME, "cg_bobpitch", "0", CVAR_FORCE };
