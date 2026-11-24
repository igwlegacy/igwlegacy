/* PUC Lua 5.4 inside gamex86's blob, made for scripts whose every run
 * must come out the same: on every machine, every DLL base and every
 * round. lua_runtime.c holds the whole interpreter and what makes it so:
 *
 * - The floating point is SSE2 (see blob_sse2_sources in
 *   patches/meson.build) and musl's math functions, in plain IEEE double
 *   arithmetic, under a fixed MXCSR and x87 control word.
 * - The states live in an arena that every new state starts afresh, and
 *   whatever hashes or prints an address sees its offset in the arena, so
 *   the tables' order is the same everywhere and in every build.
 * - String hashing, table.sort's pivots and math.random have fixed seeds,
 *   and math.randomseed needs one.
 * - No io, os, package, debug or coroutine library, and load() takes text
 *   only.
 * - A script runs at most LUA_RUNTIME_BUDGET instructions per entry;
 *   pcall cannot catch that error.
 */
#pragma once

#include "lua.h"
#include "lauxlib.h"

#include <stdbool.h>

#pragma GCC visibility push(hidden)

/* Instructions a script may run per entry, before it is stopped. */
#define LUA_RUNTIME_BUDGET 5000000

/* A new, empty state in a reset arena, with the sandbox's libraries
 * open, or NULL without memory. Any earlier state is gone without its
 * finalizers ever running.
 */
lua_State *lua_runtime_new(void);

/* lua_pcall(L, nargs, nresults, 0) with a fresh instruction budget and a
 * traceback for errors; the error message is then at the top.
 */
int lua_runtime_pcall(lua_State *L, int nargs, int nresults);

/* The game's x87 control word and MXCSR: enter saves them and sets the
 * script's, leave restores them. Every Lua call from game code must be
 * between the two; lua_runtime_game() and lua_runtime_script() switch
 * back and forth for retail calls in between.
 */
void lua_runtime_enter(void);
void lua_runtime_leave(void);
void lua_runtime_game(void);
void lua_runtime_script(void);

/* What a Lua print() writes, line by line, goes to the game console. */
void lua_runtime_write(const char *text, size_t length);

#pragma GCC visibility pop
