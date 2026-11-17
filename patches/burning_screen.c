#include "patches.h"

/* The burning overlay (0x122d0) records cg.time in cg.burnTime while
 * predictedPlayerState.eFlags has EF_IGW_BURNING (0x40). Otherwise it draws
 * a fade when cg.time - cg.burnTime is between 0 and 1500 ms. CG_Init
 * clears cg, so a new game's first 1500 ms incorrectly fade from a fire
 * that never happened, including while spectating.
 *
 * Wrap only the fade path's float-to-int call. Run the original conversion
 * first so its x87 operand is consumed, then return zero if burnTime is
 * still zero. The existing elapsed <= 0 check skips drawing. Active fire
 * bypasses this call and genuine nonzero burn timestamps keep their fade.
 *
 * The helpers occupy function-alignment padding. Their return address is
 * the relocated address of 0x12333, allowing ECX-relative access to the
 * float at RVA 0xda0380 without adding absolute operands or relocations.
 * ECX is volatile, and the original caller immediately tests EAX again.
 * Keep all three rules enabled together.
 */
static const code_rule_t patch_burning_screen[] = {
    { CGAME, true, "burning screen: consume fade calculation", 0x11f61, 13,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0xe8, B4(0x9fd70u - 0x11f66u),       /* call __ftol2_sse */
        0x8b, 0x0c, 0x24,                    /* mov ecx,[esp] */
        0xe9, B4(0x12c41u - 0x11f6eu) } },
    { CGAME, true, "burning screen: require a recorded burn", 0x12c41, 12,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0x83, 0xb9, B4(0xda0380u - 0x12333u), 0x00,
        0x75, 0x02,                          /* jne return */
        0x33, 0xc0,                          /* xor eax,eax */
        0xc3 } },
    { CGAME, true, "burning screen: guard fade-out", 0x1232e, 5,
      { 0xe8, B4(0x9fd70u - 0x12333u) },
      { 0xe8, B4(0x11f61u - 0x12333u) } },
};

REGISTER_CODE_RULES(patch_burning_screen)
