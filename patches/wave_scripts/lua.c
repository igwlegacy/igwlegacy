/* Lua wave scripts. With scripts/waves/<map>-<level>.lua present, the
 * script replaces the .toml and the .xml of the same name and the whole
 * retail wave and roamer spawning with it: nothing spawns but what the
 * script spawns. scripts.c calls lua_waves_load() first in
 * G_LoadWaveScript and lua_waves_run() in G_RunWaves.
 *
 * A script's top level only declares: the settings below as globals, and
 * locals and functions. Its global function frame(t) is its single entry
 * point, called every server frame of the round with t, the milliseconds
 * since the round's start, from when the map's routes exist on. Every
 * round runs in a new state, the file run again, and so does the rest of
 * a round after a reload for a difficulty change, with t counting on. A
 * runtime error prints its traceback and stops the script until the next
 * round; an error while loading drops the map like the XML loader's
 * errors. The lose waves at a lost round's end stay away as well.
 *
 * Settings, applied once when the script loads:
 *
 *   cvars        = { name = "value" | number | boolean, ... }
 *   spawnpoints  = { { x, y, z, yaw }, ... }
 *   deathhand    = { { x, y, z, yaw }, ... }
 *   radiospots   = { { x, y, z, yaw }, ... }
 *   rocketspots  = { { x, y, z, yaw }, ... }
 *   artillery    = { { x, y, z, intensity }, ... }
 *   superweapons = { { x, y, z }, ... }
 *   limits       = { bots = 8 }
 *
 * A spot may name its fields instead: { x = 1, y = 2, z = 3, yaw = 90 }.
 * The spots follow the XML loader's rules, see scripts.h.
 *
 * The igw table, only for frame() but for igw.print:
 *
 *   igw.spawn(class, route, n = 1, { templar = ratio }) -> spawned
 *   igw.routes() -> number of routes, which are 1-based
 *   igw.alive([class]) -> living NPCs, of the class or all
 *   igw.players() -> human players on the red team
 *   igw.difficulty() -> 0..5, the level letter's: e m h b g p
 *   igw.banner(text), igw.chat(text), igw.sound(path)
 *   igw.cvar(name) -> string, igw.cvar(name, value)
 *   igw.print(...), the same as print(...)
 *
 * Built with SSE2, see lua_runtime.h for what keeps a script's run
 * deterministic.
 */
#include "gamex86.h"
#include "lua_runtime.h"
#include "scripts.h"

#include <stdlib.h>
#include <string.h>

/* Larger files are surely not wave scripts. */
#define MAX_FILE_SIZE (1 << 20)
/* What a banner or chat line keeps of its text: a server command is at
 * most 1022 bytes.
 */
#define MAX_MESSAGE 900
/* MAX_QPATH, the longest sound name the configstrings take. */
#define MAX_SOUND_NAME 63

/* The loaded script, kept for each round's new state. */
static bool active;
static char name[128];
static char *source;
static size_t source_size;
static int difficulty;

/* The round's state; NULL until the round's first frame creates it, and
 * again after a load. Once halted, the script waits for the next round.
 */
static lua_State *state;
static int round_start;
static bool halted;
/* Whether frame() is running, the only time igw.* may touch the game. */
static bool in_frame;

/* igw.spawn's classes, AddNPC's class numbers minus 1. */
static const char *const class_names[NUM_NPC_CLASSES + 1] = {
    "conscript", "grenadier", "legionnaire", "officer", "scarab", "ontos", "walker",
    "boss_walker", "spy", "engineer", "bomber", "civilian", "herrad", NULL,
};

/* Errors with "igw.<name>" outside of frame(). */
static void check_frame(lua_State *L, const char *function)
{
    if (!in_frame)
        luaL_error(L, "igw.%s can only be called from frame()", function);
}

/* The record's class if it is an NPC alive now, otherwise 0. A record
 * outlives its entity's reuse, so the entity must point back at it.
 */
static int living_class(const unsigned char *npc)
{
    const unsigned char *entity = *(unsigned char *const *)(npc + NPC_ENTITY);
    if (!*(const int *)(npc + NPC_INUSE) || !entity)
        return 0;
    if (!*(const int *)(entity + ENTITY_INUSE) || *(const int *)(entity + ENTITY_HEALTH) <= 0)
        return 0;
    if (*(const unsigned char *const *)(entity + ENTITY_NPC) != npc)
        return 0;
    int c = *(const int *)(npc + NPC_CLASS);
    return c >= 1 && c <= NUM_NPC_CLASSES ? c : 0;
}

/* The first free wave number, 1..29 as G_AddNPCWave takes them, or 0.
 * G_AddNPCWave only asks whether the wave has a first NPC, which may be
 * stale until G_NextWaveNPC forgets it. Here a wave is free while no NPC
 * in use marches in it, and its stale first and last NPC are forgotten.
 */
static int free_wave(void)
{
    bool busy[MAX_NPC_WAVES] = { false };
    const unsigned char *npcs = GAME_ADDRESS(GAME_NPCS);
    for (int i = 0; i < MAX_GAME_NPCS; ++i) {
        const unsigned char *npc = npcs + (size_t)i * NPC_SIZE;
        int wave = *(const int *)(npc + NPC_WAVE);
        if (*(const int *)(npc + NPC_INUSE) && *(void *const *)(npc + NPC_ENTITY) && wave >= 1 &&
            wave < MAX_NPC_WAVES)
            busy[wave] = true;
    }
    unsigned char *(*waves)[2] = GAME_ADDRESS(GAME_WAVE_NPCS);
    for (int wave = 1; wave < MAX_NPC_WAVES; ++wave) {
        if (!busy[wave]) {
            waves[wave][0] = NULL;
            waves[wave][1] = NULL;
            return wave;
        }
    }
    return 0;
}

/* igw.spawn(class, route, n, opts): n NPCs of the class at the start of
 * the route. Conscripts and vehicles march along it as a new wave, the
 * other classes roam from there. The first that the game refuses (no free
 * record, a second officer) ends it; returns how many spawned.
 */
static int api_spawn(lua_State *L)
{
    check_frame(L, "spawn");
    int class = luaL_checkoption(L, 1, NULL, class_names) + 1;
    lua_Integer route = luaL_checkinteger(L, 2);
    lua_Integer n = luaL_optinteger(L, 3, 1);
    int routes = GAME(int, GAME_NUM_SPLINES);
    luaL_argcheck(L, route >= 1 && route <= routes, 2, "no such route on this map");
    luaL_argcheck(L, n >= 0, 3, "must not be negative");

    float templar = 0.0f;
    if (!lua_isnoneornil(L, 4)) {
        luaL_checktype(L, 4, LUA_TTABLE);
        lua_pushnil(L);
        while (lua_next(L, 4)) {
            if (lua_type(L, -2) != LUA_TSTRING || strcmp(lua_tostring(L, -2), "templar"))
                return luaL_argerror(L, 4, "unknown option, there is only templar");
            if (lua_type(L, -1) != LUA_TNUMBER)
                return luaL_argerror(L, 4, "templar must be a number");
            lua_Number ratio = lua_tonumber(L, -1);
            if (!(ratio >= 0 && ratio <= 1))
                return luaL_argerror(L, 4, "templar must be between 0 and 1");
            templar = (float)ratio;
            lua_pop(L, 1);
        }
    }

    /* Only these keep their wave; AddNPC drops it for the rest. */
    bool marches = class == NPC_CONSCRIPT + 1 || (class >= NPC_SCARAB + 1 &&
                                                  class <= NPC_BOSS_WALKER + 1);
    int wave = -1;
    if (marches && n > 0 && !(wave = free_wave())) {
        lua_pushinteger(L, 0);
        return 1;
    }

    void *(*add_npc)(int, int, int, float) = GAME_ADDRESS(GAME_ADD_NPC);
    void *(*add_tank)(int, int, int) = GAME_ADDRESS(GAME_ADD_TANK);
    void *(*add_walker)(int, int, int) = GAME_ADDRESS(GAME_ADD_WALKER);
    lua_Integer spawned = 0;
    lua_runtime_game();
    for (; spawned < n; ++spawned) {
        void *npc;
        if (class == NPC_SCARAB + 1 || class == NPC_ONTOS + 1)
            npc = add_tank((int)route - 1, wave, class);
        else if (class == NPC_WALKER + 1 || class == NPC_BOSS_WALKER + 1)
            npc = add_walker((int)route - 1, wave, class);
        else
            npc = add_npc((int)route - 1, wave, class,
                          class == NPC_CONSCRIPT + 1 ? templar : 0.0f);
        if (!npc)
            break;
    }
    lua_runtime_script();
    lua_pushinteger(L, spawned);
    return 1;
}

static int api_routes(lua_State *L)
{
    check_frame(L, "routes");
    lua_pushinteger(L, GAME(int, GAME_NUM_SPLINES));
    return 1;
}

static int api_alive(lua_State *L)
{
    check_frame(L, "alive");
    int class = lua_isnoneornil(L, 1) ? 0 : luaL_checkoption(L, 1, NULL, class_names) + 1;
    const unsigned char *npcs = GAME_ADDRESS(GAME_NPCS);
    lua_Integer count = 0;
    for (int i = 0; i < MAX_GAME_NPCS; ++i) {
        int c = living_class(npcs + (size_t)i * NPC_SIZE);
        count += c && (!class || c == class);
    }
    lua_pushinteger(L, count);
    return 1;
}

static int api_players(lua_State *L)
{
    check_frame(L, "players");
    lua_runtime_game();
    int players = ((int (*)(void))GAME_ADDRESS(GAME_TEAM_HUMAN_COUNT))();
    lua_runtime_script();
    lua_pushinteger(L, players);
    return 1;
}

static int api_difficulty(lua_State *L)
{
    check_frame(L, "difficulty");
    lua_pushinteger(L, difficulty);
    return 1;
}

/* Sends "<command> \"<text><suffix>\"" to every client. Quotes would end
 * the argument and become apostrophes; with newline false, so do line
 * breaks become spaces.
 */
static void send_message(lua_State *L, const char *command, bool newline, const char *suffix)
{
    size_t length;
    const char *text = luaL_checklstring(L, 1, &length);
    char line[MAX_MESSAGE + 32];
    int n = snprintf(line, sizeof(line), "%s \"", command);
    for (size_t i = 0; i < length && i < MAX_MESSAGE; ++i) {
        char c = text[i];
        if (c == '"')
            c = '\'';
        else if ((unsigned char)c < ' ' && !(newline && c == '\n'))
            c = ' ';
        line[n++] = c;
    }
    snprintf(line + n, sizeof(line) - (size_t)n, "%s\"", suffix);
    lua_runtime_game();
    game_syscall(G_SEND_SERVER_COMMAND, -1, line);
    lua_runtime_script();
}

/* The centre print, which wants its own line end. */
static int api_banner(lua_State *L)
{
    check_frame(L, "banner");
    send_message(L, "cp", true, "\n");
    return 0;
}

static int api_chat(lua_State *L)
{
    check_frame(L, "chat");
    send_message(L, "chat", false, "");
    return 0;
}

/* G_SoundIndex(name), whose name is in edi. */
static int sound_index(const char *sound)
{
    int index;
    void *function = GAME_ADDRESS(GAME_SOUND_INDEX);
    __asm__ volatile("call *%[function]"
                     : "=a"(index), "+D"(sound)
                     : [function] "r"(function)
                     : "ecx", "edx", "memory", "cc");
    return index;
}

/* A sound every client hears in full, as the round's start and end. */
static int api_sound(lua_State *L)
{
    check_frame(L, "sound");
    size_t length;
    const char *sound = luaL_checklstring(L, 1, &length);
    luaL_argcheck(L, length > 0 && length <= MAX_SOUND_NAME && strlen(sound) == length, 1,
                  "must be a sound file's path of up to 63 bytes");
    lua_runtime_game();
    int index = sound_index(sound);
    ((void (*)(int))GAME_ADDRESS(GAME_GLOBAL_SOUND))(index);
    lua_runtime_script();
    return 0;
}

/* A setting's value as cvar text: strings as they are, numbers as
 * tostring() writes them, booleans as 1 and 0. NULL for other types.
 */
static const char *cvar_text(lua_State *L, int index)
{
    switch (lua_type(L, index)) {
    case LUA_TBOOLEAN:
        return lua_toboolean(L, index) ? "1" : "0";
    case LUA_TNUMBER:
    case LUA_TSTRING:
        /* Converts a number in place, never a key here. */
        return lua_tostring(L, index);
    default:
        return NULL;
    }
}

static int api_cvar(lua_State *L)
{
    check_frame(L, "cvar");
    size_t length;
    const char *cvar = luaL_checklstring(L, 1, &length);
    luaL_argcheck(L, length > 0 && strlen(cvar) == length, 1, "must be a cvar's name");
    if (lua_gettop(L) < 2) {
        char value[256];
        lua_runtime_game();
        game_syscall(G_CVAR_VARIABLE_STRING_BUFFER, cvar, value, (int)sizeof(value));
        lua_runtime_script();
        lua_pushstring(L, value);
        return 1;
    }
    const char *value = cvar_text(L, 2);
    luaL_argexpected(L, value, 2, "string, number or boolean");
    lua_runtime_game();
    waves_set_cvar(cvar, value);
    lua_runtime_script();
    return 0;
}

/* Pushes the global's value, raw: a metatable on _G could run code and
 * raise errors outside of the protected calls.
 */
static int get_global(lua_State *L, const char *global)
{
    lua_pushglobaltable(L);
    lua_pushstring(L, global);
    int type = lua_rawget(L, -2);
    lua_remove(L, -2);
    return type;
}

/* Registers the igw table, with print the base library's. */
static void open_igw(lua_State *L)
{
    static const luaL_Reg functions[] = {
        { "spawn", api_spawn },   { "routes", api_routes },       { "alive", api_alive },
        { "players", api_players }, { "difficulty", api_difficulty }, { "banner", api_banner },
        { "chat", api_chat },     { "sound", api_sound },         { "cvar", api_cvar },
        { NULL, NULL },
    };
    luaL_newlib(L, functions);
    get_global(L, "print");
    lua_setfield(L, -2, "print");
    lua_setglobal(L, "igw");
}

/* Runs the script in a new state: the libraries, igw and then the file's
 * top level, protected. The state, or NULL with the error message in
 * message.
 */
static int run_chunk(lua_State *L)
{
    open_igw(L);
    char chunk_name[sizeof(name) + 1];
    snprintf(chunk_name, sizeof(chunk_name), "@%s", name);
    if (luaL_loadbufferx(L, source, source_size, chunk_name, "t") != LUA_OK)
        return lua_error(L);
    lua_call(L, 0, 0);
    get_global(L, "frame");
    if (!lua_isfunction(L, -1))
        luaL_error(L, "%s: the script has no function frame(t)", name);
    return 0;
}

static lua_State *new_state(char *message, size_t size)
{
    lua_State *L = lua_runtime_new();
    if (!L) {
        snprintf(message, size, "%s: out of memory for a new Lua state", name);
        return NULL;
    }
    lua_pushcfunction(L, run_chunk);
    if (lua_runtime_pcall(L, 0, 0) != LUA_OK) {
        const char *error = lua_tostring(L, -1);
        snprintf(message, size, "%s", error ? error : "unknown error");
        return NULL;
    }
    return L;
}

/* The settings, read when the script loads. Strict: anything unknown in
 * them is an error, while other globals are the script's own.
 */

/* A spot's field: the n-th entry or the named one. */
static bool spot_field(lua_State *L, int spot, int n, const char *field, float *value)
{
    if (lua_rawgeti(L, spot, n) == LUA_TNIL) {
        lua_pop(L, 1);
        lua_pushstring(L, field);
        lua_rawget(L, spot);
    }
    int type = lua_type(L, -1);
    if (type != LUA_TNIL && type != LUA_TNUMBER)
        luaL_error(L, "%s: %s of a spot must be a number", name, field);
    if (type == LUA_TNUMBER)
        *value = (float)lua_tonumber(L, -1);
    lua_pop(L, 1);
    return type == LUA_TNUMBER;
}

/* The spot list in the global setting, each spot passed to add. The
 * fourth field, if any, is yaw or intensity.
 */
static void parse_spots(lua_State *L, const char *setting, const char *fourth,
                        void (*add)(lua_State *, const waves_spot_t *))
{
    if (get_global(L, setting) == LUA_TNIL) {
        lua_pop(L, 1);
        return;
    }
    if (!lua_istable(L, -1))
        luaL_error(L, "%s: %s must be a list of spots", name, setting);
    int list = lua_gettop(L);
    lua_Integer count = (lua_Integer)lua_rawlen(L, list);
    for (lua_Integer i = 1; i <= count; ++i) {
        if (lua_rawgeti(L, list, i) != LUA_TTABLE)
            luaL_error(L, "%s: %s[%d] must be a table", name, setting, (int)i);
        int spot = lua_gettop(L);
        static const char *const fields[] = { "x", "y", "z" };
        waves_spot_t parsed = { { 0, 0, 0 }, 0, 0 };
        for (int f = 0; f < 3; ++f)
            if (!spot_field(L, spot, f + 1, fields[f], &parsed.origin[f]))
                luaL_error(L, "%s: %s[%d] has no %s", name, setting, (int)i, fields[f]);
        float extra = 0;
        if (fourth)
            spot_field(L, spot, 4, fourth, &extra);
        if (fourth && !strcmp(fourth, "yaw"))
            parsed.yaw = extra;
        else
            parsed.intensity = extra;

        /* Nothing else in it, positional or named. */
        lua_pushnil(L);
        while (lua_next(L, spot)) {
            lua_pop(L, 1);
            bool known = false;
            if (lua_type(L, -1) == LUA_TNUMBER && lua_isinteger(L, -1)) {
                lua_Integer k = lua_tointeger(L, -1);
                known = k >= 1 && k <= (fourth ? 4 : 3);
            } else if (lua_type(L, -1) == LUA_TSTRING) {
                const char *key = lua_tostring(L, -1);
                for (int f = 0; f < 3; ++f)
                    known |= !strcmp(key, fields[f]);
                known |= fourth && !strcmp(key, fourth);
            }
            if (!known)
                luaL_error(L, "%s: %s[%d] has an unknown field", name, setting, (int)i);
        }
        add(L, &parsed);
        lua_pop(L, 1);
    }
    /* Holes would end a list early without a word. */
    lua_pushnil(L);
    while (lua_next(L, list)) {
        lua_pop(L, 1);
        if (!lua_isinteger(L, -1) || lua_tointeger(L, -1) < 1 || lua_tointeger(L, -1) > count)
            luaL_error(L, "%s: %s must be a list of spots without holes", name, setting);
    }
    lua_pop(L, 1);
}

static void add_spawn_spot(lua_State *L, const waves_spot_t *spot)
{
    waves_add_spawn_spot(spot);
}

static void add_deathhand_spot(lua_State *L, const waves_spot_t *spot)
{
    waves_add_deathhand_spot(spot);
}

static void add_radio_spot(lua_State *L, const waves_spot_t *spot)
{
    waves_add_radio_spot(spot);
}

static void add_rocket_spot(lua_State *L, const waves_spot_t *spot)
{
    waves_add_rocket_spot(spot);
}

static void add_superweapon_spot(lua_State *L, const waves_spot_t *spot)
{
    waves_add_superweapon_spot(spot);
}

static void add_artillery_spot(lua_State *L, const waves_spot_t *spot)
{
    if (!waves_add_artillery_spot(spot))
        luaL_error(L, "%s: artillery has more than %d spots", name, MAX_ARTILLERY_SPOTS);
}

static void parse_cvars(lua_State *L)
{
    if (get_global(L, "cvars") == LUA_TNIL) {
        lua_pop(L, 1);
        return;
    }
    if (!lua_istable(L, -1))
        luaL_error(L, "%s: cvars must be a table", name);
    int cvars = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, cvars)) {
        if (lua_type(L, -2) != LUA_TSTRING)
            luaL_error(L, "%s: cvars may only have names as keys", name);
        const char *cvar = lua_tostring(L, -2);
        const char *value = cvar_text(L, -1);
        if (!value)
            luaL_error(L, "%s: cvars.%s must be a string, number or boolean", name, cvar);
        lua_runtime_game();
        waves_set_cvar(cvar, value);
        lua_runtime_script();
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

static void parse_limits(lua_State *L)
{
    GAME(int, GAME_MAP_MAX_BOTS) = 8;
    if (get_global(L, "limits") == LUA_TNIL) {
        lua_pop(L, 1);
        return;
    }
    if (!lua_istable(L, -1))
        luaL_error(L, "%s: limits must be a table", name);
    int limits = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, limits)) {
        if (lua_type(L, -2) != LUA_TSTRING || strcmp(lua_tostring(L, -2), "bots"))
            luaL_error(L, "%s: limits has an unknown key, there is only bots", name);
        if (!lua_isinteger(L, -1) || lua_tointeger(L, -1) < 0)
            luaL_error(L, "%s: limits.bots must be a whole number from 0", name);
        lua_Integer bots = lua_tointeger(L, -1);
        GAME(int, GAME_MAP_MAX_BOTS) = bots < 8 ? (int)bots : 8;
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

/* Applies the settings, run protected in the load's state. */
static int apply_settings(lua_State *L)
{
    parse_cvars(L);
    parse_limits(L);
    /* As the XML loader, the level starts without artillery and spawn
     * spots, and the superweapons' are cleared when the script has any.
     */
    LEVEL(int, LEVEL_NUM_ARTILLERY_SPOTS) = 0;
    LEVEL(int, LEVEL_NUM_SPAWN_SPOTS) = 0;
    if (get_global(L, "superweapons") != LUA_TNIL)
        LEVEL(int, LEVEL_NUM_SUPERWEAPON_SPOTS) = 0;
    lua_pop(L, 1);
    parse_spots(L, "spawnpoints", "yaw", add_spawn_spot);
    parse_spots(L, "deathhand", "yaw", add_deathhand_spot);
    parse_spots(L, "radiospots", "yaw", add_radio_spot);
    parse_spots(L, "rocketspots", "yaw", add_rocket_spot);
    parse_spots(L, "artillery", "intensity", add_artillery_spot);
    parse_spots(L, "superweapons", NULL, add_superweapon_spot);
    return 0;
}

/* Drops the map like the XML loader's own errors, in the game's floating
 * point environment.
 */
__attribute__((noreturn))
static void fail(const char *reason)
{
    char text[1024];
    snprintf(text, sizeof(text), "Wave script %s: %s\n", name, reason);
    free(source);
    source = NULL;
    game_syscall(G_ERROR, text);
    __builtin_unreachable();
}

/* The level letter's difficulty: scripts/waves/<map>-<letter>.xml. */
static int level_difficulty(const char *xml, size_t length)
{
    static const char letters[] = "emhbgp";
    if (length >= 6 && xml[length - 6] == '-') {
        const char *letter = strchr(letters, xml[length - 5] | 32);
        if (letter && *letter)
            return (int)(letter - letters);
    }
    return 0;
}

int lua_waves_load(const char *xml)
{
    active = false;
    state = NULL;
    halted = false;
    in_frame = false;
    free(source);
    source = NULL;

    size_t length = strlen(xml);
    if (length < 4 || length + 1 > sizeof(name) || strcmp(xml + length - 4, ".xml"))
        return -1;
    memcpy(name, xml, length - 4);
    memcpy(name + length - 4, ".lua", 5);

    int file = 0;
    int size = game_syscall(G_FS_FOPEN_FILE, name, &file, 0);
    if (!file)
        return -1;
    if (size < 0 || size > MAX_FILE_SIZE) {
        game_syscall(G_FS_FCLOSE_FILE, file);
        fail("file is too big");
    }
    source = malloc((size_t)size + 1);
    if (!source) {
        game_syscall(G_FS_FCLOSE_FILE, file);
        fail("out of memory");
    }
    game_syscall(G_FS_READ, source, size, file);
    game_syscall(G_FS_FCLOSE_FILE, file);
    source[size] = 0;
    source_size = (size_t)size;
    difficulty = level_difficulty(xml, length);

    /* The arena keeps an error's message past leaving. */
    char message[512];
    lua_runtime_enter();
    lua_State *L = new_state(message, sizeof(message));
    if (!L) {
        lua_runtime_leave();
        fail(message);
    }
    lua_pushcfunction(L, apply_settings);
    if (lua_runtime_pcall(L, 0, 0) != LUA_OK) {
        lua_runtime_leave();
        fail(lua_tostring(L, -1));
    }
    lua_runtime_leave();

    /* Nothing of the retail waves: no script waves, lose routes or
     * deathmatch civilians.
     */
    GAME(int, GAME_NUM_WAVES) = 0;
    GAME(int, GAME_FIRST_MEDIUM_WAVE) = -1;
    GAME(int, GAME_FIRST_LARGE_WAVE) = -1;
    GAME(int, GAME_NUM_LOSE_ROUTES) = 0;
    GAME(int, GAME_LOSE_ROUTES) = 0;
    GAME(int, GAME_DM_CIVS) = 0;
    LEVEL(int, LEVEL_WAVES_LOADED) = 1;
    waves_print("Loaded wave script %s\n", name);
    active = true;
    return 1;
}

bool lua_waves_active(void)
{
    return active;
}

/* Prints a runtime error with its traceback and stops the script for the
 * rest of the round.
 */
static void halt(const char *error)
{
    static const char header[] = "^1Wave script error, stopped until the next round:\n";
    lua_runtime_write(header, sizeof(header) - 1);
    if (!error)
        error = "unknown error";
    lua_runtime_write(error, strlen(error));
    lua_runtime_write("\n", 1);
    halted = true;
    state = NULL;
}

bool lua_waves_run(void)
{
    if (!active)
        return false;
    int start = LEVEL(int, LEVEL_ROUND_START_TIME);
    int t = LEVEL(int, LEVEL_TIME) - start;
    /* The map's routes are built 100 ms after it spawns, which in a new
     * round is after the round's start: frame() waits for them, so that
     * no spawn finds them missing.
     */
    if (t < 0 || GAME(int, GAME_NUM_SPLINES) <= 0)
        return true;
    if (start != round_start) {
        round_start = start;
        state = NULL;
        halted = false;
    }
    if (halted)
        return true;

    lua_runtime_enter();
    if (!state) {
        char message[512];
        state = new_state(message, sizeof(message));
        if (!state) {
            halt(message);
            lua_runtime_leave();
            return true;
        }
    }
    lua_State *L = state;
    lua_settop(L, 0);
    get_global(L, "frame");
    if (!lua_isfunction(L, -1)) {
        halt("frame is no function anymore");
        lua_runtime_leave();
        return true;
    }
    lua_pushinteger(L, t);
    in_frame = true;
    int status = lua_runtime_pcall(L, 1, 0);
    in_frame = false;
    if (status != LUA_OK)
        halt(lua_tostring(L, -1));
    lua_runtime_leave();
    return true;
}
