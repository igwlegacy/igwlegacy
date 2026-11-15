#include "patches.h"

/* IGW's centerprint draws its text on a dark band and treats the y argument
 * as an offset from the screen centre in 480-line units: CG_DrawCenterString
 * (0x10a70) and its text helper (0x10810) scale |y| by vidHeight / 480 and
 * subtract it for a negative y, add it otherwise. Callers passing 0 therefore
 * cover the crosshair.
 *
 * Move exactly those callers up by CENTERPRINT_RAISE, a quarter of the
 * screen height. Callers that already chose a lower position (warnings at
 * 100 and 170, "Use %s" at 144, the respawn countdown at 210) keep it.
 * Every y is pushed as a sign-extended imm8 (6a xx), so only that byte
 * changes; no operands with relocations are touched.
 */
#define CENTERPRINT_RAISE 128

/* The imm8 written by `push imm8`, sign-extended by the CPU. */
#define CENTERPRINT_Y ((unsigned char)(-CENTERPRINT_RAISE))

_Static_assert(CENTERPRINT_RAISE > 0 && CENTERPRINT_RAISE <= 128,
               "the raise must fit a negative sign-extended imm8");

static const code_rule_t patch_centerprint_position[] = {
    /* 0x121c0: the round start countdown ("ROUND STARTS IN X"), through
     * CG_CenterPrintTimed with its own 30 ms lifetime.
     */
    { CGAME, true, "centerprint position: round start countdown", 0x1227c, 2,
      { 0x6a, 0x00 },
      { 0x6a, CENTERPRINT_Y } },
    /* The round end message (#winmsg-atelia/-confed) of the function just
     * before, which also sets cg_winteam.
     */
    { CGAME, true, "centerprint position: round winner", 0x12152, 2,
      { 0x6a, 0x00 },
      { 0x6a, CENTERPRINT_Y } },
    /* CG_EntityEvent: the airstrike announcements. The incoming one's y
     * is pushed ahead of the va() arguments and left on the stack.
     */
    { CGAME, true, "centerprint position: airstrike incoming", 0x23c38, 2,
      { 0x6a, 0x00 },
      { 0x6a, CENTERPRINT_Y } },
    { CGAME, true, "centerprint position: airstrike prevented", 0x23c53, 2,
      { 0x6a, 0x00 },
      { 0x6a, CENTERPRINT_Y } },
    /* CG_ServerCommand: the server's "cp" command. */
    { CGAME, true, "centerprint position: server cp", 0x3178b, 2,
      { 0x6a, 0x00 },
      { 0x6a, CENTERPRINT_Y } },
};

REGISTER_CODE_RULES(patch_centerprint_position)
