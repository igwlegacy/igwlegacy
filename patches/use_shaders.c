#include "patches.h"

/* r_useShaders is registered by the renderer inside igwarlord.exe as
 * ri.Cvar_Get("r_useShaders", "1", CVAR_ARCHIVE) and stored in the cvar_t
 * pointer at VA 0x014443c4. Every renderer use reads its integer field (+0x20)
 * through that pointer, mostly ANDed or tested with the shader capability
 * word at VA 0x00b2de30. cgamex86.dll queries it by name to gate r_bloom.
 *
 * Forcing it to 1 needs three parts:
 *
 * 1. Registration calls ri.Cvar_Set (refimport slot +0x24, VA 0x0121a6c4)
 *    instead of ri.Cvar_Get (+0x20, VA 0x0121a6c0). The engine's Cvar_Set
 *    (RVA 0x24960) forwards to Cvar_Set2(name, value, qtrue) and leaves its
 *    cvar_t pointer in EAX on every path, so the following store still
 *    captures the CVAR. A saved or user-created value is overwritten with
 *    "1" on every renderer start, including vid_restart. The surplus flags
 *    argument is ignored by the cdecl callee and still cleaned up by the
 *    caller. Only the call operand changes, so its HIGHLOW slot stays valid.
 *
 * 2. Every renderer read behaves as if the integer were 1, so runtime
 *    changes from the console or the UI's graphics toggle have no effect:
 *    - "mov eax,[eax+0x20]" / "mov eax,[ecx+0x20]" becomes
 *      "xor eax,eax; inc eax". The following AND overwrites the flags.
 *    - "test [reg+0x20],r32" becomes "test r8,1"; only ZF is consumed and
 *      it matches for every value of the capability word.
 *    - "cmp [eax+0x20],ebx; je" becomes "cmp ebx,1", which yields the same
 *      ZF as comparing 1 against EBX.
 *    The preceding pointer loads keep their absolute operands and HIGHLOW
 *    relocations.
 *
 * 3. The cgame bloom gate at RVA 0x17720 returns r_useShaders && r_bloom
 *    from string queries. Its early return for a zero r_useShaders becomes
 *    an unconditional skip, leaving only the r_bloom check.
 */
static const code_rule_t patch_use_shaders[] = {
    { ENGINE, true, "r_useShaders register via Cvar_Set", 0x750d0, 6,
      { 0xff, 0x15, B4(0x0121a6c0u) }, { 0xff, 0x15, B4(0x0121a6c4u) } },
    /* Shader capability query: return 1 & capability. */
    { ENGINE, true, "r_useShaders read 1", 0x75ab5, 3,
      { 0x8b, 0x40, 0x20 }, { 0x31, 0xc0, 0x40 } },
    { ENGINE, true, "r_useShaders read 2", 0x7770e, 3,
      { 0x8b, 0x40, 0x20 }, { 0x31, 0xc0, 0x40 } },
    { ENGINE, true, "r_useShaders read 3", 0x784b6, 3,
      { 0x85, 0x50, 0x20 }, { 0xf6, 0xc2, 0x01 } },
    { ENGINE, true, "r_useShaders read 4", 0x7e448, 3,
      { 0x85, 0x41, 0x20 }, { 0xa8, 0x01, 0x90 } },
    { ENGINE, true, "r_useShaders read 5", 0x94942, 3,
      { 0x8b, 0x41, 0x20 }, { 0x31, 0xc0, 0x40 } },
    { ENGINE, true, "r_useShaders read 6", 0x958b1, 3,
      { 0x85, 0x41, 0x20 }, { 0xa8, 0x01, 0x90 } },
    { ENGINE, true, "r_useShaders read 7", 0xb1de3, 3,
      { 0x39, 0x58, 0x20 }, { 0x83, 0xfb, 0x01 } },
    { ENGINE, true, "r_useShaders read 8", 0xc2802, 3,
      { 0x85, 0x41, 0x20 }, { 0xa8, 0x01, 0x90 } },
    { CGAME, true, "bloom ignores r_useShaders", 0x17741, 2,
      { 0x75, 0x02 }, { 0xeb, 0x02 } },
};

REGISTER_CODE_RULES(patch_use_shaders)
