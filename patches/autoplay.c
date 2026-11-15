#include "patches.h"

/* The loading menu's draw callback at RVA 0xa480 reveals the PLAY! button
 * when cg_pbloading reaches 9. Its hidden flag gates this transition, so
 * the call at 0xae17 runs once after loading finishes.
 *
 * Replace Menu_SetCursorToItem with the existing PLAY! handler at 0x9c60,
 * which closes the menus, releases UI input and clears cl_paused. Both
 * use caller stack cleanup; the handler ignores the two menu arguments,
 * which the original caller still removes. This relative call introduces
 * no absolute operands or PE relocations.
 */
static const code_rule_t patch_autoplay =
    { UI, true, "autoplay after loading finishes", 0xae17, 5,
      { 0xe8, B4(0x2c440u - 0xae1cu) },
      { 0xe8, B4(0x9c60u - 0xae1cu) } };

REGISTER_CODE_RULE(patch_autoplay)
