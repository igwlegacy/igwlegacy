#include "patches.h"

/* Initial positions in the menus' 640x480 coordinate system, measured from
 * the top left. Rebuild and install uix86.dll after changing these settings.
 * Main and ingame positions apply when their menus open; the loading
 * position applies when loading finishes and the PLAY! button appears.
 */
#define MAIN_MENU_CURSOR_X 320
#define MAIN_MENU_CURSOR_Y 240
#define INGAME_MENU_CURSOR_X 140
#define INGAME_MENU_CURSOR_Y 240
#define LOADING_COMPLETE_CURSOR_X 320
#define LOADING_COMPLETE_CURSOR_Y 240

_Static_assert(MAIN_MENU_CURSOR_X >= 0 && MAIN_MENU_CURSOR_X <= 640 &&
               MAIN_MENU_CURSOR_Y >= 0 && MAIN_MENU_CURSOR_Y <= 480,
               "Main-menu cursor must be inside the 640x480 menu area");
_Static_assert(INGAME_MENU_CURSOR_X >= 0 && INGAME_MENU_CURSOR_X <= 640 &&
               INGAME_MENU_CURSOR_Y >= 0 && INGAME_MENU_CURSOR_Y <= 480,
               "Ingame-menu cursor must be inside the 640x480 menu area");
_Static_assert(LOADING_COMPLETE_CURSOR_X >= 0 && LOADING_COMPLETE_CURSOR_X <= 640 &&
               LOADING_COMPLETE_CURSOR_Y >= 0 && LOADING_COMPLETE_CURSOR_Y <= 480,
               "Loading-completion cursor must be inside the 640x480 menu area");

/* UI_MainMenu calls UI_PushMenu at 0x1fd7e. UI_InGameMenu calls its
 * initializer at 0x1c66e, after assigning the stock pixel position (319,80).
 * Wrap those calls to set the position once, then tail-call the original
 * callee with its arguments and return address intact. The shared draw and
 * mouse-event code continue to use the same cursor coordinates.
 */
static const code_rule_t patch_cursor_position_hooks[] = {
    { UI, true, "cursor position: main menu", 0x1fd7e, 5,
      { 0xe8, B4(0x4f20u - 0x1fd83u) },
      { 0xe8, B4(0x77e1u - 0x1fd83u) } },
    { UI, true, "cursor position: ingame menu", 0x1c66e, 5,
      { 0xe8, B4(0x1bb80u - 0x1c673u) },
      { 0xe8, B4(0x7f11u - 0x1c673u) } },

    /* Both the loading draw callback and initializer place the cursor when
     * cg_pbloading == 9. Replace their width/2 calculations with a helper
     * that writes both coordinates and returns the scaled x in EAX for
     * the existing store. Skip the later fixed y=400 stores, leaving their
     * relocated address operands in place as unreachable data. The draw
     * callback's hidden-button flag makes its reset run only once.
     */
    { UI, true, "cursor position: loading finishes", 0xadd4, 5,
      { 0x99, 0x2b, 0xc2, 0xd1, 0xf8 },
      { 0xe8, B4(0x8c41u - 0xadd9u) } },
    { UI, true, "cursor position: loading finishes, omit fixed y", 0xadf9, 10,
      { 0xc7, 0x05, B4(0x1010722cu), B4(400) },
      { 0xeb, 0x08, B4(0x1010722cu), B4(400) } },
    { UI, true, "cursor position: loading already complete", 0xb31d, 5,
      { 0x99, 0x2b, 0xc2, 0xd1, 0xf8 },
      { 0xe8, B4(0x8c41u - 0xb322u) } },
    { UI, true, "cursor position: loading complete, omit fixed y", 0xb331, 10,
      { 0xc7, 0x05, B4(0x1010722cu), B4(400) },
      { 0xeb, 0x08, B4(0x1010722cu), B4(400) } },
};

/* All fragments occupy guarded int3 padding after returning functions.
 * Relative branches and call/pop address discovery need no PE relocations.
 */
#define CURSOR_FRAGMENT(label, where, size, ...) \
    { UI, true, "cursor position: " label, where, size, \
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, \
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }, \
      { __VA_ARGS__ } }
#define CURSOR_JUMP(where, target) 0xe9, B4((target) - (where) - 5u)

static const code_rule_t patch_cursor_position_helpers[] = {
    CURSOR_FRAGMENT("main-menu coordinates", 0x77e1, 15,
        0x68, B4(MAIN_MENU_CURSOR_Y),      /* push y */
        0x68, B4(MAIN_MENU_CURSOR_X),      /* push x */
        CURSOR_JUMP(0x77ebu, 0x7aa1u)),
    CURSOR_FRAGMENT("main-menu continuation", 0x7aa1, 13,
        0xe8, B4(0x4fe1u - 0x7aa6u),     /* call set_position */
        0x83, 0xc4, 0x08,                /* add esp,8 */
        CURSOR_JUMP(0x7aa9u, 0x4f20u)),

    CURSOR_FRAGMENT("ingame-menu coordinates", 0x7f11, 15,
        0x68, B4(INGAME_MENU_CURSOR_Y),
        0x68, B4(INGAME_MENU_CURSOR_X),
        CURSOR_JUMP(0x7f1bu, 0x7f91u)),
    CURSOR_FRAGMENT("ingame-menu continuation", 0x7f91, 13,
        0xe8, B4(0x4fe1u - 0x7f96u),
        0x83, 0xc4, 0x08,
        CURSOR_JUMP(0x7f99u, 0x1bb80u)),

    CURSOR_FRAGMENT("loading-completion coordinates", 0x8c41, 15,
        0x68, B4(LOADING_COMPLETE_CURSOR_Y),
        0x68, B4(LOADING_COMPLETE_CURSOR_X),
        CURSOR_JUMP(0x8c4bu, 0x9f41u)),
    CURSOR_FRAGMENT("loading-completion conversion", 0x9f41, 13,
        0xe8, B4(0x4fe1u - 0x9f46u),
        0x83, 0xc4, 0x08,
        CURSOR_JUMP(0x9f49u, 0xa071u)),
    CURSOR_FRAGMENT("loading-completion continuation", 0xa071, 8,
        0x8b, 0x80, B4(0x107228u),       /* mov eax,[eax+cursorx] */
        0x99,                            /* cdq: preserve division's EDX=0 */
        0xc3),

    /* set_position(x,y): the UI stores mouse positions in physical pixels.
     * Convert using its existing scale and horizontal widescreen bias:
     * cursorx = x * scale + bias; cursory = y * scale.
     * EAX is caller-saved; other registers and the x87 depth are preserved.
     */
    CURSOR_FRAGMENT("locate UI image", 0x4fe1, 11,
        0xe8, 0, 0, 0, 0,               /* call next instruction */
        0x58,                            /* pop eax */
        CURSOR_JUMP(0x4fe7u, 0x5022u)),
    CURSOR_FRAGMENT("read x", 0x5022, 14,
        0x2d, B4(0x4fe6u),               /* sub eax,return RVA */
        0xdb, 0x44, 0x24, 0x04,          /* fild dword [esp+4] */
        CURSOR_JUMP(0x502bu, 0x5144u)),
    CURSOR_FRAGMENT("scale x", 0x5144, 11,
        0xd8, 0x88, B4(0x109ee4u),       /* fmul dword [eax+scale] */
        CURSOR_JUMP(0x514au, 0x52b1u)),
    CURSOR_FRAGMENT("offset x", 0x52b1, 11,
        0xd8, 0x80, B4(0x109eecu),       /* fadd dword [eax+bias] */
        CURSOR_JUMP(0x52b7u, 0x53f2u)),
    CURSOR_FRAGMENT("store x", 0x53f2, 11,
        0xdb, 0x98, B4(0x107228u),       /* fistp dword [eax+cursorx] */
        CURSOR_JUMP(0x53f8u, 0x5612u)),
    CURSOR_FRAGMENT("read y", 0x5612, 9,
        0xdb, 0x44, 0x24, 0x08,          /* fild dword [esp+8] */
        CURSOR_JUMP(0x5616u, 0x5803u)),
    CURSOR_FRAGMENT("scale y", 0x5803, 11,
        0xd8, 0x88, B4(0x109ee4u),
        CURSOR_JUMP(0x5809u, 0x5ad1u)),
    CURSOR_FRAGMENT("store y", 0x5ad1, 7,
        0xdb, 0x98, B4(0x10722cu),       /* fistp dword [eax+cursory] */
        0xc3),
};

/* Keep the wrappers and shared helper together when enabling this patch. */
REGISTER_CODE_RULES(patch_cursor_position_hooks)
REGISTER_CODE_RULES(patch_cursor_position_helpers)
