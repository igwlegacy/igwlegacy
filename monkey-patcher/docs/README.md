## Configuration

Each functionality has a separate file in `patches/` at the repository root:

| File | Functionality |
| --- | --- |
| [run_pitch.c](../../patches/run_pitch.c) | Pitch from forward and backward speed |
| [run_roll.c](../../patches/run_roll.c) | Roll from sideways speed |
| [bob_pitch.c](../../patches/bob_pitch.c) | Walking pitch bob |
| [bob_roll.c](../../patches/bob_roll.c) | Walking roll bob |
| [bob_up.c](../../patches/bob_up.c) | Walking eye-height bob |
| [screen_shake.c](../../patches/screen_shake.c) | Explosion and death shake |
| [damage_kick.c](../../patches/damage_kick.c) | Damage pitch and roll, including the return phase |
| [landing_dip.c](../../patches/landing_dip.c) | Eye-height dip on landing |
| [death_camera.c](../../patches/death_camera.c) | Immediate spectator movement after death, without automatic camera rise |
| [respawn_countdown.c](../../patches/respawn_countdown.c) | Respawn countdown rounded up to seconds, with text expiring at the respawn deadline |
| [shotgun.c](../../patches/shotgun.c) | Editable hardcoded shotgun stats and pellet damage falloff |
| [main_menu_ad.c](../../patches/main_menu_ad.c) | Main-menu promotional panel, ticker, and update request |

Edit the rule definitions in the corresponding file and rebuild. Each file registers its own rules with `REGISTER_CVAR_RULE`, `REGISTER_CODE_RULE`, or their plural forms for arrays. Comment out a registration to omit those rules. For a new functionality, add its `.c` file to `patches/meson.build` and register its rules in that file; no declarations or patch lists need updating in shared C files. `src/rules.c` collects and validates the registered rules, sorting by registration name for a deterministic payload layout while preserving the order within each array. One functionality can supply several CVAR or instruction rules.

| Setting                                 | Implementation                                                            |
| ---                                     | ---                                                                       |
| `cg_runpitch`, `cg_runroll`             | Force `"0"`                                                               |
| `cg_bobpitch`, `cg_bobroll`, `cg_bobup` | Force `"0"`                                                               |
| Explosion and death shake               | Return immediately from the shake starter                                 |
| Damage pitch and roll                   | Read an existing zero constant; HUD damage indicators retain their values |
| Landing dip                             | Read the zero constant                                                    |
| Death camera                            | Enter spectator movement immediately; omit the slow rise and spectator lift |
| Respawn countdown                       | Round remaining seconds up and expire the final message at the respawn deadline |
| Main-menu promotion                     | Omit the panel's menu items, ticker, and update request                    |

Weapon model sway retains its original behavior. The shotgun patch provides matching server and client recoil settings, initially set to their stock values.

Immediate death spectating requires the patched `gamex86.dll` on the server and `cgamex86.dll` on the client. The server creates the corpse and enables spectator movement on the first client update after death, without the 3000 ms delay or the upward relocation of up to 256 units. The client omits the 15-unit-per-second rise while awaiting the spectator state. The respawn timer remains unchanged.

The respawn countdown fix requires only the patched `cgamex86.dll` on the client. `CG_DrawDeathScreen` rounds the remaining milliseconds up to seconds and gives its centerprint the remaining time as its lifetime. The final `SPAWNING IN: 1` therefore covers the last second and expires at the server's advertised respawn deadline. Other centerprints keep their usual lifetime, and the server's respawn timing is unchanged. The two helpers occupy guarded function-alignment padding and use relative calls and jumps, so no new PE relocations are needed.

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

Pellet count and `SHOTGUN_SHELLS_PER_RELOAD` support 1 through 127. Set `SHOTGUN_SHELLS_PER_RELOAD` to `2`, for example, to load up to two shells per animation. Each transfer is limited by free clip space and remaining reserve ammo. The final reload animation plays when that batch fills the clip or consumes the last reserve ammo. Firepower upgrades continue to scale the shell reload times. Difficulty configs and weapon scripts are not patched; damage, clip sizes, reserve ammunition and prices continue to come from those files.

The primary and secondary firing intervals are independent positive millisecond values. Each attack sets the delay before either attack can fire again; switching fire modes does not bypass that delay. These settings do not change animation playback speed. The cooldown helpers occupy guarded padding after `PM_FillClip` and use relative calls, so no new PE relocations are needed. Keep each helper and its call enabled together.

Primary pellet damage stays at 100% through the falloff start, decreases linearly to the minimum percentage at the falloff end, then stays at that minimum. Distance is measured from the muzzle to each pellet's actual hit point. Start must be nonnegative, end must exceed start, and the minimum percentage supports 1 through 100; set it to 100 to disable attenuation. The server multiplies the existing player or NPC base damage and truncates to an integer before the game's normal upgrade, armor and minimum-damage handling. Difficulty configs and weapon scripts still provide base damage. The incendiary secondary attack is unaffected, and the client needs no additional falloff patch because damage is calculated on the server. Keep the falloff helper fragments, private constants and damage hook enabled together.

Install the resulting `gamex86.dll` on the server and `cgamex86.dll` on clients together. Both modules need the same pellet pattern and movement timings. The patch uses private floating-point constants in verified function-alignment padding, preserving the original relocation slots of their readers so other weapons keep their own spread and recoil.

The values were traced in the `reigw` source: `src/code/game/g_weapon.c` (`ShotgunPellet`, `ShotgunPattern`), `src/code/cgame/cg_weapons.c` (`CG_ShotgunPattern`), and `src/code/game/bg_pmove.c` (`PM_HeavyRifleWeapon`, `PM_LoadShell`, `PM_BeginShellReload`, `PM_FinishShell`). The corresponding retail addresses and guarded instructions are recorded in the patch.

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

## DLL support and implementation

Only the retail files are accepted:

| DLL            | Bytes  | PE timestamp | SHA-256                                                            |
| ---            | ---    | ---          | ---                                                                |
| `gamex86.dll`  | 930304 | `0x4fbdcafd` | `61db8f1ca15bd6a8cf68ee98ce4af4420c7347fecdc327b2e557d7502c0931fe` |
| `cgamex86.dll` | 911360 | `0x4fbdcb0d` | `d095cc681067f88827e7229953bcc9d8f66129d3fd680a034bb4266382f60985` |
| `uix86.dll`    | 499712 | `0x4fbdcaee` | `90ab043eb8299c5852594a92bc5e6f958b97f6af55d9d0bcbbe8b81584f43dae` |

The program checks file size and a whole-file FNV-1a 64-bit fingerprint, then verifies PE32/i386 headers and instruction guards. The SHA-256 values above are provided for independent identification. The fingerprint guards against accidental version differences, not malicious collisions. Supporting another release requires verifying its layout and patch sites and adding a profile in `src/pe.c`.

For a module with CVAR rules, the patcher adds a writable and executable `.igwmonk` section with strings, handle storage, and an i386 syscall dispatcher. It redirects the `dllEntry` export to a position independent initializer that saves the real engine syscall pointer and installs the dispatcher in the DLL's original syscall global. The original `dllEntry` instructions and their relocation entries stay untouched. The PE loader entry point, `vmMain` export, and unrelated syscall arguments remain intact.

The dispatcher intercepts the standard C ABI at the engine boundary, avoiding the custom internal register conventions introduced by MSVC 9 whole program optimization. It uses `call`/`pop` and section-relative offsets, so its code and data need no additional base relocations. Existing relocations still apply to the camera instruction operands. The patcher converts RVAs through PE section headers, updates section sizes and `SizeOfImage`, and recomputes the PE checksum.
