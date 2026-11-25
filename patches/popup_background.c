#include "patches.h"

/* Menu_Draw (uix86 RVA 0x2d980) draws the popup lists of opened dropdowns,
 * menu item type 12, last through RVA 0x2d5e0, e.g. the host-game menu's
 * map selection. That routine underlays an open list with UI_FillRect
 * (RVA 0x7f20) in the color at VA 0x1006dec4, { 0.1, 0.1, 0.1, 0.7 },
 * whose only reference is the push at RVA 0x2d6cd. Raise its alpha to 1.0
 * so the menu no longer shows through the list.
 */
static const code_rule_t patch_popup_background = {
    UI, true, "popup background: opaque", 0x6ded0, 4,
    { B4(0x3f333333u) },  /* 0.7f */
    { B4(0x3f800000u) },  /* 1.0f */
};

REGISTER_CODE_RULE(patch_popup_background)
