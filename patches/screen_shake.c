#include "patches.h"

/* reigw cg_local.h: a void shake starter; callers clean the stack.
 * Only the first byte changes, so the absolute operand and its existing
 * relocation remain intact in the now unreachable instructions.
 */
const code_rule_t patch_screen_shake = {
    CGAME, true, "explosion/death shake: return immediately", 0x13e00, 9,
    { 0x83, 0xec, 0x10, 0x8b, 0x0d, B4(0x30d7b7c4u) },
    { 0xc3, 0xec, 0x10, 0x8b, 0x0d, B4(0x30d7b7c4u) }
};
