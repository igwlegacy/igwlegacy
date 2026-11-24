/* What the wave script formats share: the game's console, cvars and the
 * level's spot lists, filled the way the XML loader fills them. Each
 * format is its own file: toml.c.
 */
#pragma once

#include <stdbool.h>

#pragma GCC visibility push(hidden)

/* Prints to the game console, up to 511 bytes. */
__attribute__((format(printf, 1, 2)))
void waves_print(const char *format, ...);

void waves_set_cvar(const char *name, const char *value);

/* G_IsExpansion, through the engine instead of the DLL's own helper,
 * which takes its argument in a register.
 */
bool waves_is_expansion(void);

/* Whether no script has loaded yet for this level: the map's own spots
 * come from the level's first script only, as in the XML loader.
 */
bool waves_first_script(void);

/* A spot as the scripts give it. Kinds without a yaw or an intensity
 * ignore those.
 */
typedef struct {
    float origin[3];
    float yaw;
    float intensity;
} waves_spot_t;

/* The map's spots. Like the XML loader, these keep the first script's
 * spots, add nothing beyond the list's capacity, and raise every spot but
 * the superweapons' by 2 units. Deathhand, radio and rocket spots exist
 * in the expansion only.
 */
void waves_add_spawn_spot(const waves_spot_t *spot);
void waves_add_deathhand_spot(const waves_spot_t *spot);
void waves_add_radio_spot(const waves_spot_t *spot);
void waves_add_rocket_spot(const waves_spot_t *spot);
void waves_add_superweapon_spot(const waves_spot_t *spot);

/* Artillery comes from every script, unlike the map's spots. False when
 * the list is full.
 */
bool waves_add_artillery_spot(const waves_spot_t *spot);

/* G_SpawnRoamers, which the TOML scripts keep. */
void waves_spawn_roamers(void);

/* toml.c's G_LoadWaveScript(xml) for the .toml next to the .xml: -1
 * without one, otherwise 1 once loaded. G_ERROR for an invalid one.
 */
int toml_waves_load(const char *xml);

/* G_RunWaves' frame for a loaded TOML script; false without one. */
bool toml_waves_run(void);

#pragma GCC visibility pop
