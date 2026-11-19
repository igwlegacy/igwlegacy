#include "patches.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Y in milliseconds before Firepower Upgrade scaling. The original
 * 3000 ms reload remains X; Z is its remaining recovery time. Install
 * matching game and cgame DLLs after changing this value and rebuilding.
 */
#define MUSKET_LOAD_MS 1100

_Static_assert(MUSKET_LOAD_MS > 0 && MUSKET_LOAD_MS < 3000,
               "Musket loading must leave a positive recovery time");

/* PM_Weapon normally lets secondary fire cancel WEAPON_RELOADING (4)
 * only when PM_AmmoForAttack returns positive ammo. The musket's empty
 * clip fails that check. Its reload after undeploying instead uses
 * WEAPON_RELOADING_UNDEPLOYED (11), which the original gate also skips.
 *
 * Replace the state comparison with a helper that leaves ZF set for
 * state 4, or state 11 on WP_LIGHTRIFLE (7). EAX remains playerState;
 * the original branch and secondary-button test still follow the call.
 * Helpers use guarded alignment padding and relative branches only.
 */
#define MUSKET_RELOAD_GATE(module, site, state, weapon, undeployed) \
    { module, true, "musket reload: check cancellation state", site, 7, \
      { 0x83, 0xb8, B4(0x94), 0x04 }, \
      { 0xe8, B4(state - (site + 5u)), 0x90, 0x90 } }, \
    { module, true, "musket reload: normal state helper", state, 15, \
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, \
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }, \
      { 0x83, 0xb8, B4(0x94), 0x04, 0x74, 0x05, \
        0xe9, B4(weapon - (state + 14u)), 0xc3 } }, \
    { module, true, "musket reload: weapon helper", weapon, 15, \
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, \
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }, \
      { 0x83, 0xb8, B4(0x90), 0x07, 0x75, 0x05, \
        0xe9, B4(undeployed - (weapon + 14u)), 0xc3 } }, \
    { module, true, "musket reload: undeployed state helper", undeployed, 8, \
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }, \
      { 0x83, 0xb8, B4(0x94), 0x0b, 0xc3 } }

/* hmg_reload.c routes its non-HMG cancellation ammo query here. Return
 * one for the musket regardless of clip/reserve ammo; tail-call the
 * original query for every other weapon, retaining its stack argument.
 * The caller's CDQ then selects WEAPON_READY (0) for the musket.
 * Cancellation clears weaponTime and skips PM_FinishReload. The earlier
 * loading check below determines whether the round has transferred yet.
 * Secondary fire can then perform the normal aiming action.
 */
#define MUSKET_CANCEL_AMMO(module, helper, ammo) \
    { module, true, "musket reload: allow cancellation without ammo", helper, 13, \
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, \
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }, \
      { 0x83, 0xf9, 0x07, 0x0f, 0x85, B4(ammo - (helper + 9u)), \
        0x6a, 0x01, 0x58, 0xc3 } }

/* Match server authority and client prediction, including scoped reloads. */
static const code_rule_t patch_musket_reload[] = {
    MUSKET_RELOAD_GATE(GAME, 0x13ff1u, 0x1e51u, 0x2051u, 0x12a26u),
    MUSKET_RELOAD_GATE(CGAME, 0x57efu, 0xfef1u, 0xffa1u, 0x41c6u),
    MUSKET_CANCEL_AMMO(GAME, 0x13273u, 0x134e0u),
    MUSKET_CANCEL_AMMO(CGAME, 0x4aa3u, 0x4d10u),
};

REGISTER_CODE_RULES(patch_musket_reload)

/* Check the loading milestone just after weaponTime is decremented and
 * clamped, before either weapon switching or secondary-fire cancellation.
 * Keep both reload states, weaponTime and weaponAnim untouched: the normal
 * reload animation and firing lock therefore last through Y + Z. At X the
 * original PM_FinishReload still handles the idle/deployment transition.
 * Its second PM_FillClip does nothing when the musket's clip is full.
 *
 * An empty musket transfers ammo when weaponTime <= Z. Scale Z by the
 * highest Firepower Upgrade's live sk_upgrade_reload_weap value, exactly
 * as PM_BeginReload scales X. Comparing the floating result with integer
 * weaponTime gives the same boundary as truncating it to milliseconds.
 * Any outstanding firing delay at reload start stays part of Y, as it
 * already extends X. No extra playerState field or client-only clock is
 * needed, so server authority and client prediction use the same state.
 *
 * Helpers use guarded, unreachable alignment padding. Branch destinations
 * are resolved after packing; call/pop addresses the upgrade table without
 * adding PE relocations. PUSHAD preserves PM_Weapon's registers and the
 * final MOV/CMP pair restores the replaced instructions and their flags.
 */
enum { LOAD_CHECK = 1, LOAD_COMPARE, LOAD_DONE, LOAD_LABELS };
enum { LOAD_PLAIN, LOAD_BRANCH, LOAD_ANCHOR, LOAD_FACTOR, LOAD_FILL };
typedef struct {
    unsigned char kind, size, label;
    unsigned char bytes[10];
} load_op_t;

#define LOAD_OP(...) \
    { LOAD_PLAIN, sizeof((unsigned char[]){ __VA_ARGS__ }), 0, { __VA_ARGS__ } }
#define LOAD_JCC(cc, label) { LOAD_BRANCH, 6, label, { 0x0f, cc } }

static const load_op_t musket_load_ops[] = {
    LOAD_OP(0x60, 0x8b, 0x0e),                     /* pushad; ecx=ps */
    LOAD_OP(0x83, 0xb9, B4(0x90), 0x07),           /* musket only */
    LOAD_JCC(0x85, LOAD_DONE),
    LOAD_OP(0x8b, 0x91, B4(0x94)),                 /* edx=weaponstate */
    LOAD_OP(0x83, 0xfa, 0x04),
    LOAD_JCC(0x84, LOAD_CHECK),
    LOAD_OP(0x83, 0xfa, 0x0b),
    LOAD_JCC(0x85, LOAD_DONE),
    { LOAD_PLAIN, 7, LOAD_CHECK,                    /* empty clip[7]? */
      { 0x83, 0xb9, B4(0x200), 0x00 } },
    LOAD_JCC(0x85, LOAD_DONE),
    LOAD_OP(0x8b, 0x91, B4(0x33c)),                /* edx=upgrades */
    LOAD_OP(0x81, 0xe2, B4(0xf0)),                 /* Firepower bits */
    LOAD_OP(0x68, B4(3000 - MUSKET_LOAD_MS)),       /* base Z */
    LOAD_OP(0xdb, 0x04, 0x24, 0x58),               /* fild [esp]; pop eax */
    LOAD_OP(0x85, 0xd2),
    LOAD_JCC(0x84, LOAD_COMPARE),
    LOAD_OP(0x0f, 0xbd, 0xd2, 0x83, 0xea, 0x04),   /* highest bit - 4 */
    LOAD_OP(0x69, 0xd2, B4(0x110)),                /* vmCvar_t stride */
    { LOAD_ANCHOR, 6, 0, { 0xe8, 0, 0, 0, 0, 0x58 } },
    { LOAD_FACTOR, 7, 0, { 0xd8, 0x8c, 0x10 } },   /* fmul [eax+edx+disp] */
    { LOAD_PLAIN, 3, LOAD_COMPARE, { 0xff, 0x71, 0x2c } }, /* push time */
    LOAD_OP(0xda, 0x1c, 0x24, 0x5a),               /* ficomp [esp]; pop edx */
    LOAD_OP(0xdf, 0xe0, 0x9e),                     /* fnstsw ax; sahf */
    LOAD_JCC(0x82, LOAD_DONE),                      /* Z < time */
    LOAD_OP(0x6a, 0x07, 0x5e),                     /* esi=WP_LIGHTRIFLE */
    { LOAD_FILL, 5, 0, { 0xe8 } },                 /* PM_FillClip(esi) */
    { LOAD_PLAIN, 1, LOAD_DONE, { 0x61 } },         /* popad */
    LOAD_OP(0x8b, 0x0e),                           /* displaced mov ecx,[esi] */
    LOAD_OP(0x39, 0x69, 0x2c),                     /* game: cmp [ecx+2c],ebp */
    LOAD_OP(0xc3),
};

#define LOAD_CC15 { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, \
                    0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }
#define LOAD_SLOT(module, rva) \
    { module, true, "musket reload: loading milestone helper", rva, 15, \
      LOAD_CC15, LOAD_CC15 }

static code_rule_t patch_musket_load_game[] = {
    LOAD_SLOT(GAME, 0x19911), LOAD_SLOT(GAME, 0x1aaa1),
    LOAD_SLOT(GAME, 0x1bdd1), LOAD_SLOT(GAME, 0x22db1),
    LOAD_SLOT(GAME, 0x22dd1), LOAD_SLOT(GAME, 0x230c1),
    LOAD_SLOT(GAME, 0x23c71), LOAD_SLOT(GAME, 0x26281),
    LOAD_SLOT(GAME, 0x26d51), LOAD_SLOT(GAME, 0x26db1),
    LOAD_SLOT(GAME, 0x27591), LOAD_SLOT(GAME, 0x27ef1),
    LOAD_SLOT(GAME, 0x280e1), LOAD_SLOT(GAME, 0x2b3a1),
    LOAD_SLOT(GAME, 0x2be51), LOAD_SLOT(GAME, 0x2cf01),
    LOAD_SLOT(GAME, 0x2cf81), LOAD_SLOT(GAME, 0x2cfe1),
    LOAD_SLOT(GAME, 0x2d021), LOAD_SLOT(GAME, 0x2d041),
};

static code_rule_t patch_musket_load_cgame[] = {
    LOAD_SLOT(CGAME, 0x16b01), LOAD_SLOT(CGAME, 0x17411),
    LOAD_SLOT(CGAME, 0x1a8c1), LOAD_SLOT(CGAME, 0x1bad1),
    LOAD_SLOT(CGAME, 0x21021), LOAD_SLOT(CGAME, 0x24311),
    LOAD_SLOT(CGAME, 0x26951), LOAD_SLOT(CGAME, 0x28141),
    LOAD_SLOT(CGAME, 0x281d1), LOAD_SLOT(CGAME, 0x288a1),
    LOAD_SLOT(CGAME, 0x28ac1), LOAD_SLOT(CGAME, 0x2a1d1),
    LOAD_SLOT(CGAME, 0x2c861), LOAD_SLOT(CGAME, 0x2eb91),
    LOAD_SLOT(CGAME, 0x30d41), LOAD_SLOT(CGAME, 0x31261),
    LOAD_SLOT(CGAME, 0x31601), LOAD_SLOT(CGAME, 0x324b1),
    LOAD_SLOT(CGAME, 0x32de1), LOAD_SLOT(CGAME, 0x32e51),
};

static const code_rule_t patch_musket_load_hooks[] = {
    { GAME, true, "musket reload: load before cancellation", 0x13f58, 5,
      { 0x8b, 0x0e, 0x39, 0x69, 0x2c },
      { 0xe8, B4(0x19911u - 0x13f5du) } },
    { CGAME, true, "musket reload: load before cancellation", 0x575c, 5,
      { 0x8b, 0x0e, 0x39, 0x59, 0x2c },
      { 0xe8, B4(0x16b01u - 0x5761u) } },
};

static void pack_musket_load(code_rule_t *slots, size_t count,
                             uint32_t factor, uint32_t fill)
{
    uint32_t labels[LOAD_LABELS] = { 0 };
    size_t op_slot[ARRAY_SIZE(musket_load_ops)];
    size_t op_pos[ARRAY_SIZE(musket_load_ops)];
    size_t slot = 0, pos = 0;

    /* Lay out complete instructions, reserving five bytes for each link. */
    for (size_t i = 0; i < ARRAY_SIZE(musket_load_ops); ++i) {
        const load_op_t *op = &musket_load_ops[i];
        bool last = i + 1 == ARRAY_SIZE(musket_load_ops);
        if (pos + op->size + (last ? 0 : 5) > slots[slot].size) {
            code_rule_t *from = &slots[slot];
            if (++slot == count) {
                fputs("musket_reload.c: helper exceeds padding slots\n", stderr);
                exit(EXIT_FAILURE);
            }
            from->replacement[pos] = 0xe9;
            put32(from->replacement + pos + 1,
                  slots[slot].rva - (from->rva + (uint32_t)pos + 5));
            pos = 0;
        }
        op_slot[i] = slot;
        op_pos[i] = pos;
        if (op->kind == LOAD_PLAIN && op->label)
            labels[op->label] = slots[slot].rva + (uint32_t)pos;
        pos += op->size;
    }

    uint32_t anchor = 0;
    for (size_t i = 0; i < ARRAY_SIZE(musket_load_ops); ++i) {
        const load_op_t *op = &musket_load_ops[i];
        code_rule_t *rule = &slots[op_slot[i]];
        unsigned char *out = rule->replacement + op_pos[i];
        uint32_t rva = rule->rva + (uint32_t)op_pos[i];
        memcpy(out, op->bytes, op->size);
        if (op->kind == LOAD_BRANCH)
            put32(out + 2, labels[op->label] - (rva + op->size));
        else if (op->kind == LOAD_ANCHOR)
            anchor = rva + 5;
        else if (op->kind == LOAD_FACTOR)
            put32(out + 3, factor - anchor);
        else if (op->kind == LOAD_FILL)
            put32(out + 1, fill - (rva + op->size));

        /* PM_Weapon keeps zero in EBP on game and EBX on cgame. */
        if (i + 2 == ARRAY_SIZE(musket_load_ops) && rule->module == CGAME)
            out[1] = 0x59;
    }
    for (size_t i = slot + 1; i < count; ++i)
        slots[i].enabled = false;
}

PATCH_CONSTRUCTOR(configure_musket_load)
{
    pack_musket_load(patch_musket_load_game, ARRAY_SIZE(patch_musket_load_game),
                     0x14561a8, 0x13300);
    pack_musket_load(patch_musket_load_cgame, ARRAY_SIZE(patch_musket_load_cgame),
                     0xaf7748, 0x4b30);
}

/* The loading hook and its helper fragments must be enabled together. */
REGISTER_CODE_RULES(patch_musket_load_game)
REGISTER_CODE_RULES(patch_musket_load_cgame)
REGISTER_CODE_RULES(patch_musket_load_hooks)
