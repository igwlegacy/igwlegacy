#include "patches.h"

/* Stock Q3 pmove can throw a player who lands straight down back up at
 * the speed they landed with. Spawning is the usual trigger: ClientSpawn
 * drops players a few units onto the floor with no horizontal speed.
 *
 * PM_GroundTrace (game 0x122a0 / cgame 0x3a50) declares a player landed
 * as soon as the floor is within 0.25 units below. When the last air
 * move ended inside that gap without touching the floor, PM_SlideMove
 * never clipped the fall, so ps.velocity is still e.g. (0, 0, -95). The
 * next PM_WalkMove clips it against the floor to (0, 0, +0.095), then
 * "doesn't decrease velocity on slopes" by normalizing and scaling it
 * back to the old length: (0, 0, +95). The kickoff check in the next
 * PM_GroundTrace then lifts the player off, and a lower hop can repeat
 * the same way.
 *
 * Clip ps.velocity against the ground plane on landing, as PM_SlideMove
 * does when its trace hits the floor, so both ways to land behave the
 * same. Landings that hit the floor are left as they are (the velocity
 * was already clipped), and slope transitions while walking still keep
 * their speed. Fall damage and landing events read
 * pml.previous_velocity, which this leaves alone.
 *
 * The landing block calls PM_CrashLand (game 0x11df0 / cgame 0x3570),
 * which reads neither ps.velocity nor registers. Redirect that call to
 * a helper that clips the velocity and then tail-jumps to PM_CrashLand.
 * At the call ESI is pm (its ps at +0), and the local trace_t that
 * pml.groundTrace was copied from sits at [ESP+0x1c] in both modules,
 * with plane.normal at +0x18. After the call and the saved ESI, that
 * normal is at [ESP+0x3c]. cgame still uses ESI after the call, so the
 * helper restores it. ECX and EDX are scratch, as they are for
 * PM_CrashLand.
 *
 * PM_ClipVelocity (game 0x10a40 / cgame 0x2200) takes in = EDX,
 * normal = ECX, out = ESI and the overbounce float on the stack, which
 * the caller pops. OVERCLIP is 1.001f (0x3f8020c5), as in PM_WalkMove.
 *
 * Padding runs are at most 15 bytes, so the helper spans three
 * unreachable int3 runs that each follow a ret, chained with jmp rel32.
 * It uses no absolute addresses and needs no new relocations.
 */
#define CC15 { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, \
               0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }
#define CC14 { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, \
               0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }
#define CC13 { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, \
               0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }
#define CC12 { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, \
               0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }

#define LANDING_BOUNCE(module, call, crash_land, clip_velocity, a, b, c) \
    { module, true, "landing bounce: clip velocity, part 1", a, 13, CC13, \
      { 0x56,                       /* push esi */ \
        0x8b, 0x36,                 /* mov esi,[esi]: ps */ \
        0x83, 0xc6, 0x20,           /* add esi,0x20: &ps->velocity */ \
        0x8b, 0xd6,                 /* mov edx,esi */ \
        0xe9, B4(b - (a + 13u)) } }, \
    { module, true, "landing bounce: clip velocity, part 2", b, 14, CC14, \
      { 0x8d, 0x4c, 0x24, 0x3c,     /* lea ecx,[esp+0x3c]: normal */ \
        0x68, B4(0x3f8020c5u),      /* push OVERCLIP */ \
        0xe9, B4(c - (b + 14u)) } }, \
    { module, true, "landing bounce: clip velocity, part 3", c, 12, CC12, \
      { 0xe8, B4(clip_velocity - (c + 5u)), \
        0x59,                       /* pop ecx: OVERCLIP */ \
        0x5e,                       /* pop esi: pm */ \
        0xe9, B4(crash_land - (c + 12u)) } }, \
    { module, true, "landing bounce: clip velocity on landing", call, 5, \
      { 0xe8, B4(crash_land - (call + 5u)) }, \
      { 0xe8, B4(a - (call + 5u)) } }

/* Server authority and client prediction must land the same way. */
static const code_rule_t patch_landing_bounce[] = {
    LANDING_BOUNCE(GAME, 0x124ecu, 0x11df0u, 0x10a40u,
                   0x3bd1u, 0x3c71u, 0x3de1u),
    LANDING_BOUNCE(CGAME, 0x3ca2u, 0x3570u, 0x2200u,
                   0x13371u, 0x13771u, 0x143a1u),
};

REGISTER_CODE_RULES(patch_landing_bounce)
