#include "patches.h"

/* Apply all six rules together: the calculation and print hooks share EBX. */
static const code_rule_t patch_respawn_countdown[] = {
    /* CG_DrawDeathScreen: retain the unscaled remaining milliseconds in
     * EBX, which the localization and formatting calls preserve. Adding
     * 999 before the existing seconds conversion rounds positive times up.
     * Use the padding after CG_CenterPrintTimed, preserving every existing
     * absolute operand and PE relocation in the calculation.
     */
    { CGAME, true, "respawn countdown: remaining time helper", 0x107d3, 13,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0xe8, B4(0x9fd70u - 0x107d8u), 0x89, 0xc3,
        0x05, B4(999u), 0xc3 } },
    { CGAME, true, "respawn countdown: round up seconds", 0xd8bc, 5,
      { 0xe8, B4(0x9fd70u - 0xd8c1u) },
      { 0xe8, B4(0x107d3u - 0xd8c1u) } },
    { CGAME, true, "respawn countdown: omit extra decrement", 0xd8d1, 3,
      { 0x8d, 0x70, 0xff },
      { 0x89, 0xc6, 0x90 } },

    /* Keep EBX's milliseconds through the string copy. The print helper
     * supplies them in EAX and reconstructs the text pointer in EBX before
     * entering CG_CenterPrint's argument setup at 0x107f1. Its timed call
     * then expires at the respawn deadline instead of lingering for
     * cg_centertime after the last update. Y and character width retain
     * their two original stack arguments and cleanup.
     */
    { CGAME, true, "respawn countdown: retain remaining milliseconds", 0xd933, 4,
      { 0x8d, 0x5c, 0x24, 0x48 },
      { 0x90, 0x90, 0x90, 0x90 } },
    { CGAME, true, "respawn countdown: use deadline for text lifetime", 0xd939, 5,
      { 0xe8, B4(0x107e0u - 0xd93eu) },
      { 0xe8, B4(0xdac5u - 0xd93eu) } },
    /* Padding after CG_DrawDeathScreen: the return address and the two
     * pushed arguments put the original text buffer at ESP + 0x4c.
     */
    { CGAME, true, "respawn countdown: timed print helper", 0xdac5, 11,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0x89, 0xd8, 0x8d, 0x5c, 0x24, 0x4c,
        0xe9, B4(0x107f1u - 0xdad0u) } },
};

REGISTER_CODE_RULES(patch_respawn_countdown)
