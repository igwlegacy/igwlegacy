---@meta igw
-- LuaLS definitions for the Lua wave scripts,
-- scripts/waves/<map>-<level>.lua; see "Lua wave scripts" in
-- monkey-patcher/docs/README.md and patches/wave_scripts/lua.c.
--
-- The scripts run in PUC Lua 5.4 with only the base, table, string, math
-- and utf8 libraries: no io, os, package, debug or coroutine, no dofile or
-- loadfile, load() takes text only, and math.randomseed needs an integer.
-- The repository's .luarc.json tells LuaLS so.
--
-- The top level only declares the settings below as globals, and locals
-- and functions. frame(t) is the single entry point.
--
-- LuaLS doesn't check what a script assigns to a global against the
-- global's type here, so a setting is only checked when the script
-- repeats its type:
--
--     ---@type igw.Limits
--     limits = { bots = 3 }
--
--     ---@type igw.YawSpot[]
--     spawnpoints = { { 120, -340, 64, 90 }, { x = 200, y = -300, z = 64 } }

------------------------------------------------------------------------
-- Spots
------------------------------------------------------------------------

-- A spot's fields may also be mixed, positional and named; nothing else
-- may be in it. The coordinates are required, the fourth field defaults
-- to 0.

---@class igw.YawSpotList
---@field [1] number x
---@field [2] number y
---@field [3] number z
---@field [4] number? yaw, the facing in degrees

---@class igw.YawSpotNamed
---@field x number
---@field y number
---@field z number
---@field yaw number? the facing in degrees

-- A map position with a facing: { x, y, z, yaw } or named fields.
---@alias igw.YawSpot igw.YawSpotList | igw.YawSpotNamed

---@class igw.ArtillerySpotList
---@field [1] number x
---@field [2] number y
---@field [3] number z
---@field [4] number? intensity

---@class igw.ArtillerySpotNamed
---@field x number
---@field y number
---@field z number
---@field intensity number?

-- An artillery battery: { x, y, z, intensity } or named fields.
---@alias igw.ArtillerySpot igw.ArtillerySpotList | igw.ArtillerySpotNamed

---@class igw.SpotList
---@field [1] number x
---@field [2] number y
---@field [3] number z

---@class igw.SpotNamed
---@field x number
---@field y number
---@field z number

-- A map position without a facing: { x, y, z } or named fields.
---@alias igw.Spot igw.SpotList | igw.SpotNamed

------------------------------------------------------------------------
-- Settings, read once after the top level ran
------------------------------------------------------------------------

-- Cvar names and their values. Numbers are set as tostring() writes
-- them, booleans as "1" and "0".
---@alias igw.Cvars table<string, string|number|boolean>

---@class igw.Limits
-- The map's bot limit, a whole number from 0; more than 8 counts as 8.
-- Default 8.
---@field bots integer?

-- Cvars set after the skill config's exec, as the XML's SKILLSETTINGS.
---@type igw.Cvars?
cvars = nil

-- Limits of the map; there is only bots.
---@type igw.Limits?
limits = nil

-- The defenders' spawn spots, used in deathmatch only; at most 20,
-- raised by 2 units. Like the other map spots, only the level's first
-- script sets them.
---@type igw.YawSpot[]?
spawnpoints = nil

-- The expansion's Deathhand spots; at most 10, raised by 2 units.
---@type igw.YawSpot[]?
deathhand = nil

-- The expansion's radio spots; at most 10, raised by 2 units.
---@type igw.YawSpot[]?
radiospots = nil

-- The expansion's rocket spots; at most 10, raised by 2 units.
---@type igw.YawSpot[]?
rocketspots = nil

-- Artillery batteries, at most 5, raised by 2 units. Read from every
-- script, not only the level's first.
---@type igw.ArtillerySpot[]?
artillery = nil

-- Superweapon spots, at most 10. When given, they replace the map's
-- earlier ones, even as an empty list.
---@type igw.Spot[]?
superweapons = nil

------------------------------------------------------------------------
-- The entry point
------------------------------------------------------------------------

-- Called every server frame of a round once the map's routes exist, which
-- is about 100 ms into a new round. Every round, and every reload during
-- one, starts a fresh state with the file run again; t keeps counting
-- from the round's start. A runtime error, or running more than 5 million
-- instructions in one call, prints a traceback and stops the script until
-- the next round.
---@param t integer milliseconds since the round's start
function frame(t) end

------------------------------------------------------------------------
-- The igw table, only for frame() but for igw.print
------------------------------------------------------------------------

---@alias igw.Class
---| "conscript"   # marches along the route in a wave of its own
---| "grenadier"   # roams from the route's start
---| "legionnaire" # roams from the route's start
---| "officer"     # at most one at a time
---| "scarab"      # vehicle, marches along the route
---| "ontos"       # vehicle, marches along the route
---| "walker"      # vehicle, marches along the route
---| "boss_walker" # vehicle, marches along the route
---| "spy"
---| "engineer"
---| "bomber"
---| "civilian"
---| "herrad"      # roams from the route's start

---@class igw.SpawnOptions
-- The conscripts' Templar machine gun ratio, 0 to 1. Other classes
-- ignore it.
---@field templar number?

---@class igwlib
igw = {}

-- Spawns n NPCs of a class at the start of a route and returns how many
-- spawned. Conscripts and vehicles march along the route as one new wave,
-- the other classes go from there on their own. It stops at the first
-- NPC the game refuses (no free NPC record, no free wave number with 29
-- waves alive, a second officer or another of its limits), so the result may be
-- less than n, down to 0.
---@param class igw.Class
---@param route integer 1 to igw.routes()
---@param n? integer how many, from 0; default 1
---@param opts? igw.SpawnOptions
---@return integer spawned
function igw.spawn(class, route, n, opts) end

-- The number of the map's routes, which igw.spawn() numbers from 1.
---@return integer
function igw.routes() end

-- The number of living NPCs, of a class or of all.
---@param class? igw.Class
---@return integer
function igw.alive(class) end

-- The number of human players on the defending (red) team.
---@return integer
function igw.players() end

-- The difficulty of the script's level letter: 0 easy (e), 1 medium (m),
-- 2 hard (h), 3 brutal (b), 4 god (g), 5 primordial (p).
---@return integer
function igw.difficulty() end

-- A centre print for every player. Double quotes become single ones,
-- control characters spaces, and only the first 900 bytes are kept.
-- Colour codes (^1) work.
---@param text string
function igw.banner(text) end

-- A chat line for every player, the text cleaned up as in igw.banner().
---@param text string
function igw.chat(text) end

-- A sound every player hears in full, such as
-- "sound/gameplay/waveattack_large.wav".
---@param path string up to 63 bytes
function igw.sound(path) end

-- Returns a cvar's value, "" when it doesn't exist; with a value, sets it
-- instead and returns nothing. Numbers are set as tostring() writes them,
-- booleans as "1" and "0".
---@param name string
---@return string value
---@overload fun(name: string, value: string|number|boolean)
function igw.cvar(name) end

-- The same as print(): a line on the server console. Also works at the
-- top level.
---@param ... any
function igw.print(...) end
