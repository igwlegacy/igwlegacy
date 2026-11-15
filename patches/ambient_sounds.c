#include "patches.h"

/* CG_EntityEffects selects ET_SPEAKER (7) for persistent map sound loops.
 * Redirect its conditional jump past the trap_S_AddRealLoopingSound call
 * to the constant-light block at RVA 0x176b6. Other entity types still use
 * trap_S_AddLoopingSound for gameplay audio. The relative branch changes
 * no stack state or absolute operands covered by PE relocations.
 */
static const code_rule_t patch_ambient_sounds = {
    CGAME, true, "ambient sounds: skip looping map speakers", 0x1768f, 2,
    { 0x74, 0x18 }, { 0x74, 0x25 }
};

REGISTER_CODE_RULE(patch_ambient_sounds)
