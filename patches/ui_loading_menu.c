#include "patches.h"

/* CL_MapLoading (engine RVA 0xbca0) refreshes the screen before
 * SV_SpawnServer shuts down the renderer and clears the hunk. That redraw
 * initializes the loading menu while the previous menu's renderer resources
 * are still live, allowing a mixed start-game/loading frame to be shown.
 *
 * Omit only this premature SCR_UpdateScreen (RVA 0x12ff0) in both the
 * existing-loopback and fresh-localhost paths. Cgame's loading updates
 * redraw the screen normally after the renderer and UI have restarted.
 * The loopback path tail-calls the refresh after restoring EBX, so it can
 * return directly. The fresh-localhost path continues connecting normally.
 * Neither relative branch carries a PE relocation.
 */
static const code_rule_t patch_loading_menu[] = {
    { ENGINE, true, "loading menu: defer loopback redraw until renderer restart", 0xbd53, 5,
      { 0xe9, B4(0x12ff0u - 0xbd58u) },
      { 0xc3, 0x90, 0x90, 0x90, 0x90 } },
    { ENGINE, true, "loading menu: defer localhost redraw until renderer restart", 0xbda3, 5,
      { 0xe8, B4(0x12ff0u - 0xbda8u) },
      { 0x90, 0x90, 0x90, 0x90, 0x90 } },
};

REGISTER_CODE_RULES(patch_loading_menu)
