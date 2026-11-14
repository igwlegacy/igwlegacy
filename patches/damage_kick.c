#include "patches.h"

/* CG_OffsetFirstPersonView: suppress both phases of the damage kick while
 * preserving the x87 stack and the HUD's damage direction indicators.
 */
static const code_rule_t patch_damage_kick[] = {
    READ_ZERO("damage pitch",        0x349b7, 0xd9, 0x05, 0x30da03b0u),
    READ_ZERO("damage roll",         0x349d4, 0xd8, 0x0d, 0x30da03b4u),
    READ_ZERO("damage pitch return", 0x34a04, 0xd9, 0x05, 0x30da03b0u),
};

REGISTER_CODE_RULES(patch_damage_kick)
