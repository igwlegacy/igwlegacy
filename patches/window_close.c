#include "patches.h"

/* MainWndProc handles WM_CLOSE by appending "quit" to the command buffer
 * (call at RVA 0x4e4ed), then forwards the message to DefWindowProcA, which
 * destroys the game window before the queued command runs.
 *
 * Call Com_Quit_f (RVA 0x20920) directly instead. It shuts down the server,
 * client, renderer and filesystem before Sys_Quit exits the process, so
 * shutdown starts with the game window still intact and does not depend
 * on another frame executing the command buffer. Com_Quit_f takes no
 * arguments and does not return. The preceding mov edi,"quit" and its
 * HIGHLOW relocation stay untouched; the call remains relative.
 *
 * Only WM_CLOSE takes this path. WM_DESTROY must keep its original handler
 * because the renderer also destroys the window during vid_restart.
 */
static const code_rule_t patch_window_close =
    { ENGINE, true, "quit immediately on game window close", 0x4e4ed, 5,
      { 0xe8, B4(0x1fab0u - (0x4e4edu + 5u)) }, /* call Cbuf_AddText */
      { 0xe8, B4(0x20920u - (0x4e4edu + 5u)) } }; /* call Com_Quit_f */

REGISTER_CODE_RULE(patch_window_close)
