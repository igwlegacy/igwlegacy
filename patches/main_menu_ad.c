#include "patches.h"

/* The main-menu initializer at RVA 0x1e390 adds this promotional panel
 * through Menu_AddItem (RVA 0x2c130). Only omit the calls: their callers
 * still push and clean up the arguments, keeping the stack balanced.
 * Relative calls contain no absolute operands or PE relocations.
 */
#define OMIT_ITEM(label, where) \
    { UI, true, label, where, 5, \
      { 0xe8, B4(0x2c130u - (where) - 5) }, \
      { 0x90, 0x90, 0x90, 0x90, 0x90 } }

const code_rule_t patch_main_menu_ad[11] = {
    OMIT_ITEM("main-menu ad: background bitmap", 0x1fc9a),
    OMIT_ITEM("main-menu ad: foreground bitmap", 0x1fca9),
    OMIT_ITEM("main-menu ad: launch button", 0x1fcb8),
    OMIT_ITEM("main-menu ad: MOTD heading", 0x1fcc7),
    OMIT_ITEM("main-menu ad: update label", 0x1fcd6),
    OMIT_ITEM("main-menu ad: news scrollbar", 0x1fce8),
    OMIT_ITEM("main-menu ad: scroll down button", 0x1fcf7),
    OMIT_ITEM("main-menu ad: scroll up button", 0x1fd06),
    OMIT_ITEM("main-menu ad: website hit area", 0x1fd15),

    /* The menu draw callback reserves one stack word at entry. After the
     * normal menu/error draw, release it and return before processing MOTD
     * responses, unhiding the panel, or drawing the separate text ticker.
     * Retain the relocated operand in the now unreachable instruction.
     */
    { UI, true, "main-menu ad: skip MOTD update and ticker", 0x1e1fc, 7,
      { 0x83, 0x3d, B4(0x10103fecu), 0x01 },
      { 0x59, 0xc3, B4(0x10103fecu), 0x01 } },

    /* Both callers ignore the result of this dedicated request starter.
     * Return before it pushes request type 5 and calls engine syscall 121.
     */
    { UI, true, "main-menu ad: disable update request", 0x1dde0, 7,
      { 0x6a, 0x05, 0xe8, 0x69, 0xcf, 0x01, 0x00 },
      { 0xc3, 0x05, 0xe8, 0x69, 0xcf, 0x01, 0x00 } },
};
