#include "patches.h"

/* The server browser's initializer at RVA 0x34a30 gives a label item (text
 * pointer at VA 0x100d8274, set at RVA 0x37a90) the localization key
 * "#banner-leaderboard" at VA 0x10064588, which default.lang maps to
 * "Visit www.igwdeath.com for the leaderboard!". A NUL
 * over the key's '#' leaves an empty literal, which is not looked up and
 * draws nothing. Nothing else references the key, and no instruction or
 * relocation changes.
 */
static const code_rule_t patch_browser_leaderboard_banner = {
    UI, true, "browser leaderboard banner: empty text", 0x64588, 1,
    "#", "",
};

REGISTER_CODE_RULE(patch_browser_leaderboard_banner)
