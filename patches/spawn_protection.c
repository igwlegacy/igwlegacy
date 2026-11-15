#include "patches.h"

/* Edit this setting and rebuild: how long, in milliseconds, a freshly
 * spawned player cannot be damaged.
 */
#define SPAWN_PROTECTION_MS 3000

_Static_assert(SPAWN_PROTECTION_MS >= 0,
               "Spawn protection time must not be negative");

/* Turn the integer constant into the CVAR string value. */
#define SPAWN_PROTECTION_STR_(x) #x
#define SPAWN_PROTECTION_STR(x) SPAWN_PROTECTION_STR_(x)

/* The retail gamex86.dll already implements spawn protection, timed by
 * the skill CVAR sk_immortal_time (milliseconds; 3000 in every shipped
 * skill-*.cfg):
 *
 * - ClientSpawn (0x1bf40) sets eFlags 0x800 on the entity and its
 *   playerState and stores level.time at entity offset 0x3e4.
 * - ClientThink (0x18680, read at 0x186b7) clears the flag once that
 *   time plus sk_immortal_time.integer is before level.time.
 * - G_Damage (0x21cd7) returns before knockback and damage for a target
 *   with the flag, unless the damage carries DAMAGE_NO_PROTECTION.
 * - Suicide and team changes clear the flag early, as before.
 *
 * G_RegisterSkillCvars registers the CVAR right after G_InitGame and
 * G_RaiseDifficulty exec the skill configs (0x5eb07). Forcing it here
 * overrides their value, makes it read-only, and resets it before every
 * CVAR update, so the configured time applies on every difficulty.
 */
static const cvar_rule_t patch_spawn_protection =
    { GAME, "sk_immortal_time", SPAWN_PROTECTION_STR(SPAWN_PROTECTION_MS),
      CVAR_FORCE };

REGISTER_CVAR_RULE(patch_spawn_protection)
