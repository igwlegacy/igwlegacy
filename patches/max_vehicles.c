#include "patches.h"

/* Raise the wave scripts' MAXCOUNTS VEHICLES cap from 4 to MAX_VEHICLES.
 *
 * G_SetMaxCount (gamex86.dll RVA 0x62f40) stores each MAXCOUNTS value of
 * a wave script clamped to a hardcoded cap. For VEHICLES that is
 *
 *     cmp edi,4; jge 0x62fc6; mov [igw_maxVehicles],edi; ret
 *     0x62fc6: mov dword [igw_maxVehicles],4; ret
 *
 * so a script asking for more than 4 live vehicles (scarabs, ontos,
 * walkers and boss walkers together) still gets 4. G_SendWave cuts every
 * wave's vehicles to what igw_maxVehicles leaves room for.
 *
 * Replace both immediates. The cmp takes a sign-extended imm8, so the cap
 * must stay below 128; the mov's imm32 follows the relocated address of
 * igw_maxVehicles, which is left untouched. The XML loader's default of 4
 * for scripts without a VEHICLES entry is kept, so only scripts that ask
 * for more vehicles change. wave_scripts/toml.c applies the same cap.
 *
 * The NPC pool (MAX_IGW_NPCS, 256) also holds the conscripts (capped at
 * 200), the officer and the roamers, so vehicles beyond that just fail to
 * spawn the way any NPC does once the pool is full.
 */
#define MAX_VEHICLES 16

static const code_rule_t patch_max_vehicles[] = {
    { GAME, true, "max vehicles: compare against the new cap", 0x62fba, 3,
      { 0x83, 0xff, 0x04 },                 /* cmp edi,4 */
      { 0x83, 0xff, MAX_VEHICLES } },       /* cmp edi,MAX_VEHICLES */
    { GAME, true, "max vehicles: clamp to the new cap", 0x62fcc, 4,
      { B4(4u) },                           /* mov [igw_maxVehicles],4 */
      { B4(MAX_VEHICLES) } },               /* mov [igw_maxVehicles],MAX_VEHICLES */
};

REGISTER_CODE_RULES(patch_max_vehicles)
