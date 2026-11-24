/* What blobs know about the retail gamex86.dll they run in. Addresses are
 * the retail virtual addresses at the preferred base 0x20000000; the DLL
 * may load elsewhere, so they are only used through GAME_ADDRESS().
 */
#pragma once

#include "blob.h"

#include <stdint.h>

#pragma GCC visibility push(hidden)

#define GAME_PREFERRED_BASE 0x20000000u

/* The engine syscall pointer, which dllEntry stores and the payload's
 * CVAR dispatcher takes over. Every trap goes through it.
 */
#define GAME_SYSCALL 0x200b58bcu
enum {
    G_PRINT = 0,
    G_ERROR = 1,
    G_CVAR_SET = 6,
    G_CVAR_VARIABLE_INTEGER_VALUE = 7,
    G_CVAR_VARIABLE_STRING_BUFFER = 8,
    G_FS_FOPEN_FILE = 12,
    G_FS_READ = 13,
    G_FS_FCLOSE_FILE = 15,
    G_SEND_SERVER_COMMAND = 19,
};

/* The wave code. G_SendScriptWave takes its wave in esi. */
#define GAME_SEND_SCRIPT_WAVE 0x2006dc10u
#define GAME_WAVE_SOUND 0x2006dc90u
/* AddNPC(route, wave, class, ratio), G_PickWaveRoute(routes, vehicles,
 * &route) and G_PickRoute(), all cdecl, and igw_numSplines, the number of
 * routes AddNPC can take.
 */
#define GAME_ADD_NPC 0x200499b0u
#define GAME_PICK_WAVE_ROUTE 0x2006da10u
#define GAME_PICK_ROUTE 0x2006d320u
#define GAME_NUM_SPLINES 0x2011c880u
/* G_SpawnRoamers(), the round's roamers ahead of G_RunWaves' waves. */
#define GAME_SPAWN_ROAMERS 0x2006d340u
/* G_AddTank(route, wave, class) for classes 5 and 6 and G_AddWalker(route,
 * wave, class) for 7 and 8, both cdecl and NULL without a free record.
 */
#define GAME_ADD_TANK 0x20053510u
#define GAME_ADD_WALKER 0x20052f70u

/* G_TeamHumanCount(-1, TEAM_RED) with both folded in: the connected
 * human players on the red team.
 */
#define GAME_TEAM_HUMAN_COUNT 0x2003d910u
/* G_SoundIndex takes its name in edi; G_GlobalSound(index) is cdecl. */
#define GAME_SOUND_INDEX 0x2002ffc0u
#define GAME_GLOBAL_SOUND 0x200309f0u

/* igwNPC_t igw_npcs[MAX_GAME_NPCS], the NPCs' records, and the fields of
 * a record and of its gentity_t that tell whether it lives. A death
 * clears the record's inuse and entity, but an officer's.
 */
#define GAME_NPCS 0x21311820u
#define MAX_GAME_NPCS 256
#define NPC_SIZE 0x60cu
#define NPC_INUSE 0x0u
#define NPC_ENTITY 0x8u
#define NPC_WAVE 0x370u
#define NPC_CLASS 0x3d4u
#define ENTITY_INUSE 0x290u
#define ENTITY_HEALTH 0x36cu
#define ENTITY_NPC 0x3dcu

/* igw_waveNPCs[MAX_NPC_WAVES + 1][2]: each wave's first and last NPC, a
 * wave free while its first is NULL. Conscripts and vehicles march in
 * waves 1..MAX_NPC_WAVES - 1, the numbers G_AddNPCWave hands out.
 */
#define GAME_WAVE_NPCS 0x21374160u
#define MAX_NPC_WAVES 30

/* igw_xml.cpp's globals. */
#define GAME_WAVES 0x2011c0b8u
#define GAME_NUM_WAVES 0x2011c83cu
#define GAME_FIRST_MEDIUM_WAVE 0x2011c0b4u
#define GAME_FIRST_LARGE_WAVE 0x2011c838u
#define GAME_MAX_CONSCRIPTS 0x2011c848u
#define GAME_MAX_VEHICLES 0x2011c844u
#define GAME_MAX_OFFICERS 0x2011c0a8u
#define GAME_MAX_COMMANDOS 0x2011c840u
#define GAME_MAP_MAX_BOTS 0x2011c0b0u
#define GAME_MIN_WAVE_DIST_SMALL 0x2011db94u
#define GAME_MIN_WAVE_DIST_MEDIUM 0x2011da64u
#define GAME_MIN_WAVE_DIST_LARGE 0x2011dba0u
#define GAME_ROAMER_INTERVAL 0x2011db9cu
#define GAME_SMALL_WAVE_TIME 0x2011da68u
#define GAME_MEDIUM_WAVE_TIME 0x2011db98u
#define GAME_LOSE_ROUTES 0x2011c8b0u
#define GAME_NUM_LOSE_ROUTES 0x2011c8b4u
#define GAME_DM_CIVS 0x2011c884u

/* The NPC counts and sk_npc_treshold.integer that G_WaveFlags compares. */
#define GAME_NUM_CONSCRIPTS 0x212f8d34u
#define GAME_NUM_VEHICLES 0x213826f4u
#define GAME_NPC_THRESHOLD 0x2011da8cu

/* level_locals_t level and the fields the wave scripts touch. */
#define GAME_LEVEL 0x215b89c0u
#define LEVEL_TIME 0x20u
#define LEVEL_ROUND_START_TIME 0x2348u
#define LEVEL_LAST_WAVE_TIME 0x2354u
#define LEVEL_SUPERWEAPON_SPOTS 0x2368u
#define LEVEL_NUM_SUPERWEAPON_SPOTS 0x23e0u
#define LEVEL_ARTILLERY_SPOTS 0x23e4u
#define LEVEL_ARTILLERY_INTENSITY 0x2420u
#define LEVEL_WAVES_LOADED 0x2434u
#define LEVEL_NUM_ARTILLERY_SPOTS 0x2438u
#define LEVEL_NUM_SPAWN_SPOTS 0x289cu
#define LEVEL_SPAWN_SPOTS 0x28a0u
#define LEVEL_NUM_DEATHHAND_SPOTS 0x2a80u
#define LEVEL_DEATHHAND_SPOTS 0x2a84u
#define LEVEL_NUM_RADIO_SPOTS 0x2b74u
#define LEVEL_RADIO_SPOTS 0x2b78u
#define LEVEL_NUM_ROCKET_SPOTS 0x2c68u
#define LEVEL_ROCKET_SPOTS 0x2c6cu

/* The capacities of the arrays above and of igw_waves. */
#define MAX_GAME_WAVES 30
#define MAX_SUPERWEAPON_SPOTS 10
#define MAX_ARTILLERY_SPOTS 5
#define MAX_SPAWN_SPOTS 20
#define MAX_DEATHHAND_SPOTS 10
#define MAX_RADIO_SPOTS 10
#define MAX_ROCKET_SPOTS 10
#define MAX_ROUTES 24
#define NUM_NPC_CLASSES 13

/* igwWave_t.counts' indices, the XML's class order. AddNPC's class is the
 * index + 1.
 */
enum {
    NPC_CONSCRIPT,
    NPC_GRENADIER,
    NPC_HLEGIONNAIRE,
    NPC_OFFICER,
    NPC_SCARAB,
    NPC_ONTOS,
    NPC_WALKER,
    NPC_BOSS_WALKER,
    NPC_SPY,
    NPC_ENGINEER,
    NPC_BOMBER,
    NPC_CIV,
    NPC_HERRAD,
};

/* igwWave_t, 0x40 bytes. */
typedef struct {
    int routes;                 /* bit n: route n, 1..24 */
    int counts[NUM_NPC_CLASSES];
    float templarRatio;
    int size;                   /* 0..2: small, medium, large */
} game_wave_t;

/* igwSpawnSpot_t, for spawn, radio and rocket spots. */
typedef struct {
    float origin[3];
    float angles[3];
} game_spot_t;

/* igwDeathhandSpot_t. */
typedef struct {
    float origin[3];
    int unknown_c;
    float yaw;
    int unknown_14;
} game_deathhand_spot_t;

_Static_assert(sizeof(game_wave_t) == 0x40, "igwWave_t is 0x40 bytes");
_Static_assert(sizeof(game_spot_t) == 0x18, "igwSpawnSpot_t is 0x18 bytes");
_Static_assert(sizeof(game_deathhand_spot_t) == 0x18, "igwDeathhandSpot_t is 0x18 bytes");

/* A retail address, a global's lvalue and a level_locals_t field's lvalue
 * where the DLL has actually been loaded.
 */
#define GAME_ADDRESS(va) ((void *)(blob_image_base + ((va) - GAME_PREFERRED_BASE)))
#define GAME(type, va) (*(type *)GAME_ADDRESS(va))
#define LEVEL(type, offset) GAME(type, GAME_LEVEL + (offset))

/* The engine's syscall entry, for traps: game_syscall(G_PRINT, text). */
#define game_syscall (*(int (**)(int, ...))GAME_ADDRESS(GAME_SYSCALL))

#pragma GCC visibility pop
