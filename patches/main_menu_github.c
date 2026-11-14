#include "patches.h"

/* The leaderboard button's text at VA 0x10062a5c is a localization key.
 * A literal GITHUB label bypasses translation and fits the existing buffer.
 * Its click handler pushes the browser URL at RVA 0x1dd77.
 *
 * The GitHub URL exceeds the original igwdeath.com string's capacity. Reuse
 * the text buffer belonging to the disabled main-menu promotion. Keep the
 * push instruction's absolute operand in its existing relocation slot.
 */
const code_rule_t patch_main_menu_github[5] = {
    { UI, true, "main-menu GitHub: button label", 0x62a5c, 7,
      "#menu-l", "GITHUB" },

    { UI, true, "main-menu GitHub: URL storage, first part", 0x62be0, 15,
      "For Victory and", "https://github." },
    { UI, true, "main-menu GitHub: URL storage, second part", 0x62bef, 15,
      " Glory! Continu", "com/igwlegacy/i" },
    { UI, true, "main-menu GitHub: URL storage, final part", 0x62bfe, 9,
      "e the bat", "gwlegacy" },

    { UI, true, "main-menu GitHub: open project URL", 0x1dd77, 5,
      { 0x68, B4(0x10062824u) },
      { 0x68, B4(0x10062be0u) } },
};
