#include "patches.h"

/* Fraction of the normal 400 units/second during the first three seconds.
 * Set to 0.1 for a 90% reduction, then rebuild the patcher.
 */
static const float death_spectator_speed_scale = 0.03f;

static code_rule_t patch_death_camera[] = {
    /* ClientThink_real: a positive deathTime still needs CopyToBodyQue.
     * On entering spectator mode, replace it with a negative deadline so
     * later updates skip the body copy and enter PM_SPECTATOR directly.
     * Respawning resets deathTime through the original spawn code.
     */
    { GAME, true, "death camera: skip copied body", 0x17f99, 6,
      { 0x0f, 0x84, B4(0x18092u - 0x17f9fu) },
      { 0x0f, 0x8e, B4(0x1809fu - 0x17f9fu) } },
    { GAME, true, "death camera: immediate spectator movement", 0x17f9f, 5,
      { 0x05, B4(3000u) },
      { 0xe9, B4(0x17fb0u - 0x17fa4u) } },

    /* After CopyToBodyQue, release its pushed argument and jump to the
     * deadline helper instead of the 256-unit lift and trace. The origin
     * and ESI (the usercmd pointer) stay intact.
     */
    { GAME, true, "death camera: omit spectator lift", 0x17ffa, 8,
      { 0xd9, 0x47, 0x1c, 0xf3, 0x0f, 0x10, 0x47, 0x14 },
      { 0x83, 0xc4, 0x04, 0xe9, B4(0x1800bu - 0x18002u) } },

    /* Reuse the lift's relocated operand for a level.time reader. Keeping
     * the operand at 0x18004 lets the existing PE relocation rebase it.
     * All other helper instructions use relative calls or client offsets.
     */
    { GAME, true, "death camera: server time helper", 0x18002, 6,
      { 0xdc, 0x05, B4(0x200a99a8u) },
      { 0x8b, 0x0d, B4(0x215b89e0u) } },
    { GAME, true, "death camera: server time return", 0x18008, 3,
      { 0x83, 0xc4, 0x04 },
      { 0xc3, 0x90, 0x90 } },

    /* In the skipped trace space, set deathTime = -(level.time + 3000).
     * Jump past the original clear and guard into the spectator setup.
     * This starts the interval on entry rather than on the killing hit.
     */
    { GAME, true, "death camera: start slowdown deadline", 0x1800b, 13,
      { 0x8d, 0x44, 0x24, 0x10, 0x50, 0xd9, 0x5c,
        0x24, 0x40, 0x8d, 0x4c, 0x24, 0x2c },
      { 0xe8, B4(0x18002u - 0x18010u),
        0x81, 0xc1, B4(3000u), 0xf7, 0xd9 } },
    { GAME, true, "death camera: retain slowdown deadline", 0x18018, 8,
      { 0x51, 0x8d, 0x77, 0x14, 0x8d, 0x94, 0x24, 0x34 },
      { 0x89, 0x8f, B4(0x8f0u), 0xeb, 0x7f } },

    /* SpectatorThink normally overwrites ps.speed with 400 every update.
     * Its replacement helper keeps that default unless EF_DEAD is set
     * and the negative deathTime deadline has not expired. ps.speed is
     * sent to the client, so its existing prediction uses the same speed.
     * EAX/ECX/EDX are scratch here; memset follows with its arguments
     * already on the stack. EBX remains the gclient pointer.
     */
    { GAME, true, "death camera: select spectator speed", 0x17649, 7,
      { 0xc7, 0x43, 0x34, B4(400u) },
      { 0xe8, B4(0x18020u - 0x1764eu), 0x90, 0x90 } },
    { GAME, true, "death camera: normal spectator speed", 0x18020, 11,
      { 0x01, 0x00, 0x00, 0x56, 0xf3, 0x0f,
        0x11, 0x44, 0x24, 0x40, 0xf3 },
      { 0xb8, B4(400u), 0xf6, 0x43, 0x68, 0x01, 0x74, 0x1a } },
    { GAME, true, "death camera: check slowdown deadline", 0x1802b, 12,
      { 0x0f, 0x10, 0x47, 0x18, 0x52, 0xb8,
        0x01, 0x00, 0x00, 0x00, 0x83, 0xc9 },
      { 0x8b, 0x93, B4(0x8f0u), 0x85, 0xd2,
        0x79, 0x10, 0xf7, 0xda } },
    { GAME, true, "death camera: expire spectator slowdown", 0x18037, 9,
      { 0xff, 0x8d, 0x54, 0x24, 0x44, 0xf3, 0x0f, 0x11, 0x44 },
      { 0xe8, B4(0x18002u - 0x1803cu), 0x39, 0xd1, 0x7d, 0x05 } },

    /* The client may render PM_DEAD before receiving the server's spectator
     * state. Replace elapsed_ms * 15 with zero, preserving the death view's
     * initialization, collision checks, eye height and angle setup.
     */
    { CGAME, true, "death camera: omit slow rise", 0x33ff9, 7,
      { 0x8b, 0xc8, 0xc1, 0xe1, 0x04, 0x2b, 0xc8 },
      { 0x33, 0xc9, 0x90, 0x90, 0x90, 0x90, 0x90 } },

    /* Final part of the speed helper: mov eax, configured speed; store;
     * ret. Keep this rule last so startup can fill its immediate below.
     */
    { GAME, true, "death camera: reduced spectator speed", 0x18040, 16,
      { 0x24, 0x48, 0xe8, 0x89, 0x4c, 0x01, 0x00, 0xf3,
        0x0f, 0x10, 0x8c, 0x24, 0x4c, 0x01, 0x00, 0x00 },
      { 0xb8, B4(0u), 0x89, 0x43, 0x34, 0xc3,
        0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90 } },
};

PATCH_CONSTRUCTOR(configure_death_spectator_speed)
{
    /* C17 requires the float conversion outside the static initializer. */
    code_rule_t *speed = &patch_death_camera[ARRAY_SIZE(patch_death_camera) - 1];
    put32(speed->replacement + 1,
          (uint32_t)(400.0f * death_spectator_speed_scale));
}

REGISTER_CODE_RULES(patch_death_camera)
