#include "patches.h"

/* CG_OffsetFirstPersonView: landing no longer dips the eye height. */
static const code_rule_t patch_landing_dip =
    READ_ZERO("landing dip", 0x34c59, 0xd8, 0x0d, 0x30d9e720u);

REGISTER_CODE_RULE(patch_landing_dip)
