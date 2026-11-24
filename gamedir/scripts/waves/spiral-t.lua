-- Spiral on titan, as a Lua wave script. It takes precedence over
-- spiral-t.toml and spiral-t.xml, and shows every setting and every igw
-- function; see "Lua wave scripts" in monkey-patcher/docs/README.md.
--
-- The campaign: the XML's eleven waves, one a minute once the field is
-- clear enough, then endless waves that keep growing. An officer from the
-- start, roaming grenadiers, legionnaires and Herrads with the waves,
-- spies and engineers after three minutes, and a boss walker at 12:00.
--
-- The top level only declares settings, locals and functions; frame(t)
-- does everything else. Every round (and every reload during one) runs
-- this file again in a fresh state.

------------------------------------------------------------------------
-- Settings, read once after the top level ran
------------------------------------------------------------------------

-- Set after the skill config, as the XML's SKILLSETTINGS. Booleans become
-- "1" and "0". g_npcspeed is here so that the boss walker's speed-up
-- below ends with the round.
cvars = {
    sk_morale_max = 400,
    sk_morale_start = 400,
    sk_deadstronghold_morale = 300,
    sk_stronghold_morale = 5,
    sk_deadofficer_morale = 21,
    sk_officer_morale = 7,
    sk_officer_respawntime = 30000,

    sk_Cmorale_max = 1000,
    sk_Amorale_max = 400,
    sk_Cmorale_start = 1000,
    sk_Amorale_start = 400,
    sk_officer_firstspawntime = 1000,

    sk_player_moralecost = 30,
    sk_bot_moralecost = 3,
    sk_npc_moralecost = 1,
    sk_vehicle_moralecost = 10,

    sk_max_food = 4,
    sk_max_jewel = 3,

    sk_npc_treshold = 30,
    g_npcspeed = 250,
    g_allowVote = true,
}

-- At most three bots join the defenders.
limits = { bots = 3 }

-- Spots are { x, y, z, yaw } (or intensity), or the same with named
-- fields; both forms are used below.

-- The defenders' spawn spots in deathmatch only: Spiral's own ig_player
-- positions.
spawnpoints = {
    { 704, 200, 294, 270 },
    { -40, 800, 294, 270 },
    { 608, -32, 294, 270 },
    { -1376, 840, 414, 180 },
    { -256, -896, 422, 180 },
    { -168, 680, 286, 0 },
    { -1344, 1096, 414, 180 },
    { -968, -816, 286, 180 },
    { 352, 696, 286, 180 },
    { -1008, -680, 286, 180 },
    { -32, -1184, 286, 270 },
    { -1464, 48, 286, 270 },
    { 856, -88, 286, 270 },
}

-- Retail Spiral's two artillery batteries.
artillery = {
    { -2671, -3244, 400, 1 },
    { x = 2504, y = -2585, z = 390, intensity = 1 },
}

-- Retail parses superweapon spots but never uses them; Spiral's
-- ig_explosion_point positions stand in here.
superweapons = {
    { -128, 256, 256 },
    { -512, -128, 256 },
    { x = -128, y = 1024, z = 256 },
}

-- The expansion's spots, from spiral-t.xml.
deathhand = {
    { -1906, -1640, 50, 85 },
    { 1666, -1236, 50, 130 },
    { -2283, 1983, 178, -73 },
    { -2630, -438, 306, 6 },
}

radiospots = {
    { -281, -1056, 434, -137 },
    { -638, -247, 54, -42 },
    { 473, 1429, 182, -76 },
    { 1092, 1069, 306, 159 },
    { 821, -1709, 178, 26 },
    { -379, -1971, 122, 0 },
    { -1586, 2369, 306, 133 },
    { -329, 1612, 306, -50 },
}

rocketspots = {
    { -2617, 1223, 306, 88 },
    { -1212, -1209, 306, -54 },
    { 1199, -1492, 180, -126 },
    { 361, 1084, 310, -147 },
    { -184, 2112, 178, 144 },
    { x = 1619, y = 866, z = 306, yaw = -117 },
}

------------------------------------------------------------------------
-- The campaign
------------------------------------------------------------------------

local SECOND = 1000
local MINUTE = 60 * SECOND

local FIRST_WAVE = 10 * SECOND  -- the first wave's time
local WAVE_GAP = MINUTE         -- the planned time between waves
local MIN_WAVE_GAP = 20 * SECOND -- a late wave still keeps this distance
local BOSS_AT = 12 * MINUTE      -- the boss walker's time
local SPECIALS_FROM = 3 * MINUTE -- spies and engineers from here on
local SPECIALS_GAP = 45 * SECOND
local STATUS_GAP = 5 * MINUTE    -- a console status line this often
local PENDING_FOR = 2 * MINUTE   -- refused spawns are retried this long

local MAX_CONSCRIPTS = 100
local MAX_VEHICLES = 4
local MAX_ROAMERS = 8
local VEHICLES = { "scarab", "ontos", "walker", "boss_walker" }

local SOUND = {
    small = "sound/gameplay/waveattack_small.wav",
    medium = "sound/gameplay/waveattack_medium.wav",
    large = "sound/gameplay/waveattack_large.wav",
    start = "sound/voices/DM_Round-Start-01.wav",
    armour = "sound/voices/atelia/clean_EnemyArmourSpotted.wav",
    held = "sound/voices/atelia/clean_EVA_HeldOffAttacksForNow.wav",
    officer = "sound/voices/atelia/clean_EVA_GoodWork.wav",
    boss = "sound/gameplay/Deathhand-Incoming-Final.wav",
}

-- The XML's waves, with roamers added. size picks the attack sound.
-- Roamers are lists, not tables keyed by class, so they spawn in a fixed
-- order (pairs() would be deterministic too, but lists read better).
local WAVES = {
    { size = "large", conscripts = 31, vehicles = { "walker" } },
    { size = "large", conscripts = 30, vehicles = { "walker" } },
    { size = "large", conscripts = 25, vehicles = { "ontos" },
      roamers = { { "grenadier", 2 } } },
    { size = "large", conscripts = 15, vehicles = { "walker" } },
    { size = "medium", conscripts = 35, vehicles = { "ontos" },
      roamers = { { "grenadier", 2 }, { "legionnaire", 1 } } },
    { size = "medium", conscripts = 55, vehicles = { "ontos" },
      roamers = { { "herrad", 1 } } },
    { size = "medium", conscripts = 45, vehicles = { "ontos" } },
    { size = "medium", conscripts = 50, vehicles = { "ontos" },
      roamers = { { "grenadier", 3 }, { "legionnaire", 2 } } },
    { size = "small", conscripts = 80, vehicles = { "scarab", "ontos", "scarab" } },
    { size = "small", conscripts = 80,
      vehicles = { "scarab", "ontos", "scarab", "walker" },
      roamers = { { "herrad", 1 } } },
    { size = "small", conscripts = 2, vehicles = { "scarab", "ontos", "scarab" } },
}

-- The waves after the XML's: bigger every time, with a random vehicle or
-- two and a Herrad every fifth wave. math.random has a fixed seed, so
-- every round on every server rolls the same.
local function endless_wave(k)
    local extra = k - #WAVES
    local pool = { "scarab", "ontos", "walker" }
    local vehicles = { pool[math.random(#pool)] }
    if extra % 3 == 0 then
        vehicles[2] = pool[math.random(#pool)]
    end
    local roamers = { { "grenadier", 2 }, { "legionnaire", 1 } }
    if extra % 5 == 0 then
        roamers[3] = { "herrad", 1 }
    end
    return { size = "small", conscripts = 50 + 5 * extra, vehicles = vehicles,
             roamers = roamers }
end

------------------------------------------------------------------------
-- State of this round, fresh in every new state
------------------------------------------------------------------------

local started = false       -- the first frame has run
local level = 0             -- igw.difficulty(), 0 easy to 5 titan
local next_wave = 1         -- the next wave's number
local last_sent = -MINUTE   -- when the last wave went out
local held = true           -- the last wave was announced as held off
local pending = {}          -- spawns the game refused, retried each second
local next_tick = 0         -- the next once-a-second check
local officer_up = false    -- our officer lives
local officer_at = 0        -- when to try the next officer
local specials_at = SPECIALS_FROM
local next_special = 1      -- alternates spies and engineers
local boss_sent = false
local status_at = STATUS_GAP

-- "m:ss" of a round time.
local function clock(t)
    return string.format("%d:%02d", t // MINUTE, t % MINUTE // SECOND)
end

-- A number cvar, or the default for an unset or non-number one.
local function cvar_number(name, default)
    return tonumber(igw.cvar(name)) or default
end

-- The vehicles alive, of every class.
local function vehicles_alive()
    local n = 0
    for _, class in ipairs(VEHICLES) do
        n = n + igw.alive(class)
    end
    return n
end

-- The roamers alive: the classes that roam from where they spawn.
local function roamers_alive()
    return igw.alive("grenadier") + igw.alive("legionnaire") + igw.alive("herrad")
end

-- igw.spawn, keeping what the game refuses for now (no free NPC record
-- or wave number) to retry for a while. Returns how many spawned.
local function send(t, class, route, n, opts)
    if n <= 0 then
        return 0
    end
    local spawned = igw.spawn(class, route, n, opts)
    if spawned < n then
        pending[#pending + 1] = { class = class, route = route, n = n - spawned,
                                  opts = opts, until_t = t + PENDING_FOR }
    end
    return spawned
end

-- Retries the refused spawns, dropping those that waited too long.
local function retry(t)
    local keep = {}
    for _, p in ipairs(pending) do
        if t < p.until_t then
            p.n = p.n - igw.spawn(p.class, p.route, p.n, p.opts)
            if p.n > 0 then
                keep[#keep + 1] = p
            end
        end
    end
    pending = keep
end

-- Splits the conscripts evenly over all routes, the remainder going to
-- the routes after a random first one.
local function send_conscripts(t, count, templar)
    local routes = igw.routes()
    local each, rest = count // routes, count % routes
    local first = math.random(routes)
    for i = 0, routes - 1 do
        local route = (first + i - 1) % routes + 1
        send(t, "conscript", route, each + (i < rest and 1 or 0), { templar = templar })
    end
end

-- Sends wave k: conscripts scaled by players and difficulty but capped,
-- the wave's vehicles up to the vehicle cap, and its roamers.
local function send_wave(t, k)
    local wave = WAVES[k] or endless_wave(k)
    local players = math.max(igw.players(), 1)
    local wanted = wave.conscripts + (players - 1) * (2 + level)
    local count = math.min(wanted, MAX_CONSCRIPTS - igw.alive("conscript"))
    -- More Templar machine guns as the round goes on, more on harder levels.
    local templar = math.min(0.04 * k, 0.1 * level)
    send_conscripts(t, math.max(count, 0), templar)

    local armour = 0
    for _, class in ipairs(wave.vehicles or {}) do
        if vehicles_alive() < MAX_VEHICLES then
            armour = armour + send(t, class, math.random(igw.routes()), 1)
        end
    end
    for _, roamer in ipairs(wave.roamers or {}) do
        local class, n = roamer[1], roamer[2]
        n = math.min(n, MAX_ROAMERS - roamers_alive())
        send(t, class, math.random(igw.routes()), n)
    end

    igw.banner(string.format("WAVE %d", k))
    igw.sound(SOUND[wave.size])
    print(string.format("wave %d at %s: %d conscripts (templar %.2f), %d vehicles",
                        k, clock(t), count, templar, armour))
end

-- The first frame of a state: a new round, or a reload during one, which
-- picks up the campaign where the clock says it is.
local function start(t)
    started = true
    level = igw.difficulty()
    officer_at = cvar_number("sk_officer_firstspawntime", SECOND)
    -- After a reload, the last state's NPCs are still around.
    officer_up = igw.alive("officer") > 0
    if t < 5 * SECOND then
        igw.banner("SPIRAL: HOLD THE CITY")
        igw.sound(SOUND.start)
    else
        next_wave = math.max((t - FIRST_WAVE) // WAVE_GAP + 2, 1)
        boss_sent = t >= BOSS_AT
        status_at = (t // STATUS_GAP + 1) * STATUS_GAP
        specials_at = math.max(t, SPECIALS_FROM)
        print(string.format("reloaded at %s, continuing with wave %d", clock(t), next_wave))
    end
end

-- Sends the next wave once it is due, the last one is far enough away and
-- the field is clear enough (sk_npc_treshold, as retail's scheduler).
local function waves(t)
    local due = FIRST_WAVE + (next_wave - 1) * WAVE_GAP
    if t < due or t - last_sent < MIN_WAVE_GAP then
        return
    end
    if igw.alive() >= cvar_number("sk_npc_treshold", 30) then
        return
    end
    send_wave(t, next_wave)
    next_wave = next_wave + 1
    last_sent = t
    held = false
end

-- Announces a wave as held off once its conscripts and vehicles are gone.
local function held_off(t)
    if held or igw.alive("conscript") + vehicles_alive() > 0 or #pending > 0 then
        return
    end
    held = true
    local due = FIRST_WAVE + (next_wave - 1) * WAVE_GAP
    igw.banner(string.format("^2WAVE %d HELD OFF\n^7The next one comes in %d s",
                             next_wave - 1, math.max(due - t, 0) // SECOND))
    igw.sound(SOUND.held)
end

-- Keeps one officer around, the next one sk_officer_respawntime after the
-- last one fell. A second officer would be refused anyway.
local function officer(t)
    if officer_up then
        if igw.alive("officer") == 0 then
            officer_up = false
            officer_at = t + cvar_number("sk_officer_respawntime", 30 * SECOND)
            igw.banner("^2THE ENEMY OFFICER IS DOWN")
            igw.sound(SOUND.officer)
        end
    elseif t >= officer_at then
        if igw.spawn("officer", math.random(igw.routes())) == 1 then
            officer_up = true
        else
            officer_at = t + 5 * SECOND
        end
    end
end

-- A spy or an engineer, alternating, while fewer than two are about.
local function specials(t)
    if t < specials_at then
        return
    end
    specials_at = t + SPECIALS_GAP
    if igw.alive("spy") + igw.alive("engineer") < 2 then
        local class = next_special == 1 and "spy" or "engineer"
        if igw.spawn(class, math.random(igw.routes())) == 1 then
            next_special = 3 - next_special
        end
    end
end

-- The boss walker, with faster infantry for the rest of the round.
local function boss(t)
    if boss_sent or t < BOSS_AT then
        return
    end
    if vehicles_alive() >= MAX_VEHICLES
        or igw.spawn("boss_walker", math.random(igw.routes())) == 0 then
        return  -- tried again next second
    end
    boss_sent = true
    local speed = cvar_number("g_npcspeed", 250)
    igw.cvar("g_npcspeed", math.floor(speed * 1.15))
    igw.banner("A BOSS WALKER APPROACHES")
    igw.sound(SOUND.boss)
    print(string.format("boss walker at %s, infantry speed %s -> %s",
                        clock(t), speed, igw.cvar("g_npcspeed")))
end

-- A status line on the server console every few minutes.
local function status(t)
    if t < status_at then
        return
    end
    status_at = status_at + STATUS_GAP
    print(string.format("%s: wave %d, %d enemies alive (%d conscripts, %d vehicles), %d defenders",
                        clock(t), next_wave - 1, igw.alive(), igw.alive("conscript"),
                        vehicles_alive(), igw.players()))
end

-- Called every server frame with the milliseconds since the round's start.
function frame(t)
    if not started then
        start(t)
    end
    waves(t)
    if t < next_tick then
        return
    end
    -- The rest once a second.
    next_tick = t + SECOND
    retry(t)
    held_off(t)
    officer(t)
    specials(t)
    boss(t)
    status(t)
end
