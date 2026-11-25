#include "patches.h"

/* The leaderboard button's text at VA 0x10062a5c is a localization key.
 * A literal GITHUB label fits the existing buffer. Its initialization and
 * drawing must still use the normal button-label path for size and alignment.
 * Its click handler pushes the browser URL at RVA 0x1dd77.
 *
 * The GitHub URL exceeds the original igwdeath.com string's capacity. Reuse
 * the text buffer belonging to the disabled main-menu promotion. Keep the
 * push instruction's absolute operand in its existing relocation slot.
 */

/* The RVA and size of the leaderboard key's "#menu-leaderboard" buffer,
 * whose bytes the label dispatch helper below treats as literal labels.
 */
#define MAIN_MENU_LABELS 0x62a5cu
#define MAIN_MENU_LABELS_SIZE 18u

static const code_rule_t patch_main_menu_github[] = {
    { UI, true, "main-menu GitHub: button label", MAIN_MENU_LABELS, 7,
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

    /* The disabled promotion's ticker supplies 81 relocation-free code bytes
     * from here on, of which this helper takes 32. It preserves ECX and
     * recognizes literal labels by their address: anything within the
     * leaderboard key's former MAIN_MENU_LABELS_SIZE bytes, GITHUB at its
     * start and further literals behind it (JOIN of ui_main_menu_join.c). The
     * buffer's address comes from call/pop, so rebasing needs no new
     * relocation: neg/lea leave the label's offset into the buffer in ECX,
     * which an unsigned compare checks against the buffer's size.
     * These literals and ordinary #keys return to the normal button-label
     * path. Other literals advance the return address by 17 bytes to their
     * existing fallback. The callers still check for escaped ## strings
     * themselves.
     */
    { UI, true, "main-menu GitHub: label dispatch helper, first part", 0x1dfaf, 16,
      { 0xe8, 0xbc, 0x17, 0x02, 0x00, 0x83, 0xe8, 0x01,
        0x74, 0x30, 0x83, 0xe8, 0x01, 0x74, 0x1c, 0x83 },
      { 0x51,                                 /* push ecx */
        0xe8, 0, 0, 0, 0,                     /* call $+5 */
        0x59,                                 /* pop ecx: VA of RVA 0x1dfb5 */
        0xf7, 0xd9,                           /* neg ecx */
        0x8d, 0x8c, 0x01,                     /* lea ecx,[ecx+eax+...]: offset */
        B4(0x1dfb5u - MAIN_MENU_LABELS) } },
    { UI, true, "main-menu GitHub: label dispatch helper, final part", 0x1dfbf, 16,
      { 0xe8, 0x01, 0xc7, 0x44, 0x24, 0x10, 0x41, 0, 0, 0, 0xbd, 0x2a,
        0, 0, 0, 0x75 },
      { 0x83, 0xf9, MAIN_MENU_LABELS_SIZE,    /* cmp ecx,size */
        0x59,                                 /* pop ecx */
        0x72, 0x09,                           /* jb: a literal label, ret */
        0x80, 0x38, 0x23,                     /* cmp byte [eax],'#' */
        0x74, 0x04,                           /* je: a #key, ret */
        0x83, 0x04, 0x24, 0x11,               /* add dword [esp],17 */
        0xc3 } },                             /* ret */

    /* These selectors otherwise route every literal to the small text path.
     * Both original branches skip exactly 17 bytes after the replaced range.
     */
    { UI, true, "main-menu GitHub: normal button-label layout", 0x2c21e, 5,
      { 0x80, 0x38, 0x23, 0x75, 0x11 },
      { 0xe8, B4(0x1dfafu - 0x2c223u) } },
    { UI, true, "main-menu GitHub: normal button-label drawing", 0x2da7b, 5,
      { 0x80, 0x38, 0x23, 0x75, 0x11 },
      { 0xe8, B4(0x1dfafu - 0x2da80u) } },
};

REGISTER_CODE_RULES(patch_main_menu_github)
