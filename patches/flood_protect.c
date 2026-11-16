#include "patches.h"

/* Stop the server from silently dropping client commands on games hosted
 * by a running client (local and listen-server games).
 *
 * SV_ClientCommand (igwarlord.exe RVA 0x39c90) flood-protects like Q3:
 * every client command pushes cl->nextReliableTime to svs.time +
 * sv_floodTime (1000), and a command arriving before that runs
 * SV_ExecuteClientCommand with clientOk = 0, so the game module never
 * sees it. Unlike ioq3, which exempts listen servers, IGW only applies it
 * when com_cl_running is 1, i.e. never on a dedicated server, and always
 * on a local game.
 *
 * That breaks joining a local game. The client sends its "userinfo" while
 * still CS_PRIMED, which arms the timer. Pressing Enter on the "Hit Enter
 * to start" spectator menu within the next second sends "pickinfo", and
 * the cgame closes that menu straight away (cgamex86 RVA 0x5eb40). The
 * dropped "pickinfo" never spawns the player, who stays a spectator with
 * no menu left to catch Enter (bound to "+use", which spectators ignore).
 *
 * Make the com_cl_running check skip the flood test unconditionally, so
 * clientOk is always 1. Dedicated servers already took that path.
 */
static const code_rule_t patch_flood_protect =
    { ENGINE, true, "no client command flood protection", 0x39d21, 2,
      { 0x75, 0x1b },                       /* jne 0x39d3e (clientOk = 1) */
      { 0xeb, 0x1b } };                     /* jmp 0x39d3e */

REGISTER_CODE_RULE(patch_flood_protect)
