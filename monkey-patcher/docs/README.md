## Configuration

Each functionality has a separate file in `patches/` at the repository root:

| File | Functionality |
| --- | --- |
| [run_pitch.c](../../patches/run_pitch.c) | Pitch from forward and backward speed |
| [run_roll.c](../../patches/run_roll.c) | Roll from sideways speed |
| [bob_pitch.c](../../patches/bob_pitch.c) | Walking pitch bob |
| [bob_roll.c](../../patches/bob_roll.c) | Walking roll bob |
| [bob_up.c](../../patches/bob_up.c) | Walking eye-height bob |
| [fog.c](../../patches/fog.c) | Engine fog defaults: `r_forceFog 1`, `r_forceFogDensity 0` |
| [skip_intro.c](../../patches/skip_intro.c) | Intro video at startup |
| [use_shaders.c](../../patches/use_shaders.c) | Engine `r_useShaders` forced to `1` |
| [heat_haze.c](../../patches/heat_haze.c) | Engine `r_heathazedistort` forced to `0` |
| [console_stdout.c](../../patches/console_stdout.c) | Engine console output mirrored to stdout |
| [window_close.c](../../patches/window_close.c) | Game window close calls the engine's quit routine immediately |
| [screen_shake.c](../../patches/screen_shake.c) | Explosion and death shake |
| [damage_kick.c](../../patches/damage_kick.c) | Damage pitch and roll, including the return phase |
| [burning_screen.c](../../patches/burning_screen.c) | Prevent a false burning overlay at the start of a game |
| [landing_dip.c](../../patches/landing_dip.c) | Eye-height dip on landing |
| [death_camera.c](../../patches/death_camera.c) | Immediate spectator movement after death, without automatic camera rise |
| [respawn_countdown.c](../../patches/respawn_countdown.c) | Respawn countdown rounded up to seconds, with text expiring at the respawn deadline |
| [centerprint_position.c](../../patches/centerprint_position.c) | Crosshair centerprints (round start, round winner, airstrike, server `cp`) moved up a quarter of the screen |
| [cursor_size.c](../../patches/cursor_size.c) | Shared cursor size for the main menu, pause menu and purchasing overlays |
| [cursor_position.c](../../patches/cursor_position.c) | Independent cursor positions for opening the main and ingame menus and completing loading |
| [shotgun.c](../../patches/shotgun.c) | Editable hardcoded shotgun stats and pellet damage falloff |
| [hmg_reload.c](../../patches/hmg_reload.c) | HMG reloading while undeployed and cancellation by undeploying |
| [musket_zoom.c](../../patches/musket_zoom.c) | Editable right-click musket zoom-in duration and start delay |
| [spawn_protection.c](../../patches/spawn_protection.c) | Spawn protection time, forced to 2000 ms |
| [team_repair.c](../../patches/team_repair.c) | Toolkit refills a teammate's current clip for money |
| [rts_spectator.c](../../patches/rts_spectator.c) | Prevent entering RTS mode while spectating or dead |
| [rts_grid.c](../../patches/rts_grid.c) | Snap RTS building placement to a configurable world-space grid |
| [main_menu_ad.c](../../patches/main_menu_ad.c) | Main-menu promotional panel, ticker, and update request |
| [main_menu_join.c](../../patches/main_menu_join.c) | Main-menu `MULTIPLAYER` button labelled `JOIN` |
| [browser_lan.c](../../patches/browser_lan.c) | Server browser opening on `LAN` instead of `Internet` |
| [browser_stop_hint.c](../../patches/browser_stop_hint.c) | Server browser refresh without the non-working `Press SPACE to stop` hint |
| [browser_stop_button.c](../../patches/browser_stop_button.c) | Server browser `STOP` button stopping a refresh at once instead of restarting it against the second master server while no server has answered |
| [browser_leaderboard_banner.c](../../patches/browser_leaderboard_banner.c) | Server browser without the `Visit www.igwdeath.com for the leaderboard!` banner |
| [browser_news_hint.c](../../patches/browser_news_hint.c) | Server browser without the `Visit www.igwarlord.com for the latest news!` line |
| [game_mode_vanilla.c](../../patches/game_mode_vanilla.c) | Host-game menu's `COOP` game mode labelled `VANILLA` |
| [game_mode_resistance.c](../../patches/game_mode_resistance.c) | `RESISTANCE` coop game mode with friendly fire off, no difficulty multiplier and the god difficulty, flagged by `g_legacygametype 1` |
| [loading_game_mode.c](../../patches/loading_game_mode.c) | Loading screen naming the game mode instead of the maximum players, with the difficulty only for `Vanilla` |
| [autoplay.c](../../patches/autoplay.c) | Automatically enter the game once loading finishes, without clicking `PLAY!` |
| [valuable_markers.c](../../patches/valuable_markers.c) | Persistent models at every valuable spawn position |
| [pickup_markers.c](../../patches/pickup_markers.c) | Persistent models at every dynamite, rocket launcher and radio spawn position |
| [rts_buildable.c](../../patches/rts_buildable.c) | Translucent green overlay on buildable terrain in RTS mode |
| [wave_scripts.c](../../patches/wave_scripts.c) | `scripts/waves/<map>-<level>.lua` Lua wave scripts and `.toml` wave scripts with sequential waves |
| [max_vehicles.c](../../patches/max_vehicles.c) | Wave scripts' `MAXCOUNTS` `VEHICLES` capped at 16 instead of 4 |

Edit the rule definitions in the corresponding file and rebuild. Each file registers its own rules with `REGISTER_CVAR_RULE`, `REGISTER_CODE_RULE`, their plural forms for arrays, or `REGISTER_PAYLOAD_FEATURE`. Comment out a registration to omit those rules. For a new functionality, add its `.c` file to `patches/meson.build` and register its rules in that file; no declarations or patch lists need updating in shared C files. `src/rules.c` collects and validates the registered rules, sorting by registration name for a deterministic payload layout while preserving the order within each array. One functionality can supply several rules.

| Setting                                 | Implementation                                                            |
| ---                                     | ---                                                                       |
| `cg_runpitch`, `cg_runroll`             | Force `"0"`                                                               |
| `cg_bobpitch`, `cg_bobroll`, `cg_bobup` | Force `"0"`                                                               |
| Explosion and death shake               | Return immediately from the shake starter                                 |
| Damage pitch and roll                   | Read an existing zero constant; HUD damage indicators retain their values |
| Landing dip                             | Read the zero constant                                                    |
| Death camera                            | Enter spectator movement immediately; omit the slow rise and spectator lift |
| Respawn countdown                       | Round remaining seconds up and expire the final message at the respawn deadline |
| Centerprint position                    | Callers that centre the message box on the crosshair push a y offset of `-120` (480-line units) instead of `0` |
| `sk_immortal_time`                      | Force `SPAWN_PROTECTION_MS` (`"2000"`)                                     |
| Main-menu promotion                     | Omit the panel's menu items, ticker, and update request                    |
| Intro video                             | Never queue the startup `cinematic` command; the command remains available |
| Loading completion                      | Run the existing `PLAY!` handler when `cg_pbloading` reaches `9` |
| `r_useShaders`                          | Force `1` at renderer registration and at every renderer read |
| `r_heathazedistort`                     | Force `0` at renderer registration and at its renderer read |

Weapon model sway retains its original behavior. The shotgun patch provides matching server and client recoil settings, initially set to their stock values.

Immediate death spectating requires the patched `gamex86.dll` on the server and `cgamex86.dll` on the client. The server creates the corpse and enables spectator movement on the first client update after death, without the 3000 ms delay or the upward relocation of up to 256 units. The client omits the 15-unit-per-second rise while awaiting the spectator state. The respawn timer remains unchanged.

The respawn countdown fix requires only the patched `cgamex86.dll` on the client. `CG_DrawDeathScreen` rounds the remaining milliseconds up to seconds and gives its centerprint the remaining time as its lifetime. The final `SPAWNING IN: 1` therefore covers the last second and expires at the server's advertised respawn deadline. Other centerprints keep their usual lifetime, and the server's respawn timing is unchanged. The two helpers occupy guarded function-alignment padding and use relative calls and jumps, so no new PE relocations are needed.

The centerprint position change requires only the patched `cgamex86.dll` on the client. IGW draws centerprints on a dark band and treats their y argument as an offset from the screen centre in 480-line units, so messages printed with `0`, such as `ROUND STARTS IN` and `AIRSTRIKE PREVENTED!`, covered the crosshair. Those callers now pass `-CENTERPRINT_RAISE` (`120`, a quarter of the screen height), which places the band's centre at a quarter of the screen from the top. Messages that already sit below the crosshair keep their positions. Each change replaces the sign-extended immediate of a `push`, so no relocated operands are touched.

### TOML wave scripts

When `G_LoadWaveScript` is asked for `scripts/waves/<map>-<level>.xml`, the patched `gamex86.dll` first looks for `scripts/waves/<map>-<level>.toml` and, if it exists, loads that instead. It only needs the patched `gamex86.dll` on the server. Without a `.toml`, the `.xml` loads and schedules waves as before.

A TOML script takes every XML section and key, with the XML loader's defaults, caps and clamps. As in the XML, the map's own spots (`SPAWNPOINT`, `SUPERWEAPON`, and the expansion's `deathhand`, `radiospot` and `rocketspot`) come from the level's first script only, the spawn-type spots are raised by 2 units, and `ARTILLERY` is read from every script. Numbers may also be given as strings. Route lists may be a string as in the XML (`"1 2 3"`), a number, or an array (`[1, 2, 3]`). Repeated spot elements become arrays of tables (`[[SPAWNPOINT]]`), and `[ARTILLERY]` and `[SUPERWEAPON]` hold theirs in `spot = [{ x = 1, y = 2, z = 3 }]` or `[[ARTILLERY.spot]]`. Unknown keys and other invalid content stop the map with the file name and line.

Waves don't use the XML's small, medium and large scheduling. The `[[WAVE]]`s are sent one after the other in file order:

```toml
[[WAVE]]
startup = 10000   # ms after the round's start
routes = "1 2 3"
CONSCRIPT = 31
WALKER = 1

[[WAVE]]
startup = 100000  # ms after the previous wave was sent
size = 2          # the incoming attack sound: 0 small, 1 medium, 2 large
routes = "1 2 3"
CONSCRIPT = 30
GRENADIER = 2
HLEGIONNAIRE = 2
HERRAD = 1
```

`startup` is mandatory and counts from the round's start for the first wave and from the previous wave's sending for every later one. After the last wave, that wave repeats every `startup` milliseconds. `size` defaults to `0` and only selects the announcement sound. Unlike the XML, a wave also spawns its `GRENADIER`, `HLEGIONNAIRE` (legionnaire) and `HERRAD` counts, which retail parses but never sends. They start together at the start of one of the wave's routes (any route without `routes`) and then roam like the roamers `G_SpawnRoamers` sends. They aren't scaled by the player count, and they stop at the first NPC the game can't add. A wave may consist of them alone. A wave is sent only while the NPC count is below `sk_npc_treshold`, as in the XML scheduler. A wave that cannot be sent is retried every second and never skipped. A new round starts over from the first wave. A reload during a round, for example after a difficulty change, keeps the current position in the new script. `MINWAVEDIST_*` are still accepted but unused. `SMALLWAVE_TIME` and `MEDIUMWAVE_TIME` don't time the waves but still delay the first spies and engineers as before. Up to 128 waves are allowed, and the first 30 are also copied into the retail wave list.

The loader is C around the vendored [tomlc17](../../vendor/tomlc17), in [wave_scripts/toml.c](../../patches/wave_scripts/toml.c), and part of `gamex86.dll`'s C blob (see [C blobs](#c-blobs)). Its scripts are limited to 1 MiB, and the blob's 4 MiB arena holds the parsed document. A `jmp` replaces `G_LoadWaveScript`'s first six bytes and replays them when no `.toml` exists. In `G_RunWaves`, the call to `G_SpawnRoamers` now goes through a stub that, while a TOML script is active, spawns the roamers from the blob and lets the TOML scheduler handle the frame, returning from `G_RunWaves` itself. Otherwise it continues into `G_SpawnRoamers` and the XML scheduler as before.

### Lua wave scripts

A `scripts/waves/<map>-<level>.lua` takes precedence over both the `.toml` and the `.xml` of the same name. It only needs the patched `gamex86.dll` on the server. A Lua script replaces all retail wave and roamer spawning: no XML or TOML scheduler, no roamers or attack groups, no lose waves at a lost round's end, and none of their sounds, chat lines, player scaling or caps. Nothing spawns but what the script spawns.

The script runs in PUC Lua 5.4 from the vendored [vendor/lua](../../vendor/lua). Its top level only declares settings, locals and functions. The global function `frame(t)` is the only entry point. It is called every server frame of a round with `t`, the milliseconds since the round's start, once the map's NPC routes exist (about 100 ms into a new round). There is no waiting or sleeping: a script keeps its own state in locals and compares `t`.

```lua
cvars = { sk_Amorale_start = 250, g_npcspeed = 230 }
limits = { bots = 6 }
spawnpoints = { { 120, -340, 64, 90 }, { x = 200, y = -300, z = 64, yaw = 180 } }
artillery = { { 0, 0, 512, 1.5 } }

local next_wave = 10000

function frame(t)
    if t >= next_wave and igw.alive("conscript") < 40 then
        local route = math.random(igw.routes())
        igw.spawn("conscript", route, 10 + 2 * igw.players(), { templar = 0.2 })
        if igw.difficulty() >= 3 then
            igw.spawn("scarab", route)
        end
        igw.incoming("large")
        print("wave on route " .. route)
        next_wave = t + 60000
    end
end
```

The settings are globals that the loader reads once, after the top level ran:

| Setting | Content |
| --- | --- |
| `cvars` | Cvar names and their values, strings, numbers or booleans (`1`/`0`), set after the skill config exec as the XML's `SKILLSETTINGS` |
| `limits` | `bots`, the map's bot limit, 0 to 8 (default 8) |
| `spawnpoints`, `deathhand`, `radiospots`, `rocketspots` | Lists of spots `{ x, y, z, yaw }` |
| `artillery` | Up to 5 spots `{ x, y, z, intensity }` |
| `superweapons` | Spots `{ x, y, z }` |

A spot may also name its fields, as in `{ x = 1, y = 2, z = 3, yaw = 90 }`, and its `yaw` or `intensity` defaults to 0. The spots follow the XML's rules: the map's own spots come from the level's first script only, the spawn-type spots are raised by 2 units, and the expansion's `deathhand`, `radiospots` and `rocketspots` are only used with the expansion. Anything unknown inside the settings stops the map with the file name, as do syntax errors, errors at the top level, a missing `frame` and any `igw` call other than `igw.print` outside of `frame`.

The `igw` table, which only works inside `frame` (but `igw.print`):

| Function | Does |
| --- | --- |
| `igw.spawn(class, route, n, opts)` | Spawns `n` (default 1) NPCs of `class` at the start of route `route` (1-based) and returns how many spawned. Conscripts and vehicles march along the route as one new wave, other classes roam from there. It stops at the first NPC the game refuses, for example with no free NPC record, no free wave number (29 waves at once) or a second officer. `opts.templar` is the conscripts' Templar machine gun ratio, 0 to 1. |
| `igw.routes()` | The number of routes on the map |
| `igw.alive([class])` | The number of living NPCs, of `class` or of all classes |
| `igw.players()` | The number of human players on the red team |
| `igw.difficulty()` | 0 to 5 from the file's level letter: `e`, `m`, `h`, `b`, `g`, `t`; 4 for `RESISTANCE`'s `<map>-res` scripts, whose difficulty is fixed to god |
| `igw.banner(text)` | A centre print for every player |
| `igw.sound(path)` | A sound every player hears in full |
| `igw.incoming([size])` | Retail's wave announcement: every player hears the attack sound of `size`, `"small"` (default), `"medium"` or `"large"`, and sees `INCOMING CONFEDERATE ATTACK!` in their language below the crosshair. It doesn't spawn anything. |
| `igw.cvar(name[, value])` | Returns a cvar's value as a string, or sets it |
| `igw.print(...)` | The same as `print`: a line on the server's console and in its `qconsole.log` (with `logfile 1`), never sent to players. Meant for debug and status output. |

The classes are `conscript`, `grenadier`, `legionnaire`, `officer`, `scarab`, `ontos`, `walker`, `boss_walker`, `spy`, `engineer`, `bomber`, `civilian` and `herrad`.

[spiral-p.lua](../../gamedir/scripts/waves/spiral-p.lua) is a complete example that uses every setting and function: Spiral's waves with player scaling, Templar ratios, caps and retries, roamers, an officer that respawns, spies and engineers, a boss walker that speeds up the infantry, and status lines on the server console. It also shows how a script continues after a reload during a round.

[wave_scripts/meta.lua](../../patches/wave_scripts/meta.lua) has the [LuaLS](https://luals.github.io) definitions of the settings, `frame` and the `igw` table, and the repository's [.luarc.json](../../.luarc.json) loads it and leaves out the libraries the scripts don't have. An editor with LuaLS opened on the repository then completes and checks wave scripts. A setting is only checked against its type when the script repeats it, as in `---@type igw.Limits` above `limits = { bots = 3 }`.

Every round runs in a fresh Lua state: the file is run again from its top level, so nothing carries over from the last round. A reload during a round, for example after a difficulty change, also starts a fresh state, and `t` keeps counting from the round's start. A runtime error, including running out of the instruction budget, prints the error with a traceback to the console and stops the script until the next round, while the map keeps running.

Scripts run deterministically: the same script with the same inputs (the time, players, cvars and the game's NPCs) does the same on every machine and with every build of the DLL. Only the `base`, `table`, `string`, `math` and `utf8` libraries are there, without `dofile` and `loadfile`, and `load` only takes text. The floating point is SSE2 with musl's math functions under a fixed rounding mode. `math.random` starts from a fixed seed and `math.randomseed` needs an argument. Strings are hashed with a fixed seed, and tables hash objects by their offset in the script's own 8 MiB arena, which every new state starts afresh, so `pairs` visits keys in the same order. `tostring` and `%p` show those offsets too. Each call of `frame` (and the top level) may run up to 5 million Lua instructions, and `pcall` cannot catch running out.

The interpreter is in [wave_scripts/lua_runtime.c](../../patches/wave_scripts/lua_runtime.c) and the loader and `igw` table in [wave_scripts/lua.c](../../patches/wave_scripts/lua.c), both built with SSE2 in `gamex86.dll`'s C blob, with the musl math sources from [vendor/musl](../../vendor/musl). The `G_LoadWaveScript` and `G_RunWaves` hooks are the TOML scripts'. While a Lua script is active, the stub in `G_RunWaves` skips `G_SpawnRoamers` and the XML scheduler, and a third hook replaces `G_CheckRound`'s call of `G_SendLoseWaves` with a stub that skips them as well.

### Cursor size

Set `CURSOR_SIZE` in [cursor_size.c](../../patches/cursor_size.c) and rebuild. It defaults to 32 units in the menus' 640x480 coordinate system and controls both the square main-menu/pause-menu cursor and the purchasing/RTS overlay cursor. Install the patched `uix86.dll` and `cgamex86.dll` together.

The menu cursor retains its existing UI scaling. The overlay cursor originally used a fixed 20-pixel size; it now scales both dimensions by `vidHeight / 480`, so 32 units draw as 32 pixels at 640x480 and 72 pixels at 1920x1080. Cursor position, click coordinates, shaders and texture coordinates keep their existing behavior. Shared float constants are left alone; private data and helper instructions occupy guarded function-alignment padding. The existing address relocation slots are preserved.

To omit this feature, comment out all three `REGISTER_CODE_RULE`/`REGISTER_CODE_RULES` lines at the end of the file and rebuild.

### Cursor position

Set `MAIN_MENU_CURSOR_X/Y`, `INGAME_MENU_CURSOR_X/Y`, and `LOADING_COMPLETE_CURSOR_X/Y` in [cursor_position.c](../../patches/cursor_position.c), then rebuild the patcher and install the patched `uix86.dll`. All three positions default to `(320, 240)`, the center of the menu. Coordinates use the menus' 640x480 area with `(0, 0)` at the top left; X supports 0 through 640 and Y supports 0 through 480. The patch applies the UI's display scale and horizontal widescreen offset automatically.

The main and ingame positions are set whenever those menus open. The loading-completion position is set once when loading finishes and the `PLAY!` button appears, including when loading is already complete as the loading menu opens. The main menu previously kept the existing mouse position, the ingame pause menu reset it to `(319, 80)` physical pixels, and loading completion used half the display width and 400 physical pixels down. Submenus and purchasing overlays retain their existing behavior. To omit this feature, comment out both registration lines in `cursor_position.c` and rebuild.

### Musket zoom timing

Set `MUSKET_ZOOM_IN_MS` and `MUSKET_ZOOM_DELAY_MS` in [musket_zoom.c](../../patches/musket_zoom.c), rebuild the patcher and install the patched `cgamex86.dll` on the client. The duration is a positive integer in milliseconds and defaults to the stock 200 ms. Use `1` for a virtually immediate transition. The delay is a nonnegative integer in milliseconds, defaulting to `0`, that holds the normal field of view before starting the transition. For example, delay `100` and duration `400` wait 100 ms and then zoom over 400 ms.

Both timings begin when the weapon accepts the aiming action. Firing, drawing and other existing weapon cooldowns can postpone that action even with delay `0`. The weapon animation, deployment delay, zoom amount, 150 ms zoom-out and other weapons' zoom timings are unchanged.

To omit this feature, comment out both registration lines at the end of `musket_zoom.c` and rebuild.

### Hardcoded shotgun stats

The shotgun is `weapon_hrifle` / `WP_HEAVYRIFLE`. Edit the settings at the top of [shotgun.c](../../patches/shotgun.c) and rebuild the patcher. The table lists retail reference values for existing settings and the default curve for the added damage falloff:

| Setting | Reference value |
| --- | --- |
| Pellets per primary shot | 11 |
| Spread, applied horizontally and vertically | 700 |
| Primary firing interval (`SHOTGUN_PRIMARY_FIRE_INTERVAL_MS`) | 1200 ms |
| Secondary firing interval (`SHOTGUN_SECONDARY_FIRE_INTERVAL_MS`) | 1200 ms |
| Shells loaded per reload animation | 1 |
| Reload time per animation | 820 ms |
| Reload time for the final batch, or the last reserve ammo | 2020 ms |
| Delay after filling the clip | 50 ms |
| Recoil duration | 40 ms |
| Recoil pitch / yaw | 0.1 / 0.05 |
| Full pellet damage through (`SHOTGUN_FALLOFF_START_UNITS`) | 512 game units |
| Minimum pellet damage reached at (`SHOTGUN_FALLOFF_END_UNITS`) | 2048 game units |
| Minimum damage multiplier (`SHOTGUN_FALLOFF_MIN_PERCENT`) | 25% |
| Push away from the aim direction per secondary shot (`shotgun_push_velocity`) | none (added; 300 units/s by default) |
| Friction-free time after a push (`SHOTGUN_PUSH_KNOCKBACK_MS`) | none (added; 200 ms by default) |

Pellet count and `SHOTGUN_SHELLS_PER_RELOAD` support 1 through 127. Set `SHOTGUN_SHELLS_PER_RELOAD` to `2`, for example, to load up to two shells per animation. Each transfer is limited by free clip space and remaining reserve ammo. The final reload animation plays when that batch fills the clip or consumes the last reserve ammo. Firepower upgrades continue to scale the shell reload times. Difficulty configs and weapon scripts are not patched; damage, clip sizes, reserve ammunition and prices continue to come from those files.

The primary and secondary firing intervals are independent positive millisecond values. Each attack sets the delay before either attack can fire again; switching fire modes does not bypass that delay. These settings do not change animation playback speed. The cooldown helpers occupy guarded padding after `PM_FillClip` and use relative calls, so no new PE relocations are needed. Keep each helper and its call enabled together.

Every secondary shot that fires subtracts the aim direction scaled by `shotgun_push_velocity` from the shooter's velocity, similar to a rocket jump (primary shots do not push): aiming at the floor launches the player upwards, aiming ahead pushes them back. The direction is recomputed from the view angles including pitch. Like retail damage knockback, the push also suspends ground friction for `SHOTGUN_PUSH_KNOCKBACK_MS` unless another movement timer is already running. It runs in the shared player movement code of both DLLs, so client prediction matches the server. No single padding slot fits the helper, so `shotgun.c` packs its instructions into a list of guarded int3 padding slots chained with relative jumps; the patcher fails if that list does not fit the helper exactly.

Primary pellet damage stays at 100% through the falloff start, decreases linearly to the minimum percentage at the falloff end, then stays at that minimum. Distance is measured from the muzzle to each pellet's actual hit point. Start must be nonnegative, end must exceed start, and the minimum percentage supports 1 through 100; set it to 100 to disable attenuation. The server multiplies the existing player or NPC base damage and truncates to an integer before the game's normal upgrade, armor and minimum-damage handling. Difficulty configs and weapon scripts still provide base damage. The incendiary secondary attack is unaffected, and the client needs no additional falloff patch because damage is calculated on the server. Keep the falloff helper fragments, private constants and damage hook enabled together.

Install the resulting `gamex86.dll` on the server and `cgamex86.dll` on clients together. Both modules need the same pellet pattern and movement timings. The patch uses private floating-point constants in verified function-alignment padding, preserving the original relocation slots of their readers so other weapons keep their own spread and recoil.

The values were traced in the `reigw` source: `src/code/game/g_weapon.c` (`ShotgunPellet`, `ShotgunPattern`), `src/code/cgame/cg_weapons.c` (`CG_ShotgunPattern`), and `src/code/game/bg_pmove.c` (`PM_HeavyRifleWeapon`, `PM_LoadShell`, `PM_BeginShellReload`, `PM_FinishShell`). The corresponding retail addresses and guarded instructions are recorded in the patch.

### HMG reloading

The HMG (`WP_HEAVYMG`) can reload while undeployed, either automatically with an empty clip or manually with a partially filled clip and reserve ammo. It stays undeployed afterward; a reload started while deployed still finishes deployed. Pressing secondary attack during a deployed reload cancels the reload and undeploys, even with an empty clip. Cancellation transfers no ammunition and uses the normal undeploy animation and timing. Reload duration, Firepower Upgrade scaling and ammunition transfer on completion keep their original behavior.

Install the patched `gamex86.dll` on the server and `cgamex86.dll` on clients together. [hmg_reload.c](../../patches/hmg_reload.c) selects the existing undeployed reload state for an undeployed HMG, allowing the original reload completion code to restore its ready state. Comment out `REGISTER_CODE_RULES(patch_hmg_reload)` and rebuild to omit this feature.

### Spawn protection

A freshly spawned player takes no damage and no knockback for `SPAWN_PROTECTION_MS` milliseconds, 2000 by default. Edit the constant at the top of [spawn_protection.c](../../patches/spawn_protection.c) and rebuild. Only the patched `gamex86.dll` on the server is required.

The retail game already implements this protection: `ClientSpawn` marks the player with eFlags `0x800`, `G_Damage` ignores damage to marked players unless it bypasses protection, and `ClientThink` clears the mark after `sk_immortal_time` milliseconds. Every shipped `skill-*.cfg` sets that CVAR to `3000`. The patch forces `sk_immortal_time` with a `CVAR_FORCE` rule, so the configured time applies on every difficulty and the skill configs' own `set` is rejected as read only. Suicide and team changes still end the protection early.

### Team repair

The toolkit can repair teammates. Every toolkit hit on a living player of the same team adds up to `TEAM_REPAIR_AMMO_PER_HIT` rounds to the clip of the weapon that player is holding, never beyond its clip size. The teammate's reserve ammo is not touched. Each hit that adds at least one round costs the repairing player `TEAM_REPAIR_COST_PER_HIT` money, also when the last hit only tops up the remainder. A player with less money than that cannot repair teammates, and hits on a full clip, a weapon without a clip, or a weapon missing from `TEAM_REPAIR_WEAPONS` are free and do nothing. `TEAM_REPAIR_WEAPONS` is a bit mask of `TEAM_REPAIR_WEAPON(WP_...)` entries and defaults to `TEAM_REPAIR_ALL_WEAPONS`; the file lists the weapon names and an example. The toolkit hits every 150 ms while the attack button stays down, so the defaults of one round for one money per hit refill about 6.7 rounds per second. Edit these constants at the top of [team_repair.c](../../patches/team_repair.c) and rebuild; the ammo must be at least 1 and the cost between 0 and 9999. Only the patched `gamex86.dll` on the server is required.

`Weapon_Toolkit_Fire` (gamex86 0x36f80) returns for anything that is neither a stronghold nor a built structure. The patch redirects that return for entities below the structure types to a helper, which handles live `ET_PLAYER` entities with a client on the repairer's team and otherwise continues to the original return. While repairing, it sets the same "working" flag in the repairer's `ps.eFlags` that structure repairs set. The helper is packed into 15-byte function-alignment padding slots after `ret` instructions, chained by relative jumps. It reads the weapon scripts' clip sizes through a `call`/`pop` base, so no new PE relocations are needed. Keep the hook and all helper fragments enabled together; to omit the feature, comment out both `REGISTER_CODE_RULES` lines in the file and rebuild.

### Valuable spawn models

The patched `cgamex86.dll` displays `models/props/jewelry01.md3` at every `item_jewel` position in the loaded map, including positions with no collectible. These are visual models only; collecting valuables, their random placement, and respawn timing retain the server's behavior. No server patch or additional asset is required.

Set `VALUABLE_MARKER_MODEL` and `VALUABLE_MARKER_OFFSET_X`, `_Y`, and `_Z` at the top of `patches/valuable_markers.c`, then rebuild. The model path is relative to the game's virtual filesystem and must reference an available asset. Offsets default to zero and use world X/Y/Z map units, applied after floor placement; they also apply to suspended positions.

`valuable_markers.c` reads and caches up to 1024 positions after the renderer loads the world. It consumes the BSP entity stream through EOF, which rewinds the token cursor for the client's original skybox parser. Unsuspended positions use a downward world collision trace with the valuable item's bounds; suspended positions retain their map origin. Map angles determine each model's orientation.

Models are added only when rendering the main world's `cg.refdef`. HUD model scenes, the map render target, and hyperspace scenes are excluded. The cache is rebuilt on each world load, including renderer restarts. To omit this feature, comment out `REGISTER_PAYLOAD_FEATURE(valuable_markers)` and rebuild.

### Dynamite, rocket launcher and radio spawn models

The patched `cgamex86.dll` displays a round highlight model at every position where the server can place a dynamite, rocket launcher or radio pickup, whether or not the pickup is currently there: `item_highlight_dynamite.md3`, `item_highlight_rocket.md3` and `item_highlight_radio.md3` under `models/mapobjects/item_highlight/`, built from `raw-assets/highlight_*.png` into `zzzigwlegacy.pk3`. These are visual models only; pickups and their placement retain the server's behavior.

Dynamite positions are the map's `weapon_dynamite` entities. Rocket launcher and radio positions exist only in the wave script `scripts/waves/<map>-<e|m|h|b|g>.xml`, as `<rocketspot>` and `<radiospot>` elements. The client selects the script as the server's `G_InitGame` does: medium in survival (gametype 4), easy in teamkill (5), otherwise the one for `sv_difficulty`. Like retail, it keeps at most 10 spots of each kind, reads `<x>`, `<y>`, `<z>` and `<yaw>` with `atoi`, raises `z` by 2 and skips XML comments. The server spawns nothing on these spots in free for all (gametype 0) or without the Scorched Earth expansion (`sv_expansion`), so no rocket launcher or radio models are shown there either. `g_gametype`, `sv_difficulty`, `sv_expansion` and `mapname` are serverinfo keys, so the same applies to clients of remote servers.

Every position drops to the first surface a point trace hits within 4096 units below it, as items do; the trace starts `PICKUP_MARKER_TRACE_LIFT` units above the position. Set the model paths, `PICKUP_MARKER_OFFSET_X`, `_Y` and `_Z`, `PICKUP_MARKER_SIZE` and the trace lift at the top of `patches/pickup_markers.c`, then rebuild.

The positions are read and cached on the first main-world render after each world load, consuming the BSP entity stream through EOF like `valuable_markers.c`. Wave scripts are read up to 32 KiB. The feature name must sort before `valuable_markers`, because that feature does not pass world loads on to later features. To omit this feature, comment out `REGISTER_PAYLOAD_FEATURE(pickup_markers)` and rebuild.

### RTS spectator guard

The patched `gamex86.dll` rejects `togglerts` while the player's movement type is `PM_SPECTATOR` or their `EF_DEAD` flag is set. This includes death spectating, where the player keeps their original session team and has entity health reset to 1, and the interval before the first spectator update. The checks run before both teams' RTS paths. Only the server needs this patch. Keep all four rules in [rts_spectator.c](../../patches/rts_spectator.c) enabled together.

### RTS buildable terrain

In RTS mode, map-marked buildable surfaces receive a green tint that keeps the terrain texture readable underneath. The main world view enables it automatically; leaving RTS mode restores the normal view. Install the patched executable, `cgamex86.dll`, and the PK3 containing `scripts/rts_buildable.shader` together.

The shader multiplies red and blue by `0.6`, then adds `0.1` green. Adjust those two `rgbGen const` values to change the strength. A plain alpha-blended green is unsuitable: constant colours bypass the engine's overbright compensation, so even 35% opacity covers the mostly dark ground almost completely.

Most maps mark buildable ground per brush face rather than per material: the BSP brush side's flags word becomes collision surface flag `0x80000` at map load, which the renderer never sees. Only `centro` and `brokengate` additionally mark whole materials (contents `0x400`).

A world surface whose material carries surface flag `0x80000` (`surfaceparm canbuild`) or contents `0x400` is highlighted directly. Any other upward-facing world face is classified once, the first time it is drawn, using the same world trace the server's build check uses (`G_CheckBuildSpot`, mask `0x2011801`). The trace passes through the centre of the face's first triangle along its normal. The result is cached in the face's unused plane padding until the next map load. Planar faces also require an upward normal of at least `0.6`, as in `G_CheckBuildSpot`. Curved and triangle surfaces, and the faces of brush models such as doors, follow only their material's marking. This shows terrain eligibility. The server still checks a structure's footprint, nearby structures, special placement rules, and the player's money when building.

The client passes the overlay shader handle in the unused upper 16 bits of the main world refdef's `rdflags`. The renderer retains the original terrain draw and submits the same geometry with a translucent, unlit green shader. The inlined world and brush-model submission loops route through the same overlay helper. Models, HUD scenes and the map render target retain their usual rendering. All four registrations at the end of `patches/rts_buildable.c` belong together; comment them out and rebuild to omit the feature.

### RTS placement grid

Set `RTS_GRID_SIZE_X` and `RTS_GRID_SIZE_Y` in [rts_grid.c](../../patches/rts_grid.c), rebuild the patcher, and install the patched `cgamex86.dll` on the client. Both default to 32 map units and accept whole-unit spacing from 1 through 32768. Grid lines are anchored at world `(0, 0)`, including negative coordinates; halfway positions choose the even grid index.

The cursor ray's hit is snapped horizontally, then traced vertically to find the ground at that grid point. The original placement checks use that new trace's height, surface normal, buildable flags and entity hits. Placement smoothing is disabled for grid-snapped structures so both the preview and the submitted build position stay on the grid. Trip mines retain free placement at both ends and their original smoothing. Stronghold rebuilding retains its map-defined attachment positions. Server placement validation still applies. Keep both registrations at the end of `rts_grid.c` enabled together.

Machine-gun and anti-tank turret rotation snaps to `RTS_TURRET_ANGLE_STEP`, which defaults to 45 degrees. This rounds the final world yaw so the preview and submitted build command share the same orientation regardless of camera direction. Other structures retain their existing rotation behavior. The angle step must be a positive whole-number divisor of 360; halfway positions choose the even step index.

### RESISTANCE game mode

The host-game menu's `Game mode:` dropdown lists `VANILLA`, `RESISTANCE` and `DEATHMATCH`. `RESISTANCE` starts the same coop game as `VANILLA` (`g_gametype 3`), but with friendly fire off, no difficulty multiplier and the `GOD` difficulty. Choosing it sets those three controls and greys them out, as `DEATHMATCH` does. Choosing another mode, or opening the menu again, gives back the values they had before. Those values are kept in the archived CVARs `ui_resistancesaved`, `ui_resistancedifficulty`, `ui_resistancefriendlyfire` and `ui_resistancediffmulti`, because the UI DLL is reloaded on every map load.

The mode is flagged by the new CVAR `g_legacygametype`: `RESISTANCE` sets it to `1`, `VANILLA` and `DEATHMATCH` to `0`, its default. It is `CVAR_SERVERINFO | CVAR_LATCH`, so like `g_gametype` it takes effect with the next map, and a dedicated server enables the mode with `set g_legacygametype 1` in its config. While it is `1` in a coop game, the patched `gamex86.dll` forces `g_friendlyfire 0`, `sv_diffmulti 0` and `sv_difficulty 4` when the game starts and on every frame, so console, rcon and config changes do not stick. It also refuses `callvote difficulty`. Outside coop the game resets `g_legacygametype` to `0` and changes nothing.

Instead of god's `skill-g.cfg` and `scripts/waves/<map>-g.xml`, the server execs `skill-res.cfg` and loads `scripts/waves/<map>-res.xml`, so like every level a `scripts/waves/<map>-res.toml` or `scripts/waves/<map>-res.lua` next to it takes precedence. As with `TITAN`, a map without any of the three fails to load in `RESISTANCE`. The patched `cgamex86.dll` also picks `skill-res.cfg` for the client's own skill cfg exec in `CG_Init`, but that exec reads the level from the client's game state, which a freshly loaded `cgamex86.dll` starts with zeroed, so in practice the client execs `skill-e.cfg` there as it does for every level in retail. The engines' skill cfg checksum that `sv_pure` checks still compares `skill-g.cfg`.

While `RESISTANCE` is selected, the host-game menu shows the mode's badge, `textures/ui/mmbdgres`, above `Map:`, where the three controls the mode fixes would be. The patched `uix86.dll` names the mode on the loading screen and shows `Resist` in the server browser's difficulty column. The patched `cgamex86.dll` names it on the connection screen, and the scoreboard's header reads `Mode: Resistance` instead of the difficulty and leaves out `Diff Multiplier:` and `Friendly Fire:`, whose values the mode fixes. The browser only sees the servers' info responses, which have no `g_legacygametype`, so the patched `igwarlord.exe` and `igwarlord-server.exe` report difficulty `6` for a `RESISTANCE` server there. An unpatched browser leaves the difficulty column of such a server empty.

The server needs the patched `gamex86.dll` for the rules and the patched executable for the browser entry; clients need the patched `uix86.dll` and `cgamex86.dll` for the names. The UI changes hook the three calls of the dropdown's index mapper, which now maps the three entries to `g_gametype` 3, 3 and 0, and use a payload feature in the UI DLL. The engine changes are a small cave in each executable's zero-filled `.text` slack, whose virtual size is extended to cover it.

### CVAR mapping

Edit the `cvar_rule_t` definition in the corresponding functionality file, then rebuild. Each entry specifies the module that registers the CVAR, its name, a string value, and the mode:

```c
{ CGAME, "cg_bobup", "0", CVAR_FORCE },
{ CGAME, "cg_fov", "100", CVAR_DEFAULT },
{ GAME, "g_knockback", "600", CVAR_DEFAULT },
{ GAME, "g_bodytime", "0", CVAR_FORCE },
```

The last three entries above are examples, not active preset rules. Register a single definition with `REGISTER_CVAR_RULE(name)` or an array with `REGISTER_CVAR_RULES(name)`. Omit a registration to leave those CVARs alone; an empty registry is supported. String values can be longer than the original defaults because the patcher appends new storage instead of modifying shared string literals. Names and values have a 255-byte limit, and there can be at most 128 rules. Duplicate names within one module are rejected without regard to ASCII case.

`CVAR_DEFAULT` replaces the fallback string passed to the engine during registration. Existing saved settings and subsequent changes still take precedence. It does not force a value.

`CVAR_FORCE` sets the engine CVAR before registration, substitutes its default, adds `CVAR_ROM` to prevent ordinary console changes, and records the registered engine handle. Before a matching `CVAR_UPDATE`, it sets the engine value again so the engine fills the `vmCvar_t` integer, float, and string consistently. It also substitutes the configured value in matching `CVAR_SET` calls from that DLL. Multiple `vmCvar_t` registrations of one CVAR are recognized by their common engine handle. A null `vmCvar_t` registration still gets the default, forced set, and ROM flag.

Rules activate when the selected DLL registers the named CVAR. They cover the main tables and individual registrations such as `G_RegisterSkillCvars`; a misspelled name, an unexecuted registration, or a CVAR registered only by the engine or another module will not activate a registration rule. Rules do not override arbitrary direct writes to memory or engine changes made by another module between updates. Apply consistent rules to both modules if they register the same CVAR.

### Instruction and function patches

Each `code_rule_t` definition in a functionality file holds an enable switch, an RVA, expected bytes, and replacement bytes for an instruction patch. Set `enabled` to `false` to skip a patch. Each rule can cover up to 16 bytes; enabled overlapping ranges are rejected. The expected bytes must match before output is written.

The supplied shake patch replaces the first opcode with `ret` and checks the rest of the original instructions. This is valid because reverse engineering established that this function returns void and its callers clean up the stack. A function that returns a value, uses a different calling convention, or has required side effects needs a different patch. Address operands in replacement code must keep the existing relocation positions valid; arbitrary new absolute addresses need relocation support that the generic byte patch table does not provide.

### Main-menu promotion

In the retail UI DLL, the menu initializer at RVA `0x1e390` assigns `textures/ui/motdsmall_IGM` to the bitmap items at VAs `0x10100ea8` and `0x10100dc4`. The generic bitmap renderer at RVA `0x2c8a0` loads their shader names through the registration wrapper at RVA `0x3a2f0`. The engine resolves the extensionless shader name to the texture asset. The initializer also supplies the default Marauders promotional text and `http://igmarauders.isotx.com` link. The response parser can select other images through the `igm`, `igl`, `igw`, and `isotx` topic table at VA `0x1006d484`.

The patch omits the nine promotional menu-item registration calls while retaining their argument pushes and stack cleanup. It returns from the menu draw callback at RVA `0x1e1fc` after the normal menu/error draw, releasing its reserved stack word before returning, to skip response handling and the separately rendered ticker. The request starter at RVA `0x1dde0` returns before calling engine syscall 121 with request type 5. Original absolute operands and their relocation slots remain intact.

## Binary support and implementation

Only the retail files are accepted:

| Binary         | Bytes  | PE timestamp | SHA-256                                                            |
| ---            | ---    | ---          | ---                                                                |
| `gamex86.dll`  | 930304 | `0x4fbdcafd` | `61db8f1ca15bd6a8cf68ee98ce4af4420c7347fecdc327b2e557d7502c0931fe` |
| `cgamex86.dll` | 911360 | `0x4fbdcb0d` | `d095cc681067f88827e7229953bcc9d8f66129d3fd680a034bb4266382f60985` |
| `uix86.dll`    | 499712 | `0x4fbdcaee` | `90ab043eb8299c5852594a92bc5e6f958b97f6af55d9d0bcbbe8b81584f43dae` |
| `igwarlord.exe` | 2414080 | `0x4fbeadb4` | `15dd413911fdfb26ccc9c0ec5cd2704e78cbb6a8622de7fe18eda398c16c55b7` |

The executable uses instruction patches in [fog.c](../../patches/fog.c) to replace only the default-string pointers passed during renderer CVAR registration. `r_forcefog` (CVAR names are case insensitive) defaults to `1`, and `r_forceFogDensity` defaults to `0` instead of `0.0003`. The original flags, shared string literals, and relocation slots are preserved. Existing settings and subsequent CVAR changes retain the engine's normal behavior.

[use_shaders.c](../../patches/use_shaders.c) forces `r_useShaders` to `1`. Its renderer registration calls the engine's `Cvar_Set` through the adjacent refimport slot instead of `Cvar_Get`, which overwrites saved or user-created values with `1` on every renderer start, including `vid_restart`, and still returns the CVAR pointer. All eight renderer reads of its integer field behave as if it were `1`, so later console or UI changes have no effect. The CVAR is no longer registered with `CVAR_ARCHIVE`, although an archived value from the config keeps that flag and is saved as `1`. The patched `cgamex86.dll` also ignores `r_useShaders` when deciding whether to draw `r_bloom`, so bloom depends on `r_bloom` alone.

[heat_haze.c](../../patches/heat_haze.c) forces `r_heathazedistort` to `0` the same way: its registration passes the existing `"0"` literal instead of `"0.01"` to `Cvar_Set`, and its only renderer read loads `0.0` instead of the CVAR value, which removes the heat-haze distortion.

[console_stdout.c](../../patches/console_stdout.c) mirrors every line the engine appends to its console window to the process's stdout, which covers all `Com_Printf` output except rcon-redirected replies, plus `Sys_Error` messages and echoed console input. A hook at the end of `Conbuf_AppendText` takes the text after color codes have been stripped, removes carriage returns so lines end in plain LF, and writes it unbuffered with `WriteFile` to `GetStdHandle(STD_OUTPUT_HANDLE)`. Without an inherited stdout, for example when the game is launched normally on Windows, the write fails silently. Under Wine, launching `igwlegacy.exe` from a terminal or redirecting its output to a file is enough. The helper is spread over unused function-alignment padding and calls the imports through the existing IAT slots relative to its own address, so no relocations are added.

[window_close.c](../../patches/window_close.c) replaces the game window's `WM_CLOSE` call to `Cbuf_AddText("quit")` at RVA `0x4e4ed` with a direct call to `Com_Quit_f` at RVA `0x20920`. The normal quit routine shuts down the server, client, renderer and filesystem, then exits the process. It runs while the window still exists, before `DefWindowProcA` can destroy it, without waiting for another frame to execute the command buffer. The patch does not change `WM_DESTROY`, so renderer restarts can still recreate the window. The relative call adds no relocations and the preceding string-address relocation stays intact.

No syscall payload is added to the executable.

The program checks file size and a whole-file FNV-1a 64-bit fingerprint, then verifies PE32/i386 headers and instruction guards. The SHA-256 values above are provided for independent identification. The fingerprint guards against accidental version differences, not malicious collisions. Supporting another release requires verifying its layout and patch sites and adding a profile in `src/pe.c`.

For a module with CVAR rules or payload features, the patcher adds a writable and executable `.igwmonk` section with strings, runtime storage, and an i386 syscall dispatcher. Registered features chain ahead of the CVAR dispatcher and forward unhandled calls to it. It redirects the `dllEntry` export to a position independent initializer that saves the real engine syscall pointer and installs the dispatcher in the DLL's original syscall global. The original `dllEntry` instructions and their relocation entries stay untouched. The PE loader entry point, `vmMain` export, and unrelated syscall arguments remain intact.

The dispatcher intercepts the standard C ABI at the engine boundary, avoiding the custom internal register conventions introduced by MSVC 9 whole program optimization. It uses `call`/`pop` and section-relative offsets, so its code and data need no additional base relocations. Existing relocations still apply to the camera instruction operands. The patcher converts RVAs through PE section headers, updates section sizes and `SizeOfImage`, and recomputes the PE checksum.

### C blobs

Features that need more than a few hand-assembled stubs can be written in C. Each module gets at most one blob, built by [blob/blob.py](../blob/blob.py) from the sources listed for it in `blob_sources` in [patches/meson.build](../../patches/meson.build), with extra include directories in `blob_include_dirs`, header dependencies in `blob_depend_files` and sources to build with SSE2 floating point in `blob_sse2_sources`. The script compiles them together with the runtime in [blob/](../blob) using Pixi's `zig cc`, freestanding for i686 with MSVC's 4-byte stack alignment, then links the result position independent at address 0 and flattens it into one image. The build fails on any relocation other than `R_386_RELATIVE`.

Blob code does its floating point on the x87 by default, because a hook in the middle of a retail function may run while that function keeps values in SSE registers. Sources listed in `blob_sse2_sources` use SSE2 instead, where every operation rounds to double exactly as IEEE 754 specifies, regardless of the precision the game set on the x87, so they compute the same bits on every CPU. Their exports may only be called from points where the retail code keeps nothing in SSE registers, such as in place of a call.

Functions marked `BLOB_EXPORT` from [blob/include/blob.h](../blob/include/blob.h) are the blob's exports; everything else stays hidden and is dropped unless an export reaches it. The generated `<module>_blob.h` declares the `blob_t` `<module>_blob` and defines each export's offset as `<MODULE>_BLOB_<NAME>`. A payload feature calls an export with `emit_blob_call()` or tail-jumps to it with `emit_blob_jump()`, using the cdecl convention. Both first call `blob_init`, which on its first run applies the blob's own relocations and derives the module's runtime image base from the RVA the patcher stored in the blob header, so no PE base relocations are added.

`payload_build()` appends the blob after all features' code, 16-byte aligned, and resolves the calls into it. Its zero-initialized data, including the 4 MiB arena behind `malloc`, `realloc` and `free` in [blob/heap.c](../blob/heap.c), extends the section's virtual size only and takes no space in the file. [blob/libc.c](../blob/libc.c) and the headers in [blob/include](../blob/include) provide the C library subset the blobs need, for the "C" locale only and without files or a clock (`time()` and `clock()` return -1). [blob/decimal.c](../blob/decimal.c) converts numbers exactly with integer arithmetic: `strtod` rounds correctly, and `vsnprintf`'s `%e`, `%f` and `%g` print the exact digits rounded half to even, as glibc and musl do, so neither depends on the FPU's precision or rounding mode. [blob/int64.c](../blob/int64.c) has the 64-bit division helpers i386 code calls and [blob/setjmp.c](../blob/setjmp.c) `setjmp` and `longjmp`. The math functions are musl's, built by the blob that needs them. The headers in [blob/sdk](../blob/sdk), such as [gamex86.h](../blob/sdk/gamex86.h), describe the retail module's functions, globals and structures; `GAME_ADDRESS()` turns a retail virtual address into a pointer relative to `blob_image_base`.
