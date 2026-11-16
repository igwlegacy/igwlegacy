#include "patches.h"

/* Only com_maxfps registration references this "85" literal. Its four-byte
 * slot includes alignment padding, so "170" and its terminator fit in place.
 * Cvar_Get retains the archive flag and honors saved and runtime values.
 */
static const code_rule_t patch_maxfps =
    { ENGINE, true, "com_maxfps default = 170", 0x1cf5cc, 4,
      { '8', '5', 0, 0 }, { '1', '7', '0', 0 } };

REGISTER_CODE_RULE(patch_maxfps)
