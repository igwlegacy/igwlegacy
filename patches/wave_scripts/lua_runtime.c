/* PUC Lua 5.4 for the game blob, see lua_runtime.h: the interpreter's
 * sources included as one unit, as Lua's own onelua.c does, behind the
 * configuration that keeps it deterministic, plus the arena, the
 * instruction budget, the sandbox and the floating point environment.
 * Built with SSE2.
 */
#include "blob.h"
#include "gamex86.h"

#include <stdint.h>
#include <string.h>

/* Ahead of luaconf.h and the sources: */

/* Builds the core and the libraries, which see each other's internals. */
#define LUA_CORE
#define LUA_LIB

/* The console instead of stdout and stderr. */
void lua_runtime_write(const char *text, size_t length);
static void write_error(const char *format, const char *text);
#define lua_writestring(s, l) lua_runtime_write((s), (l))
#define lua_writeline() lua_runtime_write("\n", 1)
#define lua_writestringerror(s, p) write_error((s), (p))

/* String hashes are seeded from a constant instead of the clock and
 * addresses, and table.sort picks its pivots without the clock.
 */
#define luai_makeseed(L) ((void)(L), 0x5eed1e55u)
#define l_randomizePivot() (~0u)

/* Without signal.h, the hook mask is a plain int. */
#define l_signalT int

#include "luaconf.h"

/* Lua's own hexadecimal float parsing and formatting, independent of
 * strtod()'s and printf()'s, which do no hexadecimal floats.
 */
#undef lua_strx2number
#undef lua_number2strx

#include "llimits.h"

/* The arena that every state lives in, see below. */
#define ARENA_SIZE (8u << 20)
static unsigned char arena[ARENA_SIZE] __attribute__((aligned(8)));

/* A pointer's offset in the arena, or ARENA_SIZE for one outside it. */
static inline uintptr_t arena_offset(const void *p)
{
    uintptr_t offset = (uintptr_t)p - (uintptr_t)arena;
    return offset < ARENA_SIZE ? offset : ARENA_SIZE;
}

/* Tables hash objects by their address. Their offsets in the arena keep a
 * table's order the same whatever base Windows loads the DLL at and
 * whatever the build, which moves the arena in the image. The libraries'
 * C functions, the only keys outside the arena, all hash alike.
 */
#undef point2uint
#define point2uint(p) ((unsigned int)arena_offset(p))

/* The core. */
#include "lapi.c"
#include "lcode.c"
#include "lctype.c"
#include "ldebug.c"
#include "ldo.c"
#include "ldump.c"
#include "lfunc.c"
#include "lgc.c"
#include "llex.c"
#include "lmem.c"
#include "lobject.c"
#include "lopcodes.c"
#include "lparser.c"
#include "lstate.c"
#include "lstring.c"
#include "ltable.c"
#include "ltm.c"
#include "lundump.c"
#include "lvm.c"
#include "lzio.c"

/* Pointers that scripts see, in tostring() and string.format("%p"): an
 * object's offset in the arena above a made-up base, and NULL for the
 * libraries' C functions, for the same reasons.
 */
#define SCRIPT_POINTER_BASE 0x10000000u

static const void *script_topointer(lua_State *L, int index)
{
    uintptr_t offset = arena_offset(lua_topointer(L, index));
    return offset < ARENA_SIZE ? (const void *)(SCRIPT_POINTER_BASE + offset) : NULL;
}
#define lua_topointer script_topointer

/* The libraries the sandbox opens, and lauxlib. */
#include "lauxlib.c"
#include "lbaselib.c"
#include "lmathlib.c"
#include "lstrlib.c"
#include "ltablib.c"
#include "lutf8lib.c"

#include "lua_runtime.h"

/* The console, a line at a time. */

static char line[512];
static size_t line_length;

static void flush_line(void)
{
    line[line_length] = 0;
    game_syscall(G_PRINT, line);
    line_length = 0;
}

void lua_runtime_write(const char *text, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        line[line_length++] = text[i];
        if (text[i] == '\n' || line_length == sizeof(line) - 1)
            flush_line();
    }
}

static void write_error(const char *format, const char *text)
{
    char message[512];
    int n = snprintf(message, sizeof(message), format, text);
    lua_runtime_write(message, n < (int)sizeof(message) ? (size_t)n : sizeof(message) - 1);
}

/* The floating point environment: round to nearest, all exceptions
 * masked; doubles with 53 bits for the x87, which only carries doubles
 * between functions here, and no denormals flushed for SSE2.
 */

#define SCRIPT_X87_CONTROL 0x027f
#define SCRIPT_MXCSR 0x1f80

static uint16_t game_x87_control;
static uint32_t game_mxcsr;

static void load_environment(uint16_t x87_control, uint32_t mxcsr)
{
    __asm__ volatile("fldcw %0" : : "m"(x87_control));
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));
}

void lua_runtime_enter(void)
{
    __asm__ volatile("fnstcw %0" : "=m"(game_x87_control));
    __asm__ volatile("stmxcsr %0" : "=m"(game_mxcsr));
    load_environment(SCRIPT_X87_CONTROL, SCRIPT_MXCSR);
}

void lua_runtime_leave(void)
{
    load_environment(game_x87_control, game_mxcsr);
}

void lua_runtime_game(void)
{
    load_environment(game_x87_control, game_mxcsr);
}

void lua_runtime_script(void)
{
    load_environment(SCRIPT_X87_CONTROL, SCRIPT_MXCSR);
}

/* The arena. Lua passes each block's size to free and realloc, so blocks
 * need no headers: a size class per multiple of 8 bytes up to 2 KiB, then
 * per power of 2, each with a list of freed blocks that it reuses last
 * freed first. Every new state starts from an empty arena, so the same
 * script with the same inputs gets the same addresses.
 */

#define SMALL_STEP 8u
#define SMALL_LIMIT 2048u
#define NUM_SMALL (SMALL_LIMIT / SMALL_STEP)
/* Large classes 2^12 .. 2^23, the whole arena. */
#define NUM_LARGE 12
#define NUM_CLASSES (NUM_SMALL + NUM_LARGE)

static size_t arena_used;
static void *free_lists[NUM_CLASSES];

/* The class of a size, or -1 beyond the arena. */
static int size_class(size_t size)
{
    if (size <= SMALL_LIMIT)
        return size ? (int)((size - 1) / SMALL_STEP) : 0;
    int c = NUM_SMALL;
    for (size_t capacity = SMALL_LIMIT * 2; capacity < size; capacity <<= 1)
        if (++c == NUM_CLASSES)
            return -1;
    return c;
}

static size_t class_size(int c)
{
    if (c < (int)NUM_SMALL)
        return (size_t)(c + 1) * SMALL_STEP;
    return (size_t)SMALL_LIMIT << (c - NUM_SMALL + 1);
}

static void *arena_alloc(size_t size)
{
    int c = size_class(size);
    if (c < 0)
        return NULL;
    void *block = free_lists[c];
    if (block) {
        free_lists[c] = *(void **)block;
        return block;
    }
    size_t bytes = class_size(c);
    if (bytes > ARENA_SIZE - arena_used)
        return NULL;
    block = arena + arena_used;
    arena_used += bytes;
    return block;
}

/* A block may be bigger than its class, when shrinking kept it. */
static void arena_free(void *block, size_t size)
{
    int c = size_class(size);
    *(void **)block = free_lists[c];
    free_lists[c] = block;
}

/* lua_Alloc. When ptr is NULL, old_size is a type and no size. */
static void *allocate(void *ud, void *ptr, size_t old_size, size_t new_size)
{
    (void)ud;
    if (!new_size) {
        if (ptr)
            arena_free(ptr, old_size);
        return NULL;
    }
    if (!ptr)
        return arena_alloc(new_size);
    int old_class = size_class(old_size), new_class = size_class(new_size);
    if (new_class == old_class)
        return ptr;
    void *block = arena_alloc(new_size);
    if (!block) {
        /* Lua relies on shrinking to succeed: keep the bigger block. */
        return new_size < old_size ? ptr : NULL;
    }
    memcpy(block, ptr, old_size < new_size ? old_size : new_size);
    arena_free(ptr, old_size);
    return block;
}

/* The instruction budget, counted in steps by a count hook. Once used up,
 * the script stays stopped: the hook raises the error again wherever it
 * continues, and pcall and xpcall pass it on.
 */

#define HOOK_STEP 1000

static int budget_left;
static bool stopped;

static int stop(lua_State *L)
{
    stopped = true;
    lua_pushliteral(L, "the script ran out of its instruction budget");
    return lua_error(L);
}

static void count_hook(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    budget_left -= HOOK_STEP;
    if (stopped || budget_left < 0)
        stop(L);
}

static int sandbox_pcall(lua_State *L)
{
    int results = luaB_pcall(L);
    return stopped ? stop(L) : results;
}

static int sandbox_xpcall(lua_State *L)
{
    int results = luaB_xpcall(L);
    return stopped ? stop(L) : results;
}

/* load(chunk, name, mode, env) with mode "t" whatever the script passes:
 * binary chunks can break the interpreter. An absent env stays absent.
 */
static int sandbox_load(lua_State *L)
{
    if (lua_gettop(L) < 3)
        lua_settop(L, 3);
    lua_pushliteral(L, "t");
    lua_replace(L, 3);
    return luaB_load(L);
}

/* math.randomseed() alone would seed from the clock and an address. */
static int sandbox_randomseed(lua_State *L)
{
    luaL_checkinteger(L, 1);
    lua_pushvalue(L, lua_upvalueindex(1));
    lua_insert(L, 1);
    lua_call(L, lua_gettop(L) - 1, LUA_MULTRET);
    return lua_gettop(L);
}

/* The seed math.random starts each state with. */
#define RANDOM_SEED 0x16f1

/* Opens the libraries and trims them to the sandbox, run protected. */
static int open_sandbox(lua_State *L)
{
    static const luaL_Reg libraries[] = {
        { LUA_GNAME, luaopen_base },     { LUA_TABLIBNAME, luaopen_table },
        { LUA_STRLIBNAME, luaopen_string }, { LUA_MATHLIBNAME, luaopen_math },
        { LUA_UTF8LIBNAME, luaopen_utf8 },
    };
    for (size_t i = 0; i < sizeof(libraries) / sizeof(libraries[0]); ++i) {
        luaL_requiref(L, libraries[i].name, libraries[i].func, 1);
        lua_pop(L, 1);
    }

    lua_pushglobaltable(L);
    lua_pushnil(L);
    lua_setfield(L, -2, "dofile");
    lua_pushnil(L);
    lua_setfield(L, -2, "loadfile");
    lua_pushcfunction(L, sandbox_load);
    lua_setfield(L, -2, "load");
    lua_pushcfunction(L, sandbox_pcall);
    lua_setfield(L, -2, "pcall");
    lua_pushcfunction(L, sandbox_xpcall);
    lua_setfield(L, -2, "xpcall");

    lua_getfield(L, -1, LUA_MATHLIBNAME);
    lua_getfield(L, -1, "randomseed");
    lua_pushvalue(L, -1);
    lua_pushinteger(L, RANDOM_SEED);
    lua_call(L, 1, 0);
    lua_pushcclosure(L, sandbox_randomseed, 1);
    lua_setfield(L, -2, "randomseed");
    lua_pop(L, 2);

    /* The package library is not there to keep track of these. */
    luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
    lua_pushnil(L);
    lua_setfield(L, -2, LUA_GNAME);
    lua_pop(L, 1);
    return 0;
}

/* An error outside of any protected call: stop the map. */
static int stop_map(lua_State *L)
{
    char message[256];
    const char *text = lua_tostring(L, -1);
    snprintf(message, sizeof(message), "Lua: %s\n", text ? text : "unprotected error");
    lua_runtime_leave();
    game_syscall(G_ERROR, message);
    return 0;
}

lua_State *lua_runtime_new(void)
{
    arena_used = 0;
    memset(free_lists, 0, sizeof(free_lists));
    line_length = 0;
    stopped = false;

    lua_State *L = lua_newstate(allocate, NULL);
    if (!L)
        return NULL;
    lua_atpanic(L, stop_map);
    lua_sethook(L, count_hook, LUA_MASKCOUNT, HOOK_STEP);
    lua_pushcfunction(L, open_sandbox);
    if (lua_runtime_pcall(L, 0, 0) != LUA_OK)
        return NULL;
    return L;
}

/* The message handler: the message with a traceback. */
static int traceback(lua_State *L)
{
    const char *message = lua_tostring(L, 1);
    if (!message)
        message = lua_pushfstring(L, "(error object is a %s value)", luaL_typename(L, 1));
    luaL_traceback(L, L, message, 1);
    return 1;
}

int lua_runtime_pcall(lua_State *L, int nargs, int nresults)
{
    budget_left = LUA_RUNTIME_BUDGET;
    int base = lua_gettop(L) - nargs;
    lua_pushcfunction(L, traceback);
    lua_insert(L, base);
    int status = lua_pcall(L, nargs, nresults, base);
    lua_remove(L, base);
    return status;
}
