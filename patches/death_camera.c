#include "patches.h"

const code_rule_t patch_death_camera[3] = {
    /* ClientThink_real: keep the deathTime != 0 guard, but omit the branch
     * that waits for deathTime + 3000. CopyToBodyQue runs once, followed by
     * clearing deathTime and entering the existing PM_SPECTATOR path.
     * The separate respawnTime check remains in force.
     */
    { GAME, true, "death camera: immediate spectator movement", 0x17faa, 6,
      { 0x0f, 0x8d, 0xe2, 0x00, 0x00, 0x00 },
      { 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 } },

    /* After CopyToBodyQue, release its pushed argument and jump to the
     * deathTime clear at 0x18088, skipping the 256-unit lift and trace.
     * The origin and ESI (the usercmd pointer) stay intact. No absolute
     * operands or relocation slots are changed by this relative jump.
     */
    { GAME, true, "death camera: omit spectator lift", 0x17ffa, 8,
      { 0xd9, 0x47, 0x1c, 0xf3, 0x0f, 0x10, 0x47, 0x14 },
      { 0x83, 0xc4, 0x04, 0xe9, B4(0x18088u - 0x18002u) } },

    /* The client may render PM_DEAD before receiving the server's spectator
     * state. Replace elapsed_ms * 15 with zero, preserving the death view's
     * initialization, collision checks, eye height and angle setup.
     */
    { CGAME, true, "death camera: omit slow rise", 0x33ff9, 7,
      { 0x8b, 0xc8, 0xc1, 0xe1, 0x04, 0x2b, 0xc8 },
      { 0x33, 0xc9, 0x90, 0x90, 0x90, 0x90, 0x90 } },
};
