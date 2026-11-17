#include "patches.h"

/* PM_BeginReload (game 0x12d00 / cgame 0x44a0) already accepts ready,
 * firing, deployed and deployed-firing states. Replace its additional
 * HMG-only rejection with a choice of reload state:
 *   state = weapon == WP_HEAVYMG && old_state < WEAPON_DEPLOYED
 *         ? WEAPON_RELOADING_UNDEPLOYED : WEAPON_RELOADING;
 *
 * PM_FinishReload (0x13390 / 0x4bc0) returns state 11 to ready, whereas
 * state 4 returns the HMG to deployed. Reusing these paths preserves the
 * original deployment state, clip transfer, animations and reload timing.
 *
 * At the gate, EAX is playerState and ECX is its old weaponstate. The
 * three saved registers and 12-byte local area leave [ESP+0x0c] unused;
 * the original locals occupy [ESP+0x10] and [ESP+0x14]. Save the chosen
 * state there until the final store, where ECX is playerState. EDX is
 * scratch at both sites; retain EAX for the torso-animation argument.
 * push/pop initializes EDX in three bytes without changing the stack
 * depth. No absolute operands or relocation slots are introduced.
 * Keep all three fragments enabled together: the first two cover the
 * complete 23-byte gate, splitting an instruction at the rule-size limit.
 */
#define HMG_RELOAD(module, gate, reject, store) \
    { module, true, "HMG reload: select state, part 1", gate, 16, \
      { 0x83, 0xb8, B4(0x90), 0x0a, 0x75, 0x0e, \
        0x83, 0xf9, 0x09, 0x74, 0x09, 0x83, 0xf9 }, \
      { 0x6a, 0x04, 0x5a, 0x83, 0xb8, B4(0x90), 0x0a, \
        0x75, 0x07, 0x83, 0xf9, 0x07, 0x73 } }, \
    { module, true, "HMG reload: select state, part 2", gate + 16, 7, \
      { 0x07, 0x0f, 0x85, B4(reject - (gate + 23u)) }, \
      { 0x02, 0xb2, 0x0b, 0x89, 0x54, 0x24, 0x0c } }, \
    { module, true, "HMG reload: begin in selected state", store, 10, \
      { 0xc7, 0x81, B4(0x94), B4(4) }, \
      { 0x8b, 0x54, 0x24, 0x0c, 0x89, 0x91, B4(0x94) } }

/* PM_Weapon's secondary-button cancellation calls PM_AmmoForAttack with
 * ECX = weapon and primary = false. For the HMG, allow cancellation even
 * with an empty clip and return WEAPON_DEPLOYED in EDX. The existing
 * cancellation clears weaponTime; restoring deployed instead of ready
 * lets the following secondary-attack handler perform normal undeployment.
 * The clip is filled only by PM_FinishReload, which this path skips.
 * Undeployed HMG reloads use state 11 and never enter this state-4 check.
 *
 * Other weapons use the musket cancellation helper with primary = false;
 * it delegates to PM_AmmoForAttack except for the musket. CDQ
 * makes EDX zero whenever the returned ammo count is positive, preserving
 * their cancellation to WEAPON_READY; nonpositive results skip the store.
 * The extra argument is removed with pop ECX, which is scratch here.
 * Helpers occupy padding after PM_FinishDeploy and PM_AmmoForAttack.
 * Keep each helper pair, call and state store enabled together.
 */
#define HMG_CANCEL_RELOAD(module, helper, fallback, ammo, cancel_ammo, site, store, zero_modrm) \
    { module, true, "HMG reload: undeploy cancellation helper", helper, 11, \
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, \
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc }, \
      { 0x83, 0xf9, 0x0a, 0x75, fallback - (helper + 5u), \
        0x6a, 0x07, 0x5a, 0x8b, 0xc2, 0xc3 } }, \
    { module, true, "HMG reload: other weapons' cancellation helper", fallback, 10, \
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, \
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc }, \
      { 0x6a, 0x00, 0xe8, B4(cancel_ammo - (fallback + 7u)), 0x59, 0x99, 0xc3 } }, \
    { module, true, "HMG reload: allow cancellation without ammo", site, 5, \
      { 0xe8, B4(ammo - (site + 5u)) }, \
      { 0xe8, B4(helper - (site + 5u)) } }, \
    { module, true, "HMG reload: restore deployment before cancellation", store, 6, \
      { 0x89, zero_modrm, B4(0x94) }, { 0x89, 0x91, B4(0x94) } }

/* Server authority and client prediction must use the same reload states. */
static const code_rule_t patch_hmg_reload[] = {
    HMG_RELOAD(GAME, 0x12d32u, 0x12e33u, 0x12e6bu),
    HMG_RELOAD(CGAME, 0x44d2u, 0x4633u, 0x460fu),
    HMG_CANCEL_RELOAD(GAME, 0x134d5u, 0x13556u, 0x134e0u, 0x13273u,
                      0x1400au, 0x1401du, 0xa9),
    HMG_CANCEL_RELOAD(CGAME, 0x4d05u, 0x4d86u, 0x4d10u, 0x4aa3u,
                      0x5808u, 0x581bu, 0x99),
};

REGISTER_CODE_RULES(patch_hmg_reload)
