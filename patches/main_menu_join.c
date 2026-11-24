#include "patches.h"

/* The main menu's MULTIPLAYER button, item id 11, which opens the server
 * browser, gets its text from the localization key "#joingame" at VA
 * 0x10062a88, which default.lang maps to MULTIPLAYER. No key maps to a
 * plain JOIN, and an unknown key would be drawn as the key itself, so the
 * button gets a literal JOIN label instead.
 *
 * Literal labels normally take the small text path. main_menu_github.c's
 * label dispatch helper sends those within the leaderboard key's former
 * "#menu-leaderboard" buffer through the normal button-label path, which
 * keeps the size and alignment of the other main-menu buttons; that patch
 * must stay enabled for this one. GITHUB occupies the buffer's first 7
 * bytes, JOIN goes right behind it, over the unused "eader".
 */
#define JOIN_LABEL 0x62a63u

static const code_rule_t patch_main_menu_join[] = {
    { UI, true, "main-menu JOIN: label text", JOIN_LABEL, 5,
      "eader", "JOIN" },

    /* The main-menu initializer stores the label pointer into the button
     * item. Point it at JOIN instead of "#joingame"; both operands keep
     * their existing relocation slots.
     */
    { UI, true, "main-menu JOIN: button label", 0x1eb3c, 10,
      { 0xc7, 0x05, B4(0x100ff898u), B4(0x10062a88u) },
      { 0xc7, 0x05, B4(0x100ff898u), B4(0x10000000u + JOIN_LABEL) } },
};

REGISTER_CODE_RULES(patch_main_menu_join)
