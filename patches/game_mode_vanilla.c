#include "patches.h"

/* The host-game menu's "Game mode:" dropdown names its first entry, which
 * the UI maps to g_gametype 3 (coop), from the table at uix86 RVA 0x6d0b8.
 * Its only reference is that table's relocated first slot. The entry is
 * renamed to VANILLA in place: "COOP" at 0x61340 is followed by three
 * alignment zeros up to the next string at 0x61348, which leaves exactly
 * the eight bytes "VANILLA" and its terminator need.
 *
 * The entry is already the default. The menu initializer zeroes its whole
 * item block, including the dropdown's selection, every time the menu
 * opens, so it always starts on the first entry.
 */
static const code_rule_t patch_game_mode_vanilla[] = {
    { UI, true, "game mode VANILLA: name", 0x61340u, 8,
      "COOP\0\0\0", "VANILLA" },
};

REGISTER_CODE_RULES(patch_game_mode_vanilla)
