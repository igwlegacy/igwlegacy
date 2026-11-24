#include "patches.h"

/* While the server browser refreshes, the two refresh starters at RVAs
 * 0x321de and 0x32a8c point its status label (VA 0x100d8208) at the
 * literal "Press SPACE to stop" at VA 0x100644d8. SPACE does not actually
 * stop a refresh, so blank the hint: a NUL over its first byte leaves an
 * empty label, as the browser shows it when idle on LAN. Nothing else
 * references the string, and no instruction or relocation changes.
 */
static const code_rule_t patch_browser_stop_hint = {
    UI, true, "browser stop hint: empty text", 0x644d8, 1,
    "P", "",
};

REGISTER_CODE_RULE(patch_browser_stop_hint)
