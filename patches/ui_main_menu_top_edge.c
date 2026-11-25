#include "patches.h"

/* The main-menu initializer assigns textures/ui/mmtopedge1 to the bitmap
 * at VA 0x100feb88. Omit its Menu_AddItem call so the widget is never
 * attached to the menu. Keep the argument pushes and caller's stack
 * cleanup intact; the relative call has no PE relocation to preserve.
 */
static const code_rule_t patch_main_menu_top_edge = {
    UI, true, "main-menu top edge: omit bitmap widget", 0x1fb5c, 5,
    { 0xe8, 0xcf, 0xc5, 0x00, 0x00 },
    { 0x90, 0x90, 0x90, 0x90, 0x90 },
};

REGISTER_CODE_RULE(patch_main_menu_top_edge)
