#include "patches.h"

/* Once a refresh of the server browser's Internet list (source 2) or of
 * source 1 ends, RVAs 0x32228 and 0x32bf6 point its status label (VA
 * 0x100d8208) at the localization key "#serverbrowsermsg" at VA
 * 0x1006df40, which default.lang maps to "Visit www.igwarlord.com for the
 * latest news!". A NUL over the key's '#' leaves an empty literal, which
 * is not looked up and draws nothing, as the label already is for the
 * other sources. Nothing else references the key, and no instruction or
 * relocation changes.
 */
static const code_rule_t patch_browser_news_hint = {
    UI, true, "browser news hint: empty text", 0x6df40, 1,
    "#", "",
};

REGISTER_CODE_RULE(patch_browser_news_hint)
