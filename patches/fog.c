#include "patches.h"

/* The renderer registers these CVARs in igwarlord.exe rather than a game DLL.
 * Redirect only their default-string pushes to existing "1" and "0" literals.
 * Keep both HIGHLOW relocation slots and the original CVAR_CHEAT flags, so
 * rebasing, saved values and later CVAR changes retain the engine's behavior.
 */
static const code_rule_t patch_fog[] = {
    { ENGINE, true, "r_forceFog default = 1", 0x7506d, 5,
      { 0x68, B4(0x005cbf2cu) }, { 0x68, B4(0x005cc9dcu) } },
    { ENGINE, true, "r_forceFogDensity default = 0", 0x7508a, 5,
      { 0x68, B4(0x005dcdf4u) }, { 0x68, B4(0x005cbf2cu) } },
};

REGISTER_CODE_RULES(patch_fog)
