#include "patches.h"

#include <float.h>
#include <string.h>

/* Square cursor size in the menus' 640x480 coordinate system. Rebuild and
 * install both uix86.dll and cgamex86.dll after changing this setting.
 */
#define CURSOR_SIZE 13

_Static_assert(CURSOR_SIZE > 0, "Cursor size must be positive");
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
               FLT_MAX_EXP == 128, "Cursor size requires IEEE binary32 floats");

/* UI_Refresh (0x8910) draws the main-menu and pause-menu cursor with the
 * same width and height, originally 12 UI units. Give only that draw a
 * private constant in alignment padding; the pooled 12.0 has other users.
 * The redirected operand keeps its existing HIGHLOW relocation slot.
 */
static code_rule_t patch_cursor_menu_size = {
    UI, true, "cursor size: menu dimension", 0x8a98, 4,
    { 0xcc, 0xcc, 0xcc, 0xcc }, { 0 }
};

static const code_rule_t patch_cursor_size[] = {
    { UI, true, "cursor size: main menu and pause menu", 0x89ea, 6,
      { 0xd9, 0x05, B4(0x1005fa2cu) },
      { 0xd9, 0x05, B4(0x10008a98u) } },

    /* The purchasing/RTS overlay cursor (0x591c0) passes its dimensions
     * straight to trap_R_DrawStretchPic, originally 20 physical pixels.
     * Load glconfig.vidHeight instead, preserving the relocated operand,
     * and convert it to CURSOR_SIZE UI units in the helper below.
     */
    { CGAME, true, "cursor size: overlay display height", 0x591e4, 6,
      { 0xd9, 0x05, B4(0x300b4accu) },
      { 0xdb, 0x05, B4(0x30d5e930u) } },
    { CGAME, true, "cursor size: purchasing overlays", 0x591ea, 8,
      { 0xd9, 0x54, 0x24, 0x0c, 0xd9, 0x5c, 0x24, 0x08 },
      { 0xe8, B4(0x59012u - 0x591efu), 0x90, 0x90, 0x90 } },

    /* CALL adds a return address: the original width/height slots move
     * from ESP+8/+12 to ESP+12/+16. Keep the x87 stack balanced and leave
     * EAX (shader), XMM0 (cursor y), and cursor coordinates untouched.
     * Both helper fragments occupy padding after returning functions.
     */
    { CGAME, true, "cursor size: overlay dimension arguments", 0x59097, 9,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0xd9, 0x54, 0x24, 0x10, 0xd9, 0x5c, 0x24, 0x0c, 0xc3 } },
};

static code_rule_t patch_cursor_overlay_scale = {
    CGAME, true, "cursor size: overlay scale by display height", 0x59012, 13,
    { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
    { 0x68, 0, 0, 0, 0,        /* push CURSOR_SIZE / 480.0f */
      0xd8, 0x0c, 0x24,        /* fmul dword [esp] */
      0x83, 0xc4, 0x04,        /* add esp,4 */
      0xeb, 0x78 }             /* jmp 0x59097 */
};

PATCH_CONSTRUCTOR(configure_cursor_size)
{
    const float size = CURSOR_SIZE;
    const float scale = CURSOR_SIZE / 480.0f;
    uint32_t bits;

    /* Encode the host's float bits explicitly as little-endian PE data. */
    memcpy(&bits, &size, sizeof(bits));
    put32(patch_cursor_menu_size.replacement, bits);
    memcpy(&bits, &scale, sizeof(bits));
    put32(patch_cursor_overlay_scale.replacement + 1, bits);
}

/* Keep the constants, both helper fragments, and their readers together. */
REGISTER_CODE_RULE(patch_cursor_menu_size)
REGISTER_CODE_RULES(patch_cursor_size)
REGISTER_CODE_RULE(patch_cursor_overlay_scale)
