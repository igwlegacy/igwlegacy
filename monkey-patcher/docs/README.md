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
| [landing_dip.c](../../patches/landing_dip.c) | Eye-height dip on landing |
| [death_camera.c](../../patches/death_camera.c) | Immediate spectator movement after death, without automatic camera rise |
| [respawn_countdown.c](../../patches/respawn_countdown.c) | Respawn countdown rounded up to seconds, with text expiring at the respawn deadline |
| [centerprint_position.c](../../patches/centerprint_position.c) | Crosshair centerprints (round start, round winner, airstrike, server `cp`) moved up a quarter of the screen |
| [cursor_size.c](../../patches/cursor_size.c) | Shared cursor size for the main menu, pause menu and purchasing overlays |
| [cursor_position.c](../../patches/cursor_position.c) | Independent initial cursor positions for the main menu and ingame pause menu |
| [shotgun.c](../../patches/shotgun.c) | Editable hardcoded shotgun stats and pellet damage falloff |
| [hmg_reload.c](../../patches/hmg_reload.c) | HMG reloading while undeployed and cancellation by undeploying |
| [spawn_protection.c](../../patches/spawn_protection.c) | Spawn protection time, forced to 2000 ms |
| [team_repair.c](../../patches/team_repair.c) | Toolkit refills a teammate's current clip for money |
| [rts_spectator.c](../../patches/rts_spectator.c) | Prevent entering RTS mode while spectating or dead |
| [main_menu_ad.c](../../patches/main_menu_ad.c) | Main-menu promotional panel, ticker, and update request |
| [autoplay.c](../../patches/autoplay.c) | Automatically enter the game once loading finishes, without clicking `PLAY!` |
| [valuable_markers.c](../../patches/valuable_markers.c) | Persistent models at every valuable spawn position |
| [pickup_markers.c](../../patches/pickup_markers.c) | Persistent models at every dynamite, rocket launcher and radio spawn position |
| [rts_buildable.c](../../patches/rts_buildable.c) | Translucent green overlay on buildable terrain in RTS mode |

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

### Cursor size

Set `CURSOR_SIZE` in [cursor_size.c](../../patches/cursor_size.c) and rebuild. It defaults to 32 units in the menus' 640x480 coordinate system and controls both the square main-menu/pause-menu cursor and the purchasing/RTS overlay cursor. Install the patched `uix86.dll` and `cgamex86.dll` together.

The menu cursor retains its existing UI scaling. The overlay cursor originally used a fixed 20-pixel size; it now scales both dimensions by `vidHeight / 480`, so 32 units draw as 32 pixels at 640x480 and 72 pixels at 1920x1080. Cursor position, click coordinates, shaders and texture coordinates keep their existing behavior. Shared float constants are left alone; private data and helper instructions occupy guarded function-alignment padding. The existing address relocation slots are preserved.

To omit this feature, comment out all three `REGISTER_CODE_RULE`/`REGISTER_CODE_RULES` lines at the end of the file and rebuild.

### Cursor position

Set `MAIN_MENU_CURSOR_X`, `MAIN_MENU_CURSOR_Y`, `INGAME_MENU_CURSOR_X`, and `INGAME_MENU_CURSOR_Y` in [cursor_position.c](../../patches/cursor_position.c), then rebuild the patcher and install the patched `uix86.dll`. Both positions default to `(320, 240)`, the center of the menu. Coordinates use the menus' 640x480 area with `(0, 0)` at the top left; X supports 0 through 640 and Y supports 0 through 480. The patch applies the UI's display scale and horizontal widescreen offset automatically.

Each position is set whenever its menu opens. The main menu previously kept the existing mouse position, while the ingame pause menu reset it to `(319, 80)` physical pixels. Submenus and purchasing overlays retain their existing behavior. To omit this feature, comment out both registration lines in `cursor_position.c` and rebuild.

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
