/* The wave scripts' entry points, which wave_scripts.c's hooks call, and
 * the level helpers the formats share.
 */
#include "scripts.h"

#include "gamex86.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void waves_print(const char *format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    game_syscall(G_PRINT, text);
}

void waves_set_cvar(const char *name, const char *value)
{
    game_syscall(G_CVAR_SET, name, value);
}

bool waves_is_expansion(void)
{
    if (game_syscall(G_CVAR_VARIABLE_INTEGER_VALUE, "com_dedicated"))
        return game_syscall(G_CVAR_VARIABLE_INTEGER_VALUE, "sv_expansion") != 0;
    return game_syscall(G_CVAR_VARIABLE_INTEGER_VALUE, "cl_expansionSE") != 0;
}

bool waves_first_script(void)
{
    return !LEVEL(int, LEVEL_WAVES_LOADED);
}

/* Appends to one of the level's igwSpawnSpot_t lists, raised by 2. */
static void add_spawn_type_spot(const waves_spot_t *spot, uint32_t count_at, uint32_t spots_at,
                                int max)
{
    int *count = &LEVEL(int, count_at);
    if (!waves_first_script() || *count >= max)
        return;
    game_spot_t *s = &((game_spot_t *)GAME_ADDRESS(GAME_LEVEL + spots_at))[(*count)++];
    s->origin[0] = spot->origin[0];
    s->origin[1] = spot->origin[1];
    s->origin[2] = spot->origin[2] + 2;
    s->angles[1] = spot->yaw;
}

void waves_add_spawn_spot(const waves_spot_t *spot)
{
    add_spawn_type_spot(spot, LEVEL_NUM_SPAWN_SPOTS, LEVEL_SPAWN_SPOTS, MAX_SPAWN_SPOTS);
}

void waves_add_radio_spot(const waves_spot_t *spot)
{
    if (waves_is_expansion())
        add_spawn_type_spot(spot, LEVEL_NUM_RADIO_SPOTS, LEVEL_RADIO_SPOTS, MAX_RADIO_SPOTS);
}

void waves_add_rocket_spot(const waves_spot_t *spot)
{
    if (waves_is_expansion())
        add_spawn_type_spot(spot, LEVEL_NUM_ROCKET_SPOTS, LEVEL_ROCKET_SPOTS, MAX_ROCKET_SPOTS);
}

void waves_add_deathhand_spot(const waves_spot_t *spot)
{
    int *count = &LEVEL(int, LEVEL_NUM_DEATHHAND_SPOTS);
    if (!waves_is_expansion() || !waves_first_script() || *count >= MAX_DEATHHAND_SPOTS)
        return;
    game_deathhand_spot_t *s =
        &((game_deathhand_spot_t *)GAME_ADDRESS(GAME_LEVEL + LEVEL_DEATHHAND_SPOTS))[(*count)++];
    s->origin[0] = spot->origin[0];
    s->origin[1] = spot->origin[1];
    s->origin[2] = spot->origin[2] + 2;
    s->yaw = spot->yaw;
}

void waves_add_superweapon_spot(const waves_spot_t *spot)
{
    int *count = &LEVEL(int, LEVEL_NUM_SUPERWEAPON_SPOTS);
    if (!waves_first_script() || *count >= MAX_SUPERWEAPON_SPOTS)
        return;
    float (*spots)[3] = GAME_ADDRESS(GAME_LEVEL + LEVEL_SUPERWEAPON_SPOTS);
    memcpy(spots[(*count)++], spot->origin, sizeof(spot->origin));
}

bool waves_add_artillery_spot(const waves_spot_t *spot)
{
    int *count = &LEVEL(int, LEVEL_NUM_ARTILLERY_SPOTS);
    if (*count >= MAX_ARTILLERY_SPOTS)
        return false;
    float (*spots)[3] = GAME_ADDRESS(GAME_LEVEL + LEVEL_ARTILLERY_SPOTS);
    memcpy(spots[*count], spot->origin, sizeof(spot->origin));
    ((float *)GAME_ADDRESS(GAME_LEVEL + LEVEL_ARTILLERY_INTENSITY))[*count] = spot->intensity;
    ++*count;
    return true;
}

void waves_spawn_roamers(void)
{
    ((void (*)(void))GAME_ADDRESS(GAME_SPAWN_ROAMERS))();
}

/* G_LoadWaveScript(xml), before the XML loader: -1 continues into it, as
 * no other script exists; otherwise the result to return.
 */
BLOB_EXPORT int wave_scripts_load(const char *xml)
{
    return toml_waves_load(xml);
}

/* G_RunWaves, in place of its call of G_SpawnRoamers: 0 to spawn the
 * roamers and continue into the XML scheduler, 1 once a script has
 * handled the frame instead.
 */
BLOB_EXPORT int wave_scripts_run(void)
{
    return toml_waves_run();
}
