#include "patcher.h"

#define B4(a) (a) & 255, ((a) >> 8) & 255, ((a) >> 16) & 255, ((a) >> 24) & 255

/* The zero float at cgamex86 RVA 0xa5344 is already relocated by the DLL's
 * existing HIGHLOW entries. Keeping the operand in the same place preserves
 * those relocations when the DLL loads away from its preferred base.
 */
#define READ_ZERO(label, where, op0, op1, address) \
    { CGAME, true, label, where, 6, \
      { op0, op1, B4(address) }, { op0, op1, B4(0x300a5344u) } }

const code_rule_t code_rules[] = {
    /* reigw cg_local.h: a void shake starter; callers clean the stack.
     * Only the first byte changes, so the absolute operand and its existing
     * relocation remain intact in the now unreachable instructions.
     */
    { CGAME, true, "explosion/death shake: return immediately", 0x13e00, 9,
      { 0x83, 0xec, 0x10, 0x8b, 0x0d, B4(0x30d7b7c4u) },
      { 0xc3, 0xec, 0x10, 0x8b, 0x0d, B4(0x30d7b7c4u) } },

    /* CG_OffsetFirstPersonView: redirect reads, preserving the x87 stack
     * and the damage values used by the HUD's direction indicators.
     */
    READ_ZERO("damage pitch",        0x349b7, 0xd9, 0x05, 0x30da03b0u),
    READ_ZERO("damage roll",         0x349d4, 0xd8, 0x0d, 0x30da03b4u),
    READ_ZERO("damage pitch return", 0x34a04, 0xd9, 0x05, 0x30da03b0u),
    READ_ZERO("landing dip",         0x34c59, 0xd8, 0x0d, 0x30d9e720u),
};
const size_t code_rule_count = ARRAY_SIZE(code_rules);
