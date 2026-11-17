#include "patches.h"

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
 * Cancellation clears weaponTime and skips PM_FinishReload, so no ammo
 * transfers. Secondary fire can then perform the normal aiming action.
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
