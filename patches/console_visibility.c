#include "patches.h"

/* Sys_CreateConsole (RVA 0x4db70) creates the top-level window without
 * WS_VISIBLE, then shows it with SW_SHOWDEFAULT at RVA 0x4de93. Jump to
 * the epilogue instead, skipping ShowWindow, UpdateWindow, foreground and
 * focus calls, and the assignment of 1 to the console's visibility level.
 * The console and its controls still exist and collect the startup log.
 *
 * Leaving the visibility level at its initial zero is essential: Sys_Error
 * (RVA 0x49bb0) calls Sys_ShowConsole(1) only when that level differs from 1.
 * It can therefore reveal the console with the accumulated log on a fatal
 * error, including one during startup. Explicit viewlog settings retain
 * their normal behavior. The short jump leaves all absolute operands and
 * their HIGHLOW relocation entries untouched.
 */
static const code_rule_t patch_console_visibility =
    { ENGINE, true, "hide console during startup", 0x4de93, 2,
      { 0x6a, 0x0a },                         /* push SW_SHOWDEFAULT */
      { 0xeb, 0x37 } };                       /* jmp 0x4decc */

REGISTER_CODE_RULE(patch_console_visibility)
