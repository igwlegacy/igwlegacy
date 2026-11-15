#include "patches.h"

/* r_heathazedistort is registered by the renderer inside igwarlord.exe as
 * ri.Cvar_Get("r_heathazedistort", "0.01", CVAR_ARCHIVE) and stored in the
 * cvar_t pointer at VA 0x01444030. Its only reader multiplies the float value
 * (+0x1c) into the heat-haze distortion strength. No module queries it by
 * name.
 *
 * Force it to 0:
 *
 * 1. Registration passes the existing "0" literal (VA 0x005cbf2c) instead of
 *    "0.01" and calls ri.Cvar_Set (refimport slot +0x24, VA 0x0121a6c4)
 *    instead of ri.Cvar_Get (+0x20, VA 0x0121a6c0). The engine's Cvar_Set
 *    forwards to Cvar_Set2(name, value, qtrue) and returns the cvar_t pointer
 *    in EAX, so the following store still captures the CVAR. A saved or
 *    user-created value is overwritten with "0" on every renderer start,
 *    including vid_restart. The surplus flags argument is ignored by the
 *    cdecl callee and still cleaned up by the caller. Both operands keep
 *    their HIGHLOW relocation slots.
 *
 * 2. The reader at RVA 0x96ec2 loads 0.0 with "fldz; nop" instead of the
 *    CVAR's value, so runtime changes have no effect. x87 loads leave EFLAGS
 *    intact for the pending "jl" that follows. The preceding pointer load
 *    keeps its absolute operand and HIGHLOW relocation.
 */
static const code_rule_t patch_heat_haze[] = {
    { ENGINE, true, "r_heathazedistort register value = 0", 0x750db, 5,
      { 0x68, B4(0x005dce40u) }, { 0x68, B4(0x005cbf2cu) } },
    { ENGINE, true, "r_heathazedistort register via Cvar_Set", 0x750ea, 6,
      { 0xff, 0x15, B4(0x0121a6c0u) }, { 0xff, 0x15, B4(0x0121a6c4u) } },
    { ENGINE, true, "r_heathazedistort read 0", 0x96ec2, 3,
      { 0xd9, 0x41, 0x1c }, { 0xd9, 0xee, 0x90 } },
};

REGISTER_CODE_RULES(patch_heat_haze)
