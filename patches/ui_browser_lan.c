#include "patches.h"

/* The server browser's initializer at RVA 0x34a30 runs every time the main
 * menu's JOIN (MULTIPLAYER) button opens the browser. Near its end it
 * selects the Internet source: it marks the Internet button (id 0x28,
 * selection flag at VA 0x100d7980) as selected and the LAN button (id
 * 0x27, flag at VA 0x100d789c) as not, sets ui_browserMaster to 1 and
 * switches the server list to source 2 through RVA 0x34320, after
 * resetting the current source at VA 0x100d3788 so the switch always
 * happens.
 *
 * Select LAN instead, the same way the LAN button's own click handler at
 * RVA 0x3483b does: its flag set, the Internet one cleared, source 0 and
 * ui_browserMaster 0. Throughout the initializer ESI holds 1 and EDI 0,
 * so swapping the two flag stores only exchanges their ModRM register
 * fields. Every absolute operand stays in its existing relocation slot.
 */
static const code_rule_t patch_browser_lan[] = {
    { UI, true, "browser LAN: select LAN button", 0x38d0c, 6,
      { 0x89, 0x3d, B4(0x100d789cu) },    /* mov [LAN flag],edi */
      { 0x89, 0x35, B4(0x100d789cu) } },  /* mov [LAN flag],esi */
    { UI, true, "browser LAN: deselect Internet button", 0x38d12, 6,
      { 0x89, 0x35, B4(0x100d7980u) },    /* mov [Internet flag],esi */
      { 0x89, 0x3d, B4(0x100d7980u) } },  /* mov [Internet flag],edi */
    { UI, true, "browser LAN: LAN server source", 0x38d18, 10,
      { 0xc7, 0x05, B4(0x100d3788u), B4(2u) },
      { 0xc7, 0x05, B4(0x100d3788u), B4(0u) } },
    /* The float argument of the following Cvar_SetValue("ui_browserMaster"). */
    { UI, true, "browser LAN: ui_browserMaster 0", 0x38d2d, 2,
      { 0xd9, 0xe8 },                     /* fld1 */
      { 0xd9, 0xee } },                   /* fldz */
};

REGISTER_CODE_RULES(patch_browser_lan)
