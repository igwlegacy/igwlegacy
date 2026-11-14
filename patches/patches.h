#ifndef IGW_PATCHES_H
#define IGW_PATCHES_H

#include "patcher.h"

/* Each definition belongs to its functionality's .c file. The shared
 * registry references these rules without duplicating their settings.
 */
extern const cvar_rule_t patch_run_pitch;
extern const cvar_rule_t patch_run_roll;
extern const cvar_rule_t patch_bob_pitch;
extern const cvar_rule_t patch_bob_roll;
extern const cvar_rule_t patch_bob_up;
extern const code_rule_t patch_screen_shake;
extern const code_rule_t patch_damage_kick[3];
extern const code_rule_t patch_landing_dip;
extern const code_rule_t patch_death_camera[3];
extern const code_rule_t patch_respawn_countdown[6];
extern const code_rule_t patch_main_menu_ad[11];
extern const code_rule_t patch_main_menu_github[9];

#define B4(a) (a) & 255, ((a) >> 8) & 255, ((a) >> 16) & 255, ((a) >> 24) & 255

/* Redirect an x87 operand to the existing zero float. Its position remains
 * covered by the DLL's original HIGHLOW relocation when the image rebases.
 */
#define READ_ZERO(label, where, op0, op1, address) \
    { CGAME, true, label, where, 6, \
      { op0, op1, B4(address) }, { op0, op1, B4(0x300a5344u) } }

#endif
