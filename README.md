# Iron Grip: Warlord Legacy
As long as <https://github.com/reigw/reigw> hasn't finished, `IGWL` will be
rather minimal and focus on modifying assets and monkey patching existing
binaries to start with the low-hanging fruits first.

# Build
Put the original IGW `gamex86.dll`, `cgamex86.dll`, `uix86.dll`, and `igwarlord.exe` in
`./input-binaries/`, along with the dedicated server's `server/igwarlord.exe`
renamed to `igwarlord-server.exe`. A native C17 compiler, `idt3tc`, and Blender must be
available on `PATH`. Models are exported through the vendored add-on in
`./model-converter/`, so fetch the submodule first.

```sh
git submodule update --init
pixi run build
```

The output in `./dist/` mirrors an IGW installation: the patched
`igwarlord.exe` as `igwlegacy.exe` at the top, the patched dedicated server
as `server/igwarlord.exe` with copies of the patched `gamex86.dll` and
the PK3 in `server/base/` and the contents of `./serverdir/` (the
`start-server.sh` wine launcher and a `server.cfg` with dead master
servers removed and netcode settings), and `base/` with
`gamex86.dll`, `cgamex86.dll`, `uix86.dll`, and
`igwlegacy.pk3`, a plain ZIP archive of the contents of `./gamedir/`
plus the assets generated from `./raw-assets/`: textures converted with
`idt3tc` and Blender models exported as static MD3s by
[`model-converter/export_md3.py`](model-converter/export_md3.py). The
`TEXTURE_RULES` and `MODEL_RULES` in `dodo.py` map game paths to their sources.
All five binaries pass through the monkey patcher. Neither executable imports Steam anymore, so `steam_api.dll` and the dedicated server's `steam_appid.txt`, `steamclient.dll`, `Steam.dll`, `tier0_s.dll`, and `vstdlib_s.dll` need not ship; the game behaves as it already did whenever Steam was not running, and dedicated servers no longer register with the Steam master server. The executable defaults `r_forceFog` to `1` and `r_forceFogDensity` to `0`; existing settings still take precedence. It forces `r_useShaders` to `1` and `r_heathazedistort` to `0` regardless of saved or runtime settings, skips the intro video at startup, and mirrors all console output to stdout. The UI patch removes the
main-menu promotional panel and its update request, and automatically enters the game once loading finishes without requiring a `PLAY!` click. The main menu's `MULTIPLAYER` button reads `JOIN`, and the server browser it opens starts on `LAN` instead of `Internet`. Its refresh no longer shows `Press SPACE to stop`, as SPACE never stopped it. Its `STOP` button stops a refresh at once instead of restarting the scan against the second master server while no server has answered yet, which happened even on `LAN`. The browser's `Visit www.igwdeath.com for the leaderboard!` banner and `Visit www.igwarlord.com for the latest news!` line are gone as well. The host-game menu's `Game mode:` dropdown calls its coop entry `VANILLA`, which stays the default, and the menu opens on the last selected mode again, also after restarting the game. A new `RESISTANCE` entry between `VANILLA` and `DEATHMATCH` starts a coop game with friendly fire off, no difficulty multiplier and the `GOD` difficulty, greys out those controls, shows the mode's badge above `Map:` and sets the new server CVAR `g_legacygametype` to `1`; the server then enforces those rules, also on dedicated servers with `set g_legacygametype 1`, and the server browser, loading screen and scoreboard name the mode. Instead of god's files the server execs `skill-res.cfg` and uses `scripts/waves/<map>-res.lua`, `.toml` or `.xml`, so a map needs one of those to load in `RESISTANCE`. The loading screen's `Maximum Players:` line reads `Mode:` with `Vanilla`, `Resistance`, `Deathmatch`, `Survival` or `Teamkill` from the server's `g_gametype` and `g_legacygametype`, and its `Difficulty:` line only shows for `Vanilla`.

A sixth difficulty, `TITAN` (`sv_difficulty 5`), follows god in the host-game and vote menus, `callvote difficulty` and `changedifficulty`, and shows on the loading screen, scoreboard and server browser. Like the retail levels it reads `skill-t.cfg` from the archive root (`gamedir/skill-t.cfg`, not `gamedir/resources/`) and `scripts/waves/<map>-t.xml`. Survival and teamkill keep their own difficulty handling.

For every difficulty, a `scripts/waves/<map>-<level>.toml` takes precedence over the `.xml` of the same name. It holds the same settings and spots, but its `[[WAVE]]`s are sent one after the other in file order, each one `startup` milliseconds after the previous one, and the last wave repeats. See [monkey-patcher/docs/README.md](monkey-patcher/docs/README.md#toml-wave-scripts) for the format.

A `scripts/waves/<map>-<level>.lua` takes precedence over both. It replaces all retail wave and roamer spawning: its `frame(t)` function runs every server frame of a round and spawns NPCs, prints banners and plays sounds itself, deterministically. See [monkey-patcher/docs/README.md](monkey-patcher/docs/README.md#lua-wave-scripts) for the API.

A wave script's `MAXCOUNTS` `VEHICLES` may ask for up to 16 live vehicles instead of retail's 4, in both the `.xml` and the `.toml`; without it the default stays 4.

A weapon, upgrade or structure whose cost in the skill config is 10000 or more cannot be bought. In the in-game shop, the spawn weapon and upgrade pages, the RTS build menu and the RTS structure upgrade button its button turns a dark grey, distinct from the regular looks including the grey of merely unaffordable structures, ignores clicks and shows no price, and an unpurchasable upgrade tier greys out all later tiers of its chain too. The server refuses such weapons and upgrades as well, even when sale credit would cover them.

Archive paths start directly with `scripts/`, `sound/`, etc. Sorted entries,
fixed timestamps and permissions, and a fixed compression level make the
archive reproducible for unchanged contents in the same Pixi environment.
