#include "patches.h"

/* G_ToggleRTS (0x43150) only rejects TEAM_SPECTATOR. Dead players keep
 * their session team while SpectatorThink gives them PM_SPECTATOR (2),
 * and their entity health is reset to 1. Check ps.pm_type and EF_DEAD
 * before either team's RTS path; EF_DEAD also covers the first update
 * after death, before spectator movement starts.
 *
 * At 0x43172 EAX is ent->client, ECX is ent and ESI has been pushed.
 * Rejected commands use the original pop esi; ret at 0x4330c. Accepted
 * commands replay the session-team load and resume at 0x43178.
 * Helpers occupy unused int3 padding after returns and use only relative
 * jumps and client offsets, so no PE relocations need changing.
 * Keep all four rules enabled together.
 */
static const code_rule_t patch_rts_spectator[] = {
    { GAME, true, "RTS: reject spectator movement", 0x42df1, 15,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0x83, 0x78, 0x04, 0x02,                 /* cmp [eax+4],PM_SPECTATOR */
        0x0f, 0x84, B4(0x4330cu - 0x42dfbu),
        0xe9, B4(0x486a1u - 0x42e00u) } },
    { GAME, true, "RTS: reject death spectating", 0x486a1, 15,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0xf6, 0x40, 0x68, 0x01,                 /* test [eax+0x68],EF_DEAD */
        0x0f, 0x85, B4(0x4330cu - 0x486abu),
        0xe9, B4(0x4edd1u - 0x486b0u) } },
    { GAME, true, "RTS: resume session-team check", 0x4edd1, 11,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0x8b, 0x90, B4(0x874u),                /* mov edx,[eax+sessionTeam] */
        0xe9, B4(0x43178u - 0x4eddcu) } },
    { GAME, true, "RTS: guard the toggle command", 0x43172, 6,
      { 0x8b, 0x90, B4(0x874u) },
      { 0xe9, B4(0x42df1u - 0x43177u), 0x90 } },
};

REGISTER_CODE_RULES(patch_rts_spectator)
