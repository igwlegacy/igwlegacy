#include "patches.h"

/* The loading menu's background bitmap at VA 0x10086d78 uses alignment
 * flag 4, unlike the main-menu background. Bitmap initialization at RVA
 * 0x29110 then scales both axes through the width-based helper at 0x3fb10:
 * a 640x480 background becomes 1920x1440 on a 1920x1080 display.
 *
 * Clear that flag to use independent horizontal and vertical scaling, as
 * the main menu does. FLDZ at 0xaf8a already supplies zero for the bitmap's
 * position, so FST can also store zero bits into its alignment flags.
 * Keep EBX=4 for subsequent menu items and preserve the absolute operand's
 * existing HIGHLOW relocation slot.
 *
 * The 1280x1024 branch also substitutes a 514-unit height. Independent
 * vertical scaling needs the usual 480 units to fill the screen exactly.
 */
static const code_rule_t patch_loading_background[] = {
    { UI, true, "loading background: scale each axis to the screen", 0xaf97, 6,
      { 0x89, 0x1d, B4(0x10086da8u) },
      { 0xd9, 0x15, B4(0x10086da8u) } },
    { UI, true, "loading background: full height at 1280x1024", 0xafd1, 6,
      { 0xd9, 0x05, B4(0x1005fac8u) },
      { 0xd9, 0x05, B4(0x1005fae4u) } },
};

REGISTER_CODE_RULES(patch_loading_background)
