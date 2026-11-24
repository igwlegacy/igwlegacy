# spiral-t.xml, setting by setting

What every setting in `spiral-t.xml` does, read from the game's code
(gamex86.dll, as decompiled in the reigw project; the addresses are
gamex86 RVAs). Kept here and not as XML comments because the game reads a
wave file into an 8 KB buffer and refuses the map ("Failed to load
wavefile") once the file is 8192 bytes or more (`G_LoadWaveScript`,
0x64730).

Values the file does not set come from `skill-t.cfg` (exec'd first) or the
built-in defaults. Everything under `SKILLSETTINGS` and `AI_SETTINGS` is
written into cvars and never reset, so a later map whose wave file leaves
a setting out keeps this map's value.

Sides: the players are Atelia (team 1), the NPCs the Confederation.

## How a round plays out with this file

- First wave check 8 to 12 s into the round (`sk_wavespawn_delay` 10000
  give or take `sk_wavespawn_random` 2000), then one every 10 s, fixed in
  code (`G_RunWaves`, 0x6d7b0).
- A wave is only sent while live conscripts + 4 per live vehicle are fewer
  than `sk_npc_treshold` (20, from skill-t.cfg; this file does not set it).
  So the next wave comes once the previous one is mostly dead.
- Which wave: by the round's age. Before `SMALLWAVE_TIME` (70 s) a random
  `size="0"` wave, before `MEDIUMWAVE_TIME` (200 s) a random `size="1"`
  one, after that a random `size="2"` one. In this file the biggest waves
  (80 conscripts and up to 4 vehicles) are the `size="0"` ones, so they
  come first, and the last phase has the smallest (15 to 31 conscripts).
  The stock scripts go the other way round.
- Roamers ("commandos": grenadiers, legionnaires, herrads) start 0 to 2 s
  into the round and are topped up continuously (see `COMMANDOS`).
- The officer first appears 220 s after the map started.
- Players win when the Confederate morale reaches 0 (about 1000 infantry
  kills here), and lose when Atelia's morale reaches 0 (in practice by
  losing strongholds).

## MAXCOUNTS

| setting | value | what it does |
| --- | --- | --- |
| `CONSCRIPTS` | 100 | Most live conscripts at once (cap 200, default 200). Each wave is cut to `CONSCRIPTS` minus the live ones before it is sent (`G_SendWave`, 0x6d840), then scaled by the players (see below). |
| `VEHICLES` | 4 | Most live vehicles at once (scarab, ontos, walker, boss walker; cap 16, default 4). A wave's vehicles are cut to what is left, boss walkers kept first, then ontos, scarabs, walkers. |
| `BOTS` | 3 | Friendly bots on the players' team (cap 8, default 8). Once a second a bot is added while humans x 3 + bots < ordered bots + `BOTS` + 3, removed while it is more (`G_CheckMinimumPlayers`): with `BOTS` 3 that is 2 bots for one human and none from two humans on. `bot_minplayers` still caps the bots. |
| `OFFICERS` | 1 | No effect: clamped to 1, and the roamer spawner sets it to 1 again; there is never more than one officer. |
| `COMMANDOS` | 100 | Target for the roamer spawner (cap 100, default 100), times the player scale (70 solo). While fewer NPCs live than that, every tick sends 1 to 3 roamers. All live NPCs count, wave conscripts and vehicles too, so while the waves keep 70+ alive no roamers come. |

### Player scaling (`sk_level_*` / `sk_plevel_*`, from skill-t.cfg)

Wave conscripts (not vehicles) and the `COMMANDOS` target are multiplied
by 70 % for 1 human player, 80 % for 2, 90 % for 3 to 4, 100 % for 5 to 8
(bots do not count). `g_fullPowerNPC 1` keeps it at 100 %. Example: an
80-conscript wave solo is 80 cut to the room under `CONSCRIPTS`, then
x 0.7 = 56.

## WAVESETTINGS

| setting | value | what it does |
| --- | --- | --- |
| `MINWAVEDIST_SMALL` | 2000 | Least ms since the last wave before a small-phase wave may come (default 40000). |
| `MINWAVEDIST_MEDIUM` | 1500 | The same in the medium phase (default 24000). |
| `MINWAVEDIST_LARGE` | 1000 | The same in the large phase (default 12000). All three are below the fixed 10 s wave check, so here they never hold a wave back; `sk_npc_treshold` decides. |
| `ROAMERINTERVAL` | 1000 | After a roamer tick that sent anyone, the next tick is this + 0 to 5000 ms later (default 20000). A tick that sent nobody tries again next frame. |
| `SMALLWAVE_TIME` | 70000 | Round age (ms) until which the small waves (`size="0"`) are picked (default 600000). |
| `MEDIUMWAVE_TIME` | 200000 | Round age until which the medium waves (`size="1"`) are picked, the large (`size="2"`) after it (default 1200000). Bombers also only come after it. |

`LOSEROUTES` (a list of route numbers) is parsed too; this file has none.

## SKILLSETTINGS

Each element sets the cvar of its name, overriding skill-t.cfg.

### Morale

Morale is worked out every 10 s (`G_UpdateMorale`, 0x416f0). The kill
costs are taken off at once.

| setting | value | what it does |
| --- | --- | --- |
| `sk_morale_max` | 400 | No effect: no binary of the game reads a cvar of this name. |
| `sk_morale_start` | 400 | No effect, likewise. |
| `sk_Amorale_max` | 400 | Atelia's (the players') morale ceiling. Also the scale of the morale percent shown to players and bots. |
| `sk_Amorale_start` | 400 | Atelia's morale at round start. |
| `sk_Cmorale_max` | 1000 | The Confederation's (the NPCs') morale ceiling. |
| `sk_Cmorale_start` | 1000 | The Confederation's morale at round start. Players win when it reaches 0 (not in survival). |
| `sk_deadstronghold_morale` | 300 | Atelia loses this every 10 s for each stronghold it no longer holds. With a ceiling of 400 one lost stronghold ends the round within about 20 s unless the held ones make up for it. |
| `sk_stronghold_morale` | 5 | Atelia gains this every 10 s for each stronghold it holds. |
| `sk_officer_morale` | 7 | The Confederation gains this every 10 s while the officer lives. |
| `sk_deadofficer_morale` | 21 | The Confederation loses this every 10 s while the officer is dead and waiting to respawn. |
| `sk_player_moralecost` | 3 | Atelia loses this when a human player dies in a running round. |
| `sk_bot_moralecost` | 3 | Atelia loses this when a bot dies. |
| `sk_npc_moralecost` | 1 | The Confederation loses this per NPC killed (not the officer, and not a suicide). |
| `sk_vehicle_moralecost` | 10 | The Confederation loses this per walker or tank destroyed. |

### Officer

| setting | value | what it does |
| --- | --- | --- |
| `sk_officer_firstspawntime` | 220000 | The officer first comes this many ms after the map started (`level.startTime`, so time spent in warmup counts), at a random route's start. |
| `sk_officer_respawntime` | 30000 | A killed officer comes back this many ms after his death (the same one, at full health, at a random route's start). |

### Other

| setting | value | what it does |
| --- | --- | --- |
| `sk_immortal_time` | 1000 | Spawn protection: a spawned player takes no damage for this many ms. |
| `sk_max_food` | 4 | Food items out on the map at once, on random spots among the map's food items. |
| `sk_max_jewel` | 3 | Jewels out at once, likewise. |
| `sk_wavespawn_delay` | 10000 | First wave check this many ms after the round started... |
| `sk_wavespawn_random` | 2000 | ...give or take up to this many. |
| `sk_roamerspawn_delay` | 1000 | First roamer tick this many ms after the round started... |
| `sk_roamerspawn_random` | 1000 | ...give or take up to this many. |
| `sk_numbombers` | 0 | Bombers the roamer spawner keeps alive (scaled by the players, at least 1 if above 0), once `MEDIUMWAVE_TIME` has passed and over half the `COMMANDOS` target lives. 0: no bombers at all. |

## AI_SETTINGS

| setting | value | cvar | what it does |
| --- | --- | --- | --- |
| `AIM` | 0.4 | `g_aiskillAim` (default 0.7) | Vehicle aim only: their aim point is off at random by up to (1 - AIM) x 30, 30, 25 units (here 18, 18, 15). |
| `GANGSIZE` | 5 | `g_aiskillGangsize` (default 2) | Officer's squad size, officer included, capped at 4: here the officer plus 3 roamers. A roamer looks for an officer with room at most every 3 s and joins the one it reaches fastest. |
| `SQUADSIZE` | 20 | `g_npcSquadSize` (default 3) | Size of the leaderless roamer groups, leader included, no cap. 20 makes the roamers gather into one or two big groups that move together on a random route. Herrads, bombers, spies and the officer never join. |
| `REACTIONTIME` | 200 | `g_aiskillReactiontime` (default 400) | Ms an NPC waits after noticing a target before it fires (skipped near a radio); for vehicles per gun. |
| `WAVEDISTANCE` | 70 | `npc_wavedist` (default 30, clamped 15 to 250) | Wave spacing: an infantry NPC's speed is scaled by its gap to the one ahead divided by this, between 0.6 and 2, so it catches up or falls back. Larger spreads a wave out. |

Not set here, so whatever was set last (default 1 each):
`SCALE_GRENADIER`, `SCALE_LEGIONNAIRE`, `SCALE_HERRAD`, the weights the
roamers' classes are picked by. `DM_CIVS` is for deathmatch only.

## WAVE

`<WAVE routes="1 2 3" size="2">` with a count per class.

- `routes`: the routes (1 to 25) the wave may take; one is picked at
  random per wave. A wave with vehicles prefers a route whose stronghold
  still stands; if none does and 2 or more vehicles are already out, the
  wave comes without its vehicles (`G_PickWaveRoute`, 0x6da10).
- `size`: the phase it belongs to (0 small, 1 medium, 2 large, see
  WAVESETTINGS); anything else counts as 0. Players hear the small,
  medium or large wave alarm accordingly.
- `CONSCRIPT`, `SCARAB`, `ONTOS`, `WALKER`, `BOSS_WALKER`: the counts.
  A class named twice adds up, so `<SCARAB>1</SCARAB>` twice is 2 scarabs.
  Other class names (`GRENADIER`, `OFFICER`, ...) are read but never sent
  by a wave.
- `TEMPLAR_RATIO` (not used in this file): the share of the conscripts
  that get the Templar machine gun. Without it, 0: no Templars here.
- At most 30 waves per file.

The waves here:

| size | phase | waves |
| --- | --- | --- |
| 0 | 0 to 70 s | 80 + 2 scarabs + ontos; 80 + 2 scarabs + ontos + walker; 2 + 2 scarabs + ontos |
| 1 | 70 to 200 s | 35, 55, 45, 50, each + ontos |
| 2 | after 200 s | 31 + walker, 30 + walker, 25 + ontos, 15 + walker |

## deathhand, radiospot, rocketspot

Spots for the Scorched Earth expansion only (without it these are
skipped), read from the first wave file of a map only. `x`, `y`, `z` are
map coordinates (`z` gets +2), `yaw` the facing in degrees.

- `deathhand`: where the death hand can be built.
- `radiospot`: the radio pool's spots (at most 10); `sk_max_radio` of
  them have a radio at once.
- `rocketspot`: the rocket launcher pool's spots (at most 10);
  `sk_max_rocket` of them have one at once.

The first `rocketspot` is empty: it still takes a slot, at 0 0 0 with yaw
0, so a rocket launcher can land at the map's origin. The line after it,
`(-989 1086 1120) : 4`, is text outside any element and is ignored; it
looks like the coordinates meant for that spot.
