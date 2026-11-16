#include "patches.h"

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* weapon_hrifle / WP_HEAVYRIFLE is the shotgun. Edit these tuning values
 * and rebuild; use the resulting game DLL on the server and cgame DLL on
 * clients so authoritative shots and prediction use the same settings.
 * Base damage, ammo capacity and other config/script values are not patched.
 * Primary pellets receive the distance multiplier below on the server.
 */
/* #define SHOTGUN_PELLETS               11 */ /*OG*/
#define SHOTGUN_PELLETS               25
#define SHOTGUN_PRIMARY_FIRE_INTERVAL_MS    800
/* #define SHOTGUN_SECONDARY_FIRE_INTERVAL_MS  600 */ /*OG*/
#define SHOTGUN_SECONDARY_FIRE_INTERVAL_MS  600
#define SHOTGUN_SHELLS_PER_RELOAD     2
#define SHOTGUN_SHELL_RELOAD_MS       820
#define SHOTGUN_LAST_SHELL_RELOAD_MS  2020
#define SHOTGUN_RELOAD_FINISH_MS      50
#define SHOTGUN_KICK_DURATION_MS      40

/* Linear falloff between these ranges; 100 percent disables attenuation. */
#define SHOTGUN_FALLOFF_START_UNITS   100
#define SHOTGUN_FALLOFF_END_UNITS     400
#define SHOTGUN_FALLOFF_MIN_PERCENT   25

/* static const double shotgun_spread = 1600.0; */
static const double shotgun_spread = 700.0;
static const float shotgun_kick_pitch = 0.1f;
static const float shotgun_kick_yaw = 0.05f;

/* Every secondary shot that fires pushes the shooter straight
 * away from the aim direction by this many units per second, like a rocket
 * jump: aiming at the floor launches upwards, aiming ahead pushes back.
 * For comparison, a regular jump is 270 and a Quake 3 rocket jump ~500.
 * Like retail knockback, it then suspends ground friction for
 * SHOTGUN_PUSH_KNOCKBACK_MS unless another movement timer is running.
 */
static const float shotgun_push_velocity = 500.0f;
#define SHOTGUN_PUSH_KNOCKBACK_MS     200

/* The server initializes its pellet loop with lea esi,[ebx+imm8], EBX=0.
 * A positive signed byte also prevents its decrement loop wrapping at 0.
 */
_Static_assert(SHOTGUN_PELLETS >= 1 && SHOTGUN_PELLETS <= 127,
               "Shotgun pellet count must be between 1 and 127");
/* Reload comparisons use signed-byte immediates in both DLLs. */
_Static_assert(SHOTGUN_SHELLS_PER_RELOAD >= 1 && SHOTGUN_SHELLS_PER_RELOAD <= 127,
               "Shotgun shells per reload must be between 1 and 127");
_Static_assert(SHOTGUN_PRIMARY_FIRE_INTERVAL_MS > 0 &&
               SHOTGUN_PRIMARY_FIRE_INTERVAL_MS <= INT32_MAX &&
               SHOTGUN_SECONDARY_FIRE_INTERVAL_MS > 0 &&
               SHOTGUN_SECONDARY_FIRE_INTERVAL_MS <= INT32_MAX,
               "Shotgun firing intervals must be positive signed 32-bit values");
_Static_assert(SHOTGUN_SHELL_RELOAD_MS > 0 &&
               SHOTGUN_LAST_SHELL_RELOAD_MS > 0 && SHOTGUN_RELOAD_FINISH_MS >= 0 &&
               SHOTGUN_KICK_DURATION_MS > 0, "Invalid shotgun timing");
_Static_assert(SHOTGUN_FALLOFF_START_UNITS >= 0 &&
               SHOTGUN_FALLOFF_END_UNITS > SHOTGUN_FALLOFF_START_UNITS &&
               SHOTGUN_FALLOFF_END_UNITS <= INT32_MAX &&
               SHOTGUN_FALLOFF_MIN_PERCENT >= 1 &&
               SHOTGUN_FALLOFF_MIN_PERCENT <= 100,
               "Invalid shotgun falloff range or minimum percentage");
_Static_assert(SHOTGUN_PUSH_KNOCKBACK_MS >= 1 &&
               SHOTGUN_PUSH_KNOCKBACK_MS <= INT32_MAX,
               "Shotgun push knockback time must be a positive signed 32-bit value");
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
               sizeof(double) == 8 && DBL_MANT_DIG == 53,
               "Shotgun constants require IEEE binary32 and binary64");

/* reigw: g_weapon.c ShotgunPattern (game 0x36810), cg_weapons.c
 * CG_ShotgunPattern (cgame 0x3b360), and bg_pmove.c's PM_HeavyRifleWeapon
 * (0x13850 / 0x5070), PM_BeginShellReload (0x136d0 / 0x4ef0) and
 * PM_FinishShell (0x13790 / 0x4fb0).
 *
 * The original floats are pooled: the server's 700.0 is also used by
 * fire_fuelgel, and recoil constants are shared by other weapons. Put
 * private copies in guarded, unreachable function-alignment padding and
 * redirect only the shotgun's readers. Each operand retains its original
 * HIGHLOW relocation at the same offset, so rebasing still works.
 */
enum { GAME_SPREAD, CGAME_SPREAD, GAME_KICK, CGAME_KICK, GAME_FALLOFF };
static code_rule_t patch_shotgun_constants[] = {
    { GAME, true, "shotgun: private spread", 0x36808, 8,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }, { 0 } },
    { CGAME, true, "shotgun: private spread", 0x3b498, 8,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }, { 0 } },
    { GAME, true, "shotgun: private pitch and yaw kick", 0x13788, 8,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }, { 0 } },
    { CGAME, true, "shotgun: private pitch and yaw kick", 0x5368, 8,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }, { 0 } },
    /* Padding after Weapon_HandGrenade_Fire, before DistanceSquared. */
    { GAME, true, "shotgun: private falloff constants", 0x34b92, 12,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }, { 0 } },
};

/* Preserve the entire mov [register+weaponTime],imm32 as its byte guard. */
#define SHOTGUN_TIME(module, label, rva, modrm, stock, value) \
    { module, true, "shotgun: " label, rva, 7, \
      { 0xc7, modrm, 0x2c, B4(stock) }, \
      { 0xc7, modrm, 0x2c, B4(value) } }

static const code_rule_t patch_shotgun_instructions[] = {
    /* ShotgunPattern and CG_ShotgunPattern must generate identical traces. */
    { GAME, true, "shotgun: pellet count", 0x3684d, 3,
      { 0x8d, 0x73, 11 }, { 0x8d, 0x73, SHOTGUN_PELLETS } },
    { CGAME, true, "shotgun: pellet count", 0x3b399, 5,
      { 0xbf, B4(11) }, { 0xbf, B4(SHOTGUN_PELLETS) } },
    { GAME, true, "shotgun: horizontal spread", 0x36878, 6,
      { 0xdc, 0x0d, B4(0x200aa088u) }, { 0xdc, 0x0d, B4(0x20036808u) } },
    { GAME, true, "shotgun: vertical spread", 0x36890, 6,
      { 0xdc, 0x0d, B4(0x200aa088u) }, { 0xdc, 0x0d, B4(0x20036808u) } },
    { CGAME, true, "shotgun: horizontal spread", 0x3b3c9, 6,
      { 0xdc, 0x0d, B4(0x300b5020u) }, { 0xdc, 0x0d, B4(0x3003b498u) } },
    { CGAME, true, "shotgun: vertical spread", 0x3b3e2, 6,
      { 0xdc, 0x0d, B4(0x300b5020u) }, { 0xdc, 0x0d, B4(0x3003b498u) } },

    /* Both attacks retain the same recoil settings. */
    { GAME, true, "shotgun: kick duration", 0x13b02, 10,
      { 0xc7, 0x81, B4(0x32c), B4(40) },
      { 0xc7, 0x81, B4(0x32c), B4(SHOTGUN_KICK_DURATION_MS) } },
    { CGAME, true, "shotgun: kick duration", 0x5327, 10,
      { 0xc7, 0x81, B4(0x32c), B4(40) },
      { 0xc7, 0x81, B4(0x32c), B4(SHOTGUN_KICK_DURATION_MS) } },
    { GAME, true, "shotgun: pitch kick", 0x13ae3, 8,
      { 0xf3, 0x0f, 0x10, 0x05, B4(0x200a9d74u) },
      { 0xf3, 0x0f, 0x10, 0x05, B4(0x20013788u) } },
    { GAME, true, "shotgun: yaw kick", 0x13b18, 8,
      { 0xf3, 0x0f, 0x10, 0x05, B4(0x200a9e38u) },
      { 0xf3, 0x0f, 0x10, 0x05, B4(0x2001378cu) } },
    { CGAME, true, "shotgun: pitch kick", 0x5308, 8,
      { 0xf3, 0x0f, 0x10, 0x05, B4(0x300b4b58u) },
      { 0xf3, 0x0f, 0x10, 0x05, B4(0x30005368u) } },
    { CGAME, true, "shotgun: yaw kick", 0x533d, 8,
      { 0xf3, 0x0f, 0x10, 0x05, B4(0x300b4c50u) },
      { 0xf3, 0x0f, 0x10, 0x05, B4(0x3000536cu) } },

    /* Firepower upgrades still scale the shell reload times afterwards.
     * The long animation runs for the last batch or the last reserve ammo.
     */
    SHOTGUN_TIME(GAME, "shell reload", 0x136fc, 0x40, 820, SHOTGUN_SHELL_RELOAD_MS),
    SHOTGUN_TIME(CGAME, "shell reload", 0x4f1c, 0x40, 820, SHOTGUN_SHELL_RELOAD_MS),
    SHOTGUN_TIME(GAME, "last shell reload", 0x13726, 0x41, 2020, SHOTGUN_LAST_SHELL_RELOAD_MS),
    SHOTGUN_TIME(CGAME, "last shell reload", 0x4f46, 0x41, 2020, SHOTGUN_LAST_SHELL_RELOAD_MS),
    SHOTGUN_TIME(GAME, "reload finish", 0x137c3, 0x42, 50, SHOTGUN_RELOAD_FINISH_MS),
    SHOTGUN_TIME(CGAME, "reload finish", 0x4fe3, 0x42, 50, SHOTGUN_RELOAD_FINISH_MS),
};

/* PM_HeavyRifleWeapon retains its resolved attack in EBP (0 = primary,
 * 1 = secondary), including primary precedence when both buttons are down
 * or a delayed primary completes. At the cooldown site ESI is the
 * playerState, and EAX is scratch before the PM_CheckNoAmmo tail call.
 *
 * In padding after PM_FillClip (game 0x13381 / cgame 0x4bb1), compute:
 *   weaponTime += primary + secondary_flag * (secondary - primary);
 * The helper preserves EBP and ESI. Its relative call needs no relocation.
 * The call site enters the shotgun push below, which tail-jumps here.
 * Keep each helper, the push fragments and the call enabled together.
 */
#define SHOTGUN_COOLDOWN(module, helper, entry, site) \
    { module, true, "shotgun: attack cooldown helper", helper, 15, \
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, \
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }, \
      { 0x69, 0xc5, B4((uint32_t)(SHOTGUN_SECONDARY_FIRE_INTERVAL_MS - \
                                  SHOTGUN_PRIMARY_FIRE_INTERVAL_MS)), \
        0x05, B4(SHOTGUN_PRIMARY_FIRE_INTERVAL_MS), 0x01, 0x46, 0x2c, 0xc3 } }, \
    { module, true, "shotgun: separate attack cooldowns", site, 7, \
      { 0x81, 0x46, 0x2c, B4(1200) }, \
      { 0xe8, B4(entry - (site + 5u)), 0x90, 0x90 } }

/* Unused int3 padding for the push fragments, in chaining order. The first
 * slot of each list is the entry the cooldown call site jumps to.
 */
#define SHOTGUN_PUSH_GAME_ENTRY  0xe601u
#define SHOTGUN_PUSH_CGAME_ENTRY 0x2132u

static const code_rule_t patch_shotgun_cooldowns[] = {
    SHOTGUN_COOLDOWN(GAME, 0x13381u, SHOTGUN_PUSH_GAME_ENTRY, 0x13b2au),
    SHOTGUN_COOLDOWN(CGAME, 0x4bb1u, SHOTGUN_PUSH_CGAME_ENTRY, 0x534fu),
};

/* The push runs at the cooldown call site of PM_HeavyRifleWeapon, so it
 * runs in both DLLs' pmove and client prediction matches the server. There
 * ESI is the playerState and EBX the PM_AmmoForAttack result: nonzero only
 * when the shot actually fired (the dry-fire path also reaches the site).
 * EBP is the resolved attack (0 = primary, 1 = secondary, as for the
 * cooldown helper); only secondary shots push. EAX, ECX and EDX are free
 * before the PM_CheckNoAmmo tail call.
 *
 * pml.forward cannot be reused: PM_WalkMove and PM_AirMove flatten it
 * before PM_Weapon runs. Recompute the aim direction from viewangles
 * (pitch at ps + 0x98, yaw at + 0x9c) like AngleVectors does:
 *   forward = (cos p * cos y, cos p * sin y, -sin p)
 *   velocity (ps + 0x20) -= forward * shotgun_push_velocity
 * then, like G_Damage's knockback, if pm_time (ps + 0x10) is 0, set it
 * and PMF_TIME_KNOCKBACK (0x40 in pm_flags, ps + 0x0c) so ground friction
 * does not swallow the push right away. Both float constants are pushed
 * as immediates, so the code is position independent and needs no new
 * relocations. The x87 stack is balanced and only ESP changes temporarily.
 *
 * No padding slot is large enough for the whole helper, so the packer
 * below spreads whole instructions over int3 padding slots, chaining
 * them with jmp rel32.
 */
typedef struct {
    unsigned char bytes[7];
    size_t size;
    /* The trailing rel32 targets the cooldown helper. */
    bool to_cooldown;
} shotgun_insn_t;

typedef struct {
    uint32_t rva;
    size_t size;
} shotgun_slot_t;

static const shotgun_slot_t shotgun_push_game_slots[] = {
    { SHOTGUN_PUSH_GAME_ENTRY, 15 }, { 0xf062, 14 }, { 0x10222, 14 },
    { 0x10431, 15 }, { 0x10972, 14 }, { 0x10f31, 15 }, { 0x10fe2, 14 },
    { 0x11bb1, 15 }, { 0x13682, 14 }, { 0x16211, 15 }, { 0x163f2, 14 },
    { 0x17702, 14 }, { 0x177d1, 15 }, { 0x186f2, 14 },
    { 0x18821, 15 },
};
static const shotgun_slot_t shotgun_push_cgame_slots[] = {
    { SHOTGUN_PUSH_CGAME_ENTRY, 14 }, { 0x2422, 14 }, { 0x7c12, 14 },
    { 0x8962, 14 }, { 0x8a82, 14 }, { 0x8d32, 14 }, { 0xb8d1, 15 },
    { 0xc021, 15 }, { 0xc592, 14 }, { 0xca92, 14 }, { 0xd0d2, 14 },
    { 0xdd81, 15 }, { 0xf401, 15 }, { 0xfd71, 15 },
};

static code_rule_t patch_shotgun_push_game[ARRAY_SIZE(shotgun_push_game_slots)];
static code_rule_t patch_shotgun_push_cgame[ARRAY_SIZE(shotgun_push_cgame_slots)];

/* Place the instructions in order, keeping room for the chaining jmp in
 * every slot but the last. Each rule guards its complete int3 slot and
 * leaves the unused tail as int3. Every listed slot must end up used, so
 * a stale slot list fails loudly instead of silently registering rules.
 */
static void pack_shotgun_push(module_t module, const shotgun_slot_t *slots,
                              size_t slot_count, code_rule_t *rules,
                              const shotgun_insn_t *insns, size_t insn_count,
                              uint32_t cooldown)
{
    size_t slot = 0, used = 0;

    for (size_t s = 0; s < slot_count; ++s) {
        rules[s] = (code_rule_t){ module, true, "shotgun push: fragment",
                                  slots[s].rva, slots[s].size, { 0 }, { 0 } };
        memset(rules[s].expected, 0xcc, slots[s].size);
        memset(rules[s].replacement, 0xcc, slots[s].size);
    }
    for (size_t i = 0; i < insn_count; ++i) {
        size_t reserve = i + 1 < insn_count ? 5 : 0;
        unsigned char *out;

        if (used + insns[i].size + reserve > slots[slot].size) {
            uint32_t at = slots[slot].rva + (uint32_t)used;
            if (slot + 1 == slot_count)
                break;
            rules[slot].replacement[used] = 0xe9;
            put32(rules[slot].replacement + used + 1, slots[slot + 1].rva - (at + 5u));
            ++slot;
            used = 0;
            if (insns[i].size + reserve > slots[slot].size)
                break;
        }
        out = rules[slot].replacement + used;
        memcpy(out, insns[i].bytes, insns[i].size);
        used += insns[i].size;
        if (insns[i].to_cooldown)
            put32(out + insns[i].size - 4, cooldown - (slots[slot].rva + (uint32_t)used));
        if (i + 1 == insn_count && slot + 1 == slot_count)
            return;
    }
    fprintf(stderr, "shotgun push: the %s padding slots do not fit the helper exactly\n",
            module == GAME ? "game" : "cgame");
    exit(EXIT_FAILURE);
}

PATCH_CONSTRUCTOR(configure_shotgun_push)
{
    uint32_t push, deg2rad;
    const float radians_per_degree = 0.017453292519943295f;
    shotgun_insn_t insns[] = {
        { { 0x85, 0xdb }, 2, false },                  /* test ebx,ebx */
        { { 0x0f, 0x84, 0, 0, 0, 0 }, 6, true },       /* jz cooldown (no shot) */
        { { 0x85, 0xed }, 2, false },                  /* test ebp,ebp */
        { { 0x0f, 0x84, 0, 0, 0, 0 }, 6, true },       /* jz cooldown (primary) */
        { { 0x68 }, 5, false },                        /* push deg2rad */
        { { 0x68 }, 5, false },                        /* push velocity */
        { { 0xd9, 0x86, B4(0x98) }, 6, false },        /* fld [esi+0x98] (pitch) */
        { { 0xd8, 0x4c, 0x24, 0x04 }, 4, false },      /* fmul [esp+4] */
        { { 0xd9, 0xfb }, 2, false },                  /* fsincos: cp, sp */
        { { 0xd9, 0x86, B4(0x9c) }, 6, false },        /* fld [esi+0x9c] (yaw) */
        { { 0xd8, 0x4c, 0x24, 0x04 }, 4, false },      /* fmul [esp+4] */
        { { 0xd9, 0xfb }, 2, false },                  /* fsincos: cy, sy, cp, sp */
        { { 0xd9, 0x04, 0x24 }, 3, false },            /* fld [esp]: v, cy, sy, cp, sp */
        { { 0x83, 0xc4, 0x08 }, 3, false },            /* add esp,8 */
        { { 0xdc, 0xcb }, 2, false },                  /* fmul st3,st0: v*cp */
        { { 0xde, 0xcc }, 2, false },                  /* fmulp st4,st0: cy, sy, vcp, vsp */
        { { 0xd8, 0xca }, 2, false },                  /* fmul st0,st2: vcp*cy */
        { { 0xd8, 0x6e, 0x20 }, 3, false },            /* fsubr [esi+0x20] */
        { { 0xd9, 0x5e, 0x20 }, 3, false },            /* fstp [esi+0x20]: sy, vcp, vsp */
        { { 0xde, 0xc9 }, 2, false },                  /* fmulp st1,st0: vcp*sy, vsp */
        { { 0xd8, 0x6e, 0x24 }, 3, false },            /* fsubr [esi+0x24] */
        { { 0xd9, 0x5e, 0x24 }, 3, false },            /* fstp [esi+0x24]: vsp */
        { { 0xd8, 0x46, 0x28 }, 3, false },            /* fadd [esi+0x28] */
        { { 0xd9, 0x5e, 0x28 }, 3, false },            /* fstp [esi+0x28]: empty */
        { { 0x83, 0x7e, 0x10, 0x00 }, 4, false },      /* cmp dword [esi+0x10],0 */
        { { 0x0f, 0x85, 0, 0, 0, 0 }, 6, true },       /* jne cooldown */
        { { 0xc7, 0x46, 0x10, B4((uint32_t)SHOTGUN_PUSH_KNOCKBACK_MS) }, 7, false },
        { { 0x83, 0x4e, 0x0c, 0x40 }, 4, false },      /* or [esi+0xc],0x40 */
        { { 0xe9, 0, 0, 0, 0 }, 5, true },             /* jmp cooldown */
    };

    /* Write the floats' IEEE bits as little-endian push immediates. */
    memcpy(&push, &shotgun_push_velocity, sizeof(push));
    memcpy(&deg2rad, &radians_per_degree, sizeof(deg2rad));
    put32(insns[4].bytes + 1, deg2rad);
    put32(insns[5].bytes + 1, push);

    pack_shotgun_push(GAME, shotgun_push_game_slots, ARRAY_SIZE(shotgun_push_game_slots),
                      patch_shotgun_push_game, insns, ARRAY_SIZE(insns), 0x13381u);
    pack_shotgun_push(CGAME, shotgun_push_cgame_slots, ARRAY_SIZE(shotgun_push_cgame_slots),
                      patch_shotgun_push_cgame, insns, ARRAY_SIZE(insns), 0x4bb1u);
}

/* Redirect only ShotgunPellet's G_Damage call (0x367d6). Its existing
 * damage selection still distinguishes players from NPCs; the incendiary
 * secondary never enters this path. On helper entry the original damage
 * argument is at ESP + 0x18, the hit-point pointer at ESP + 0x14 and the
 * copied trace start at ESP + 0x44. Tail-calling G_Damage keeps its stack
 * arguments, return address and caller cleanup intact.
 *
 * Compute distance with the retail DistanceSquared (EAX/ECX, ST0 return),
 * then clamp 1 - (distance - start) * slope to [minimum, 1]. __ftol2_sse
 * truncates the scaled damage using the game's normal conversion helper;
 * G_Damage retains its usual upgrades, armor and minimum-damage handling.
 * Only EAX/ECX/EDX and x87 scratch are used; the x87 stack is balanced.
 *
 * The fragments occupy unreachable function-alignment padding near the
 * weapon handlers. Calls and jumps are relative; call/pop at 0x33cc1
 * obtains 0x33cc6 and derives the private constants' address from it.
 * Keep all fragments, the constants and the hook enabled together.
 */
static const code_rule_t patch_shotgun_falloff[] = {
    { GAME, true, "shotgun falloff: hit distance arguments", 0x34f33, 10,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0x8d, 0x44, 0x24, 0x44, 0x8b, 0x4c, 0x24, 0x14,
        0xeb, 0x26 } },
    { GAME, true, "shotgun falloff: measure hit distance", 0x34f63, 12,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0xe8, B4(0x34ba0u - 0x34f68u), 0xd9, 0xfa,
        0xe9, B4(0x33cc1u - 0x34f6fu) } },
    { GAME, true, "shotgun falloff: private constant base", 0x33cc1, 14,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0xe8, 0, 0, 0, 0, 0x59, 0x8d, 0x89,
        B4(0x34b92u - 0x33cc6u), 0xeb, 0x93 } },
    { GAME, true, "shotgun falloff: calculate linear multiplier", 0x33c62, 14,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0xd8, 0x21, 0xd8, 0x49, 0x04, 0xd9, 0xe8, 0xde, 0xe1,
        0xe9, B4(0x35563u - 0x33c70u) } },
    { GAME, true, "shotgun falloff: cap multiplier at one", 0x35563, 10,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0xd9, 0xe8, 0xdb, 0xf1, 0xdb, 0xc1, 0xdd, 0xd9, 0xeb, 0x26 } },
    { GAME, true, "shotgun falloff: clamp to minimum multiplier", 0x35593, 11,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0xd9, 0x41, 0x08, 0xdb, 0xf1, 0xda, 0xc1, 0xdd, 0xd9, 0xeb, 0x65 } },
    { GAME, true, "shotgun falloff: scale pellet damage", 0x35603, 9,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0xda, 0x4c, 0x24, 0x18, 0xe9, B4(0x36052u - 0x3560cu) } },
    { GAME, true, "shotgun falloff: apply damage", 0x36052, 14,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0xe8, B4(0x94c30u - 0x36057u), 0x89, 0x44, 0x24, 0x18,
        0xe9, B4(0x21bc0u - 0x36060u) } },
    { GAME, true, "shotgun falloff: pellet damage hook", 0x367d6, 5,
      { 0xe8, B4(0x21bc0u - 0x367dbu) },
      { 0xe8, B4(0x34f33u - 0x367dbu) } },
};

/* PM_LoadShell (game 0x13690 / cgame 0x4eb0) takes the weapon in ESI.
 * Replace it, including six padding bytes, with:
 *   count = min(SHOTGUN_SHELLS_PER_RELOAD, clipsize - clip, reserve);
 *   if (count > 0) { reserve -= count; clip += count; }
 *
 * Preserve EDI and the original pm reader's relocation at entry + 3.
 * call/pop obtains entry + 21, so the clipsize reader uses a relative
 * displacement and needs no new relocation. EAX is the reserve pointer;
 * the clip is 0x80 bytes further on. The complete 64 bytes are guarded
 * together, although individual rules split instructions at their limit.
 */
#define SHOTGUN_LOAD_SHELL(module, rva, pm, clipsize) \
    { module, true, "shotgun: load shells, part 1", rva, 16, \
      { 0x57, 0x8b, 0x3d, B4(pm), 0x8b, 0x07, \
        0x8b, 0x94, 0xb0, 0x64, 0x01, 0x00, 0x00 }, \
      { 0x57, 0x8b, 0x3d, B4(pm), 0x8b, 0x07, \
        0x8d, 0x84, 0xb0, 0x64, 0x01, 0x00, 0x00 } }, \
    { module, true, "shotgun: load shells, part 2", rva + 16, 16, \
      { 0x8d, 0x84, 0xb0, 0x64, 0x01, 0x00, 0x00, 0xb9, \
        0x01, 0x00, 0x00, 0x00, 0x3b, 0xd1, 0x7d, 0x06 }, \
      { 0xe8, 0x00, 0x00, 0x00, 0x00, 0x5f, 0x8b, 0xbf, \
        B4(clipsize - (rva + 21u)), 0x2b, 0xb8, 0x80, 0x00 } }, \
    { module, true, "shotgun: load shells, part 3", rva + 32, 16, \
      { 0x8b, 0xca, 0x85, 0xd2, 0x74, 0x12, 0x29, 0x08, \
        0x8b, 0x17, 0x01, 0x8c, 0xb2, 0xe4, 0x01, 0x00 }, \
      { 0x00, 0x00, 0xb9, B4(SHOTGUN_SHELLS_PER_RELOAD), \
        0x3b, 0xcf, 0x0f, 0x4f, 0xcf, 0x3b, 0x08, 0x0f, 0x4f } }, \
    { module, true, "shotgun: load shells, part 4", rva + 48, 16, \
      { 0x00, 0x8d, 0x84, 0xb2, 0xe4, 0x01, 0x00, 0x00, \
        0x5f, 0xc3, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }, \
      { 0x08, 0x85, 0xc9, 0x7e, 0x08, 0x29, 0x08, 0x01, \
        0x88, 0x80, 0x00, 0x00, 0x00, 0x5f, 0xc3, 0x90 } }

/* PM_BeginShellReload's last-animation test must recognize a whole batch:
 * clip >= clipsize - batch || reserve <= batch. EDI is only tested for
 * zero later, so incrementing the flag suffices when reserve runs out.
 */
#define SHOTGUN_LAST_BATCH(module, rva) \
    { module, true, "shotgun: last reload batch, part 1", rva, 16, \
      { 0x33, 0xd2, 0x49, 0x39, 0x88, 0x10, 0x02, 0x00, \
        0x00, 0x57, 0x0f, 0x94, 0xc2, 0x83, 0xb8, 0x90 }, \
      { 0x83, 0xe9, SHOTGUN_SHELLS_PER_RELOAD, 0x33, 0xd2, \
        0x39, 0x88, 0x10, 0x02, 0x00, 0x00, 0x57, 0x0f, 0x9d, 0xc2, 0x83 } }, \
    { module, true, "shotgun: last reload batch, part 2", rva + 16, 13, \
      { 0x01, 0x00, 0x00, 0x01, 0x8b, 0xfa, 0x75, \
        0x05, 0xbf, 0x01, 0x00, 0x00, 0x00 }, \
      { 0xb8, 0x90, 0x01, 0x00, 0x00, SHOTGUN_SHELLS_PER_RELOAD, \
        0x8b, 0xfa, 0x7f, 0x01, 0x47, 0x90, 0x90 } }

static const code_rule_t patch_shotgun_reload[] = {
    SHOTGUN_LOAD_SHELL(GAME, 0x13690u, 0x215bc30cu, 0x142b3ccu),
    SHOTGUN_LOAD_SHELL(CGAME, 0x4eb0u, 0x3116c3acu, 0xacc96cu),
    SHOTGUN_LAST_BATCH(GAME, 0x136dfu),
    SHOTGUN_LAST_BATCH(CGAME, 0x4effu),
};

PATCH_CONSTRUCTOR(configure_shotgun_constants)
{
    uint64_t spread;
    uint32_t pitch, yaw;
    const float falloff[] = {
        (float)SHOTGUN_FALLOFF_START_UNITS,
        (1.0f - SHOTGUN_FALLOFF_MIN_PERCENT / 100.0f) /
            (SHOTGUN_FALLOFF_END_UNITS - SHOTGUN_FALLOFF_START_UNITS),
        SHOTGUN_FALLOFF_MIN_PERCENT / 100.0f,
    };

    /* Copy the floating-point representation without aliasing, then write
     * little-endian PE data regardless of the native patcher's endianness.
     */
    memcpy(&spread, &shotgun_spread, sizeof(spread));
    memcpy(&pitch, &shotgun_kick_pitch, sizeof(pitch));
    memcpy(&yaw, &shotgun_kick_yaw, sizeof(yaw));
    for (size_t i = GAME_SPREAD; i <= CGAME_SPREAD; ++i) {
        put32(patch_shotgun_constants[i].replacement, (uint32_t)spread);
        put32(patch_shotgun_constants[i].replacement + 4, (uint32_t)(spread >> 32));
    }
    for (size_t i = GAME_KICK; i <= CGAME_KICK; ++i) {
        put32(patch_shotgun_constants[i].replacement, pitch);
        put32(patch_shotgun_constants[i].replacement + 4, yaw);
    }
    for (size_t i = 0; i < ARRAY_SIZE(falloff); ++i) {
        uint32_t bits;
        memcpy(&bits, &falloff[i], sizeof(bits));
        put32(patch_shotgun_constants[GAME_FALLOFF].replacement + i * 4, bits);
    }
}

/* Keep the private constants and their readers registered together. */
REGISTER_CODE_RULES(patch_shotgun_constants)
REGISTER_CODE_RULES(patch_shotgun_instructions)
REGISTER_CODE_RULES(patch_shotgun_cooldowns)
REGISTER_CODE_RULES(patch_shotgun_push_game)
REGISTER_CODE_RULES(patch_shotgun_push_cgame)
REGISTER_CODE_RULES(patch_shotgun_falloff)
REGISTER_CODE_RULES(patch_shotgun_reload)
