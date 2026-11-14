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

/* Server authority and client prediction must use the same reload states. */
static const code_rule_t patch_hmg_reload[] = {
    HMG_RELOAD(GAME, 0x12d32u, 0x12e33u, 0x12e6bu),
    HMG_RELOAD(CGAME, 0x44d2u, 0x4633u, 0x460fu),
};

REGISTER_CODE_RULES(patch_hmg_reload)
