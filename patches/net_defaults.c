#include "patches.h"

/* Client network defaults, raised to the engine's upper limits.
 *
 * Both CVARs are registered by CL_Init in igwarlord.exe through
 * Cvar_Get(name, default, CVAR_ARCHIVE...), and each default string literal
 * is referenced only by its registration push. Rewriting the literals in
 * place changes the fallback for fresh configurations, while saved archive
 * values and runtime overrides still take precedence.
 *
 * - rate "25000" -> "90000": same length, so it is a plain byte swap.
 * - cl_maxpackets "30" -> "125": the four-byte slot is padded after "30",
 *   so "125" plus its terminator fits exactly before the next string. 125 is
 *   also the upper bound the client clamps cl_maxpackets to (RVA 0x89ad).
 */
static const code_rule_t patch_net_defaults[] = {
    { ENGINE, true, "rate default = 90000", 0x1cd724, 6,
      { '2', '5', '0', '0', '0', 0 }, { '9', '0', '0', '0', '0', 0 } },
    { ENGINE, true, "cl_maxpackets default = 125", 0x1cd5c8, 4,
      { '3', '0', 0, 0 }, { '1', '2', '5', 0 } },
};

REGISTER_CODE_RULES(patch_net_defaults)
