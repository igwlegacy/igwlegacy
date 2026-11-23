/* TOML wave scripts. The payload calls toml_waves_load() at the start of
 * G_LoadWaveScript and toml_waves_run() from G_RunWaves, right after the
 * roamers spawn. With scripts/waves/<map>-<level>.toml present, the TOML
 * replaces the XML. It sets every value the XML loader sets, with the same
 * defaults, caps and first-script-only rules. Its [[WAVE]]s are sent one
 * after the other in file order, each its startup milliseconds after the
 * previous one, the first after the round's start. The last one repeats
 * every startup milliseconds. A wave's size only picks the attack sound.
 */
#include "gamex86.h"
#include "tomlc17.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_WAVES 128
/* How long a due wave waits for room before the next try. */
#define RETRY_TIME 1000
/* Larger files are surely not wave scripts. */
#define MAX_FILE_SIZE (1 << 20)

/* The loaded script and the round's progress through its waves. */
static bool active;
static game_wave_t waves[MAX_WAVES];
static int startup[MAX_WAVES];
static unsigned char sizes[MAX_WAVES];
static int num_waves;
static bool restart;
static int round_start;
static int sent;
static int last_sent;
static int retry_time;

/* The file being loaded, for messages and for cleanup before an error. */
static const char *loading;
static char *source;
static toml_result_t parsed;
static bool parsed_valid;

static void release(void)
{
    if (parsed_valid)
        toml_free(parsed);
    parsed_valid = false;
    free(source);
    source = NULL;
}

static void print(const char *format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    game_syscall(G_PRINT, text);
}

/* Drops the map like the XML loader's own errors; G_ERROR does not return,
 * so everything allocated is freed first.
 */
__attribute__((noreturn))
static void fail(const toml_datum_t *where, const char *format, ...)
{
    char reason[256], text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(reason, sizeof(reason), format, args);
    va_end(args);
    if (where && where->lineno)
        snprintf(text, sizeof(text), "%s, line %d: %s\n", loading, where->lineno, reason);
    else
        snprintf(text, sizeof(text), "%s: %s\n", loading, reason);
    release();
    active = false;
    game_syscall(G_ERROR, text);
    __builtin_unreachable();
}

static void unknown(const char *section, const char *key)
{
    print("^3WARNING: %s: unknown key %s%s%s, ignored\n", loading,
          section ? section : "", section ? "." : "", key);
}

static bool is(const char *key, const char *name)
{
    return !strcmp(key, name);
}

/* Like the XML loader's atoi and atof, numbers may also be given as
 * strings; floats where integers are expected are truncated.
 */
static int to_int(const toml_datum_t *d, const char *key)
{
    switch (d->type) {
    case TOML_INT64:
        if (d->u.int64 > INT_MAX || d->u.int64 < INT_MIN)
            fail(d, "%s is out of range", key);
        return (int)d->u.int64;
    case TOML_FP64:
        if (!(d->u.fp64 < 2147483648.0 && d->u.fp64 > -2147483649.0))
            fail(d, "%s is out of range", key);
        return (int)d->u.fp64;
    case TOML_BOOLEAN:
        return d->u.boolean;
    case TOML_STRING:
        return atoi(d->u.s);
    default:
        fail(d, "%s must be a number", key);
    }
}

static double to_double(const toml_datum_t *d, const char *key)
{
    switch (d->type) {
    case TOML_INT64:
        return (double)d->u.int64;
    case TOML_FP64:
        return d->u.fp64;
    case TOML_BOOLEAN:
        return d->u.boolean;
    case TOML_STRING:
        return strtod(d->u.s, NULL);
    default:
        fail(d, "%s must be a number", key);
    }
}

/* The shortest of up to six decimals, e.g. 0.6 and not 0.600000. */
static void format_double(double value, char *text, size_t size)
{
    if (!(value == value) || value > 1e15 || value < -1e15) {
        snprintf(text, size, "%s", value == value ? (value > 0 ? "1e15" : "-1e15") : "0");
        return;
    }
    const char *sign = value < 0 ? "-" : "";
    if (value < 0)
        value = -value;
    long long whole = (long long)value;
    int fraction = (int)((value - (double)whole) * 1e6 + 0.5);
    if (fraction >= 1000000)
        ++whole, fraction -= 1000000;
    if (!whole && !fraction)
        sign = "";
    int n = snprintf(text, size, "%s%lld.%06d", sign, whole, fraction);
    /* Trim the zeros and a bare point. */
    while (n > 0 && (size_t)n < size && text[n - 1] == '0')
        text[--n] = 0;
    if (n > 0 && (size_t)n < size && text[n - 1] == '.')
        text[--n] = 0;
}

/* A cvar value: the text the XML would have held. */
static void to_string(const toml_datum_t *d, const char *key, char *text, size_t size)
{
    switch (d->type) {
    case TOML_STRING:
        snprintf(text, size, "%s", d->u.s);
        break;
    case TOML_INT64:
        snprintf(text, size, "%lld", (long long)d->u.int64);
        break;
    case TOML_FP64:
        format_double(d->u.fp64, text, size);
        break;
    case TOML_BOOLEAN:
        snprintf(text, size, "%d", d->u.boolean ? 1 : 0);
        break;
    default:
        fail(d, "%s must be a string or a number", key);
    }
}

static void set_cvar(const char *name, const char *value)
{
    game_syscall(G_CVAR_SET, name, value);
}

/* G_IsExpansion, through the engine instead of the DLL's own helper,
 * which takes its argument in a register.
 */
static bool is_expansion(void)
{
    if (game_syscall(G_CVAR_VARIABLE_INTEGER_VALUE, "com_dedicated"))
        return game_syscall(G_CVAR_VARIABLE_INTEGER_VALUE, "sv_expansion") != 0;
    return game_syscall(G_CVAR_VARIABLE_INTEGER_VALUE, "cl_expansionSE") != 0;
}

static const toml_datum_t *table(const toml_datum_t *d, const char *key)
{
    if (d->type != TOML_TABLE)
        fail(d, "%s must be a table", key);
    return d;
}

/* Calls each() for a single table or for every table of an array. */
static void for_tables(const toml_datum_t *d, const char *key,
                       void (*each)(const toml_datum_t *, const char *))
{
    if (d->type == TOML_ARRAY) {
        for (int i = 0; i < d->u.arr.size; ++i)
            each(table(&d->u.arr.elem[i], key), key);
    } else {
        each(table(d, key), key);
    }
}

/* Route lists: "1 2 3" as in the XML, a single number, or an array. Each
 * route calls add().
 */
static void for_routes(const toml_datum_t *d, const char *key, void (*add)(int, void *),
                       void *context)
{
    if (d->type == TOML_ARRAY) {
        for (int i = 0; i < d->u.arr.size; ++i)
            add(to_int(&d->u.arr.elem[i], key), context);
    } else if (d->type == TOML_STRING) {
        const char *p = d->u.s;
        for (;;) {
            p += strspn(p, " \t,");
            if (!*p)
                break;
            char *end;
            long route = strtol(p, &end, 10);
            if (end == p)
                fail(d, "%s must hold route numbers", key);
            add((int)route, context);
            p = end;
        }
    } else {
        add(to_int(d, key), context);
    }
}

static void check_route(const toml_datum_t *d, const char *key, int route)
{
    if (route < 1 || route > MAX_ROUTES)
        fail(d, "%s has route %d, outside 1..%d", key, route, MAX_ROUTES);
}

static const toml_datum_t *current;

static void add_lose_route(int route, void *context)
{
    (void)context;
    check_route(current, "LOSEROUTES", route);
    ++GAME(int, GAME_NUM_LOSE_ROUTES);
    GAME(int, GAME_LOSE_ROUTES) |= 1 << (route - 1);
}

static void add_wave_route(int route, void *context)
{
    check_route(current, "routes", route);
    ((game_wave_t *)context)->routes |= 1 << route;
}

static void parse_max_counts(const toml_datum_t *t)
{
    for (int i = 0; i < t->u.tab.size; ++i) {
        const char *key = t->u.tab.key[i];
        const toml_datum_t *d = &t->u.tab.value[i];
        if (is(key, "CONSCRIPTS")) {
            int v = to_int(d, key);
            GAME(int, GAME_MAX_CONSCRIPTS) = v < 200 ? v : 200;
        } else if (is(key, "VEHICLES")) {
            int v = to_int(d, key);
            /* max_vehicles.c raises the XML loader's cap of 4 to 16. */
            GAME(int, GAME_MAX_VEHICLES) = v < 16 ? v : 16;
        } else if (is(key, "BOTS")) {
            int v = to_int(d, key);
            GAME(int, GAME_MAP_MAX_BOTS) = v < 8 ? v : 8;
        } else if (is(key, "OFFICERS")) {
            int v = to_int(d, key);
            GAME(int, GAME_MAX_OFFICERS) = v < 1 ? v : 1;
        } else if (is(key, "COMMANDOS")) {
            int v = to_int(d, key);
            GAME(int, GAME_MAX_COMMANDOS) = v < 100 ? v : 100;
        } else {
            unknown("MAXCOUNTS", key);
        }
    }
}

static void parse_wave_settings(const toml_datum_t *t)
{
    static const struct {
        const char *key;
        uint32_t address;
    } settings[] = {
        { "MINWAVEDIST_SMALL", GAME_MIN_WAVE_DIST_SMALL },
        { "MINWAVEDIST_MEDIUM", GAME_MIN_WAVE_DIST_MEDIUM },
        { "MINWAVEDIST_LARGE", GAME_MIN_WAVE_DIST_LARGE },
        { "ROAMERINTERVAL", GAME_ROAMER_INTERVAL },
        { "SMALLWAVE_TIME", GAME_SMALL_WAVE_TIME },
        { "MEDIUMWAVE_TIME", GAME_MEDIUM_WAVE_TIME },
    };
    for (int i = 0; i < t->u.tab.size; ++i) {
        const char *key = t->u.tab.key[i];
        const toml_datum_t *d = &t->u.tab.value[i];
        size_t s = 0;
        for (; s < sizeof(settings) / sizeof(settings[0]); ++s)
            if (is(key, settings[s].key))
                break;
        if (s < sizeof(settings) / sizeof(settings[0])) {
            GAME(int, settings[s].address) = to_int(d, key);
        } else if (is(key, "LOSEROUTES")) {
            GAME(int, GAME_NUM_LOSE_ROUTES) = 0;
            GAME(int, GAME_LOSE_ROUTES) = 0;
            current = d;
            for_routes(d, key, add_lose_route, NULL);
        } else {
            unknown("WAVESETTINGS", key);
        }
    }
}

/* Each key sets the cvar of its name, after the skill-*.cfg exec. */
static void parse_skill_settings(const toml_datum_t *t)
{
    char value[256];
    for (int i = 0; i < t->u.tab.size; ++i) {
        to_string(&t->u.tab.value[i], t->u.tab.key[i], value, sizeof(value));
        set_cvar(t->u.tab.key[i], value);
    }
}

static void parse_ai_settings(const toml_datum_t *t)
{
    static const struct {
        const char *key;
        const char *cvar;
    } settings[] = {
        { "AIM", "g_aiskillAim" },
        { "GANGSIZE", "g_aiskillGangsize" },
        { "REACTIONTIME", "g_aiskillReactiontime" },
        { "SQUADSIZE", "g_npcSquadSize" },
        { "SCALE_LEGIONNAIRE", "g_aiScaleLegionnaire" },
        { "SCALE_HERRAD", "g_aiScaleHerrad" },
        { "SCALE_GRENADIER", "g_aiScaleGrenadier" },
    };
    char value[256];
    for (int i = 0; i < t->u.tab.size; ++i) {
        const char *key = t->u.tab.key[i];
        const toml_datum_t *d = &t->u.tab.value[i];
        size_t s = 0;
        for (; s < sizeof(settings) / sizeof(settings[0]); ++s)
            if (is(key, settings[s].key))
                break;
        if (s < sizeof(settings) / sizeof(settings[0])) {
            to_string(d, key, value, sizeof(value));
            set_cvar(settings[s].cvar, value);
        } else if (is(key, "WAVEDISTANCE")) {
            int distance = to_int(d, key);
            distance = distance < 15 ? 15 : distance > 250 ? 250 : distance;
            snprintf(value, sizeof(value), "%d", distance);
            set_cvar("npc_wavedist", value);
        } else if (is(key, "DM_CIVS")) {
            GAME(int, GAME_DM_CIVS) = to_int(d, key);
        } else {
            unknown("AI_SETTINGS", key);
        }
    }
}

/* The x, y and z of every spot kind, with yaw and intensity where the
 * kind has them. Like the XML, the spots drawn into the map get z + 2.
 */
typedef struct {
    float origin[3];
    float yaw;
    float intensity;
} spot_t;

static spot_t parse_spot(const toml_datum_t *t, const char *section, bool lift,
                         bool has_yaw, bool has_intensity)
{
    spot_t spot = { { 0, 0, 0 }, 0, 0 };
    for (int i = 0; i < t->u.tab.size; ++i) {
        const char *key = t->u.tab.key[i];
        const toml_datum_t *d = &t->u.tab.value[i];
        if (is(key, "x"))
            spot.origin[0] = (float)to_double(d, key);
        else if (is(key, "y"))
            spot.origin[1] = (float)to_double(d, key);
        else if (is(key, "z"))
            spot.origin[2] = (float)to_double(d, key) + (lift ? 2 : 0);
        else if (has_yaw && is(key, "yaw"))
            spot.yaw = (float)to_double(d, key);
        else if (has_intensity && is(key, "intensity"))
            spot.intensity = (float)to_double(d, key);
        else
            unknown(section, key);
    }
    return spot;
}

/* The map's own spots come from the level's first script only. */
static bool first_script(void)
{
    return !LEVEL(int, LEVEL_WAVES_LOADED);
}

static void add_spawn_spot(const toml_datum_t *t, const char *key)
{
    spot_t spot = parse_spot(t, key, true, true, false);
    int *count = &LEVEL(int, LEVEL_NUM_SPAWN_SPOTS);
    if (!first_script() || *count >= MAX_SPAWN_SPOTS)
        return;
    game_spot_t *s = &((game_spot_t *)GAME_ADDRESS(GAME_LEVEL + LEVEL_SPAWN_SPOTS))[(*count)++];
    memcpy(s->origin, spot.origin, sizeof(s->origin));
    s->angles[1] = spot.yaw;
}

/* Radio and rocket spots exist in the expansion only. */
static void add_expansion_spot(const toml_datum_t *t, const char *key, uint32_t count_at,
                               uint32_t spots_at, int max)
{
    spot_t spot = parse_spot(t, key, true, true, false);
    int *count = &LEVEL(int, count_at);
    if (!is_expansion() || !first_script() || *count >= max)
        return;
    game_spot_t *s = &((game_spot_t *)GAME_ADDRESS(GAME_LEVEL + spots_at))[(*count)++];
    memcpy(s->origin, spot.origin, sizeof(s->origin));
    s->angles[1] = spot.yaw;
}

static void add_radio_spot(const toml_datum_t *t, const char *key)
{
    add_expansion_spot(t, key, LEVEL_NUM_RADIO_SPOTS, LEVEL_RADIO_SPOTS, MAX_RADIO_SPOTS);
}

static void add_rocket_spot(const toml_datum_t *t, const char *key)
{
    add_expansion_spot(t, key, LEVEL_NUM_ROCKET_SPOTS, LEVEL_ROCKET_SPOTS, MAX_ROCKET_SPOTS);
}

static void add_deathhand_spot(const toml_datum_t *t, const char *key)
{
    spot_t spot = parse_spot(t, key, true, true, false);
    int *count = &LEVEL(int, LEVEL_NUM_DEATHHAND_SPOTS);
    if (!is_expansion() || !first_script() || *count >= MAX_DEATHHAND_SPOTS)
        return;
    game_deathhand_spot_t *s =
        &((game_deathhand_spot_t *)GAME_ADDRESS(GAME_LEVEL + LEVEL_DEATHHAND_SPOTS))[(*count)++];
    memcpy(s->origin, spot.origin, sizeof(s->origin));
    s->yaw = spot.yaw;
}

/* ARTILLERY is read from every script, unlike the map's spots. */
static void add_artillery_spot(const toml_datum_t *t, const char *key)
{
    spot_t spot = parse_spot(t, key, false, false, true);
    int *count = &LEVEL(int, LEVEL_NUM_ARTILLERY_SPOTS);
    if (*count >= MAX_ARTILLERY_SPOTS)
        fail(t, "ARTILLERY has more than %d spots", MAX_ARTILLERY_SPOTS);
    float (*spots)[3] = GAME_ADDRESS(GAME_LEVEL + LEVEL_ARTILLERY_SPOTS);
    memcpy(spots[*count], spot.origin, sizeof(spot.origin));
    ((float *)GAME_ADDRESS(GAME_LEVEL + LEVEL_ARTILLERY_INTENSITY))[*count] = spot.intensity;
    ++*count;
}

static void add_superweapon_spot(const toml_datum_t *t, const char *key)
{
    spot_t spot = parse_spot(t, key, false, false, false);
    int *count = &LEVEL(int, LEVEL_NUM_SUPERWEAPON_SPOTS);
    if (!first_script() || *count >= MAX_SUPERWEAPON_SPOTS)
        return;
    float (*spots)[3] = GAME_ADDRESS(GAME_LEVEL + LEVEL_SUPERWEAPON_SPOTS);
    memcpy(spots[(*count)++], spot.origin, sizeof(spot.origin));
}

/* [ARTILLERY] and [SUPERWEAPON] hold their spots in spot = [...] or in
 * [[ARTILLERY.spot]] tables, which TOML reads the same.
 */
static void parse_spot_list(const toml_datum_t *t, const char *section,
                            void (*add)(const toml_datum_t *, const char *))
{
    for (int i = 0; i < t->u.tab.size; ++i) {
        if (is(t->u.tab.key[i], "spot"))
            for_tables(&t->u.tab.value[i], section, add);
        else
            unknown(section, t->u.tab.key[i]);
    }
}

/* The XML's class names, in igwWave_t.counts order. */
static const char *const class_names[NUM_NPC_CLASSES] = {
    "CONSCRIPT", "GRENADIER", "HLEGIONNAIRE", "OFFICER", "SCARAB", "ONTOS", "WALKER",
    "BOSS_WALKER", "SPY", "ENGINEER", "BOMBER", "CIV", "HERRAD",
};

static void add_wave(const toml_datum_t *t, const char *section)
{
    if (num_waves >= MAX_WAVES)
        fail(t, "more than %d waves", MAX_WAVES);
    game_wave_t *wave = &waves[num_waves];
    memset(wave, 0, sizeof(*wave));
    sizes[num_waves] = 0;
    bool has_startup = false;
    for (int i = 0; i < t->u.tab.size; ++i) {
        const char *key = t->u.tab.key[i];
        const toml_datum_t *d = &t->u.tab.value[i];
        int c = 0;
        for (; c < NUM_NPC_CLASSES; ++c)
            if (is(key, class_names[c]))
                break;
        if (c < NUM_NPC_CLASSES) {
            /* Repeated XML elements add up; so do the values of an array. */
            if (d->type == TOML_ARRAY)
                for (int e = 0; e < d->u.arr.size; ++e)
                    wave->counts[c] += to_int(&d->u.arr.elem[e], key);
            else
                wave->counts[c] += to_int(d, key);
        } else if (is(key, "routes")) {
            current = d;
            for_routes(d, key, add_wave_route, wave);
        } else if (is(key, "TEMPLAR_RATIO")) {
            wave->templarRatio = (float)to_double(d, key);
        } else if (is(key, "startup")) {
            startup[num_waves] = to_int(d, key);
            if (startup[num_waves] < 0)
                fail(d, "startup must not be negative");
            has_startup = true;
        } else if (is(key, "size")) {
            /* Unlike the XML's size, it does not affect the scheduling. */
            int size = to_int(d, key);
            if (size < 0 || size > 2)
                fail(d, "size must be 0, 1 or 2");
            sizes[num_waves] = (unsigned char)size;
        } else {
            unknown(section, key);
        }
    }
    if (!has_startup)
        fail(t, "WAVE %d has no startup", num_waves + 1);
    ++num_waves;
}

static void parse_document(const toml_datum_t *top)
{
    for (int i = 0; i < top->u.tab.size; ++i) {
        const char *key = top->u.tab.key[i];
        const toml_datum_t *d = &top->u.tab.value[i];
        if (is(key, "WAVE")) {
            for_tables(d, key, add_wave);
        } else if (is(key, "MAXCOUNTS")) {
            parse_max_counts(table(d, key));
        } else if (is(key, "WAVESETTINGS")) {
            parse_wave_settings(table(d, key));
        } else if (is(key, "SKILLSETTINGS")) {
            parse_skill_settings(table(d, key));
        } else if (is(key, "AI_SETTINGS")) {
            parse_ai_settings(table(d, key));
        } else if (is(key, "ARTILLERY")) {
            LEVEL(int, LEVEL_NUM_ARTILLERY_SPOTS) = 0;
            parse_spot_list(table(d, key), key, add_artillery_spot);
        } else if (is(key, "SUPERWEAPON")) {
            /* The XML loader clears them even when it keeps the first
             * script's spots and reads none.
             */
            LEVEL(int, LEVEL_NUM_SUPERWEAPON_SPOTS) = 0;
            parse_spot_list(table(d, key), key, add_superweapon_spot);
        } else if (is(key, "SPAWNPOINT")) {
            for_tables(d, key, add_spawn_spot);
        } else if (is(key, "deathhand")) {
            for_tables(d, key, add_deathhand_spot);
        } else if (is(key, "radiospot")) {
            for_tables(d, key, add_radio_spot);
        } else if (is(key, "rocketspot")) {
            for_tables(d, key, add_rocket_spot);
        } else {
            unknown(NULL, key);
        }
    }
}

/* The XML loader's defaults, set before each script. */
static void set_defaults(void)
{
    GAME(int, GAME_MAX_CONSCRIPTS) = 200;
    GAME(int, GAME_MAX_VEHICLES) = 4;
    GAME(int, GAME_MAP_MAX_BOTS) = 8;
    GAME(int, GAME_MAX_OFFICERS) = 1;
    GAME(int, GAME_MAX_COMMANDOS) = 100;
    GAME(int, GAME_MIN_WAVE_DIST_SMALL) = 40000;
    GAME(int, GAME_MIN_WAVE_DIST_MEDIUM) = 24000;
    GAME(int, GAME_MIN_WAVE_DIST_LARGE) = 12000;
    GAME(int, GAME_ROAMER_INTERVAL) = 20000;
    GAME(int, GAME_MEDIUM_WAVE_TIME) = 1200000;
    GAME(int, GAME_SMALL_WAVE_TIME) = 600000;
    LEVEL(int, LEVEL_NUM_ARTILLERY_SPOTS) = 0;
    LEVEL(int, LEVEL_NUM_SPAWN_SPOTS) = 0;
    num_waves = 0;
}

/* G_LoadWaveScript(xml) for the TOML next to it. Returns -1 without one, so
 * that the payload continues into the XML loader, and 1 once loaded.
 */
BLOB_EXPORT int toml_waves_load(const char *xml)
{
    active = false;

    /* Static, as loading keeps pointing at it for the run's messages. */
    static char name[128];
    size_t length = strlen(xml);
    if (length < 4 || length + 2 > sizeof(name) || strcmp(xml + length - 4, ".xml"))
        return -1;
    memcpy(name, xml, length - 4);
    memcpy(name + length - 4, ".toml", 6);

    int file = 0;
    int size = game_syscall(G_FS_FOPEN_FILE, name, &file, 0);
    if (!file)
        return -1;
    loading = name;
    if (size < 0 || size > MAX_FILE_SIZE) {
        game_syscall(G_FS_FCLOSE_FILE, file);
        fail(NULL, "file is too big");
    }
    source = malloc((size_t)size + 1);
    if (!source) {
        game_syscall(G_FS_FCLOSE_FILE, file);
        fail(NULL, "out of memory");
    }
    game_syscall(G_FS_READ, source, size, file);
    game_syscall(G_FS_FCLOSE_FILE, file);
    source[size] = 0;

    parsed = toml_parse(source, size);
    parsed_valid = true;
    if (!parsed.ok) {
        char reason[sizeof(parsed.errmsg)];
        memcpy(reason, parsed.errmsg, sizeof(reason));
        reason[sizeof(reason) - 1] = 0;
        fail(NULL, "%s", reason);
    }

    /* Restart the sequence for a new level, but keep the round's place
     * when a difficulty change reloads during it.
     */
    if (first_script())
        restart = true;
    set_defaults();
    parse_document(&parsed.toptab);
    release();

    /* The retail wave list gets the first waves too, all of them small and
     * unsorted, for anything else that reads it.
     */
    int count = num_waves < MAX_GAME_WAVES ? num_waves : MAX_GAME_WAVES;
    memcpy(GAME_ADDRESS(GAME_WAVES), waves, (size_t)count * sizeof(game_wave_t));
    GAME(int, GAME_NUM_WAVES) = count;
    GAME(int, GAME_FIRST_MEDIUM_WAVE) = -1;
    GAME(int, GAME_FIRST_LARGE_WAVE) = -1;
    LEVEL(int, LEVEL_WAVES_LOADED) = 1;
    print("Loaded wave script %s: %d waves\n", name, num_waves);
    active = true;
    return 1;
}

/* G_SendScriptWave(wave, 0), whose wave is in esi; nonzero once sent. */
static int send_script_wave(game_wave_t *wave)
{
    int result;
    void *function = GAME_ADDRESS(GAME_SEND_SCRIPT_WAVE);
    __asm__ volatile("pushl $0\n\t"
                     "call *%[function]\n\t"
                     "addl $4, %%esp"
                     : "=a"(result), "+S"(wave)
                     : [function] "r"(function)
                     : "ecx", "edx", "memory", "cc");
    return result;
}

/* The route of a wave's grenadiers, legionnaires and herrads: one of its
 * routes, or any without; -1 if the map has none. G_PickWaveRoute prefers
 * the forced route like G_PickRoute does.
 */
static int pick_route(const game_wave_t *wave)
{
    if (GAME(int, GAME_NUM_SPLINES) <= 0)
        return -1;
    if (!wave->routes)
        return ((int (*)(void))GAME_ADDRESS(GAME_PICK_ROUTE))();
    int route = 0;
    ((int (*)(int, int, int *))GAME_ADDRESS(GAME_PICK_WAVE_ROUTE))(wave->routes, 0, &route);
    return route;
}

/* G_SendScriptWave sends only the conscripts and vehicles. The wave's
 * grenadiers, legionnaires and herrads start together at one route's
 * start and roam from there, as G_SpawnRoamers' do. Like G_AddNPCWave,
 * the first that AddNPC refuses ends them.
 */
static void spawn_infantry(const game_wave_t *wave)
{
    static const int classes[] = { NPC_GRENADIER, NPC_HLEGIONNAIRE, NPC_HERRAD };
    void *(*add_npc)(int, int, int, float) = GAME_ADDRESS(GAME_ADD_NPC);
    int route = -2;
    for (size_t c = 0; c < sizeof(classes) / sizeof(classes[0]); ++c) {
        for (int i = 0; i < wave->counts[classes[c]]; ++i) {
            if (route == -2)
                route = pick_route(wave);
            /* AddNPC does not check the route itself. */
            if (route < 0 || route >= GAME(int, GAME_NUM_SPLINES)) {
                print("^3WARNING: %s: route %d does not exist on this map\n", loading,
                      route + 1);
                return;
            }
            /* No wave, and for a grenadier a shield by the structures. */
            if (!add_npc(route, -1, classes[c] + 1, 0.0f))
                return;
        }
    }
}

/* G_RunWaves after G_SpawnRoamers, for TOML scripts. Returns 0 for XML
 * scripts, which continue in G_RunWaves, and 1 when it has handled the
 * frame instead.
 */
BLOB_EXPORT int toml_waves_run(void)
{
    if (!active)
        return 0;
    int time = LEVEL(int, LEVEL_TIME);
    int start = LEVEL(int, LEVEL_ROUND_START_TIME);
    if (restart || start != round_start) {
        restart = false;
        round_start = start;
        sent = 0;
        retry_time = 0;
    }
    if (!num_waves)
        return 1;
    int index = sent < num_waves ? sent : num_waves - 1;
    int due = (sent ? last_sent : round_start) + startup[index];
    if (time < due || time < retry_time)
        return 1;

    /* Like the XML scheduler, send only with room for more NPCs. A wave
     * without room, or whose conscripts and vehicles find no room or
     * route, waits; it is not skipped.
     */
    int npcs = GAME(int, GAME_NUM_CONSCRIPTS) + GAME(int, GAME_NUM_VEHICLES) * 4;
    if (npcs >= GAME(int, GAME_NPC_THRESHOLD)) {
        retry_time = time + RETRY_TIME;
        return 1;
    }
    game_wave_t *wave = &waves[index];
    const int *n = wave->counts;
    /* G_SendWave reports whether any conscripts, scarabs, ontos or boss
     * walkers went out, never the walkers, so a wave of walkers alone
     * counts as sent either way.
     */
    bool reported = n[NPC_CONSCRIPT] > 0 || n[NPC_SCARAB] > 0 || n[NPC_ONTOS] > 0 ||
                    n[NPC_BOSS_WALKER] > 0;
    if ((reported || n[NPC_WALKER] > 0) && !send_script_wave(wave) && reported) {
        retry_time = time + RETRY_TIME;
        return 1;
    }
    spawn_infantry(wave);
    ((void (*)(int))GAME_ADDRESS(GAME_WAVE_SOUND))(sizes[index]);
    LEVEL(int, LEVEL_LAST_WAVE_TIME) = time;
    last_sent = time;
    if (sent < num_waves)
        ++sent;
    return 1;
}
