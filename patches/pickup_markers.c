#include "patches.h"

#include <float.h>
#include <stdio.h>
#include <string.h>

/* Edit these settings and rebuild. Like retail's FinishSpawningItem, each
 * marker drops onto the first surface a point trace hits straight below its
 * spawn point. The trace starts TRACE_LIFT map units above the spawn point,
 * so spots resting on or slightly inside the floor still find it. Offsets
 * use world X/Y/Z map units and fine-tune the dropped position. Size is a
 * uniform multiplier around the model origin: 1 is native size.
 */
#define PICKUP_MARKER_DYNAMITE_MODEL "models/mapobjects/item_highlight/item_highlight_dynamite.md3"
#define PICKUP_MARKER_ROCKET_MODEL   "models/mapobjects/item_highlight/item_highlight_rocket.md3"
#define PICKUP_MARKER_RADIO_MODEL    "models/mapobjects/item_highlight/item_highlight_radio.md3"
#define PICKUP_MARKER_OFFSET_X  0.0f
#define PICKUP_MARKER_OFFSET_Y  0.0f
#define PICKUP_MARKER_OFFSET_Z  1.5f
#define PICKUP_MARKER_SIZE      1.0f
#define PICKUP_MARKER_TRACE_LIFT 15.0f

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24,
               "Pickup marker settings require IEEE binary32 floats");
_Static_assert(sizeof(PICKUP_MARKER_DYNAMITE_MODEL) > 1 &&
               sizeof(PICKUP_MARKER_ROCKET_MODEL) > 1 &&
               sizeof(PICKUP_MARKER_RADIO_MODEL) > 1,
               "Pickup marker model paths must not be empty");

/* Persistent client-side models at every spot where retail places the
 * dynamite, rocket launcher and radio pickups:
 *
 * - Dynamite spots are the BSP's weapon_dynamite entities, which retail
 *   pools like jewels and moves between at random.
 * - Rocket launcher and radio spots exist only in the wave script
 *   scripts/waves/<map>-<e|m|h|b|g>.xml, as <rocketspot> and <radiospot>
 *   elements with integer <x>, <y>, <z> and <yaw> children. G_InitGame
 *   loads the medium script in survival (gametype 4), the easy one in
 *   teamkill (5), else the sv_difficulty one (easy when out of range).
 *   Only that first script's spots are kept, at most 10 of each kind, z
 *   raised by 2, a missing value left 0. Retail reads them only with the
 *   Scorched Earth expansion (sv_expansion) and spawns nothing on them in
 *   free for all (gametype 0). g_gametype, sv_difficulty, sv_expansion and
 *   mapname are all serverinfo keys, so remote clients see them too.
 *
 * Like stronghold_markers.c, emit position-independent i386 code with EBX
 * pointing at the payload and EBP at the syscall dispatcher's frame.
 * Invalidate on world load, then read the BSP and wave script on the first
 * main-world draw. Features chain in registration name order and the
 * valuable markers consume world loads without passing them on, so this
 * feature's name must sort before "valuable_markers". Consuming the BSP
 * tokens through EOF rewinds the shared cursor for other BSP readers.
 */
enum {
    MAX_SPOTS = 256, MAX_WAVE_SPOTS = 10, SPOT_SIZE = 52,
    REF_ENTITY_SIZE = 0xa68, XML_MAX = 0x8000, MAPNAME_MAX = 64,
    FS_FOPEN = 10, FS_READ = 11, FS_CLOSE = 13, BOX_TRACE = 25,
    LOAD_WORLD = 36, REGISTER_MODEL = 37, ADD_ENTITY = 41, RENDER_SCENE = 44,
    GET_TOKEN = 86, MASK_PLAYERSOLID = 0x2010801, FLOAT_ONE = 0x3f800000,
    RETAIL_CONFIG_STRING = 0x27a90, RETAIL_INFO_VALUE = 0x3d9b0,
    RETAIL_ATOI = 0x82e32, RETAIL_ATOF = 0x84dc9,
    RETAIL_ANGLES_TO_AXIS = 0x3c0d0, MAIN_REFDEF = 0xd9e7a0,
};

enum {
    RESET, DRAW, NEXT_ENTITY, NEXT_KEY, CLASSNAME, ORIGIN, ANGLES, ANGLE,
    SAVE, WAVES, MEDIUM, EASY, LETTER, COPY, LOWERED, COPIED, READ, CLOSE,
    SCAN, SCAN_COMMENT, SCAN_NEXT, RADIO, ROCKET, SPOT, SPOT_LOOP,
    SPOT_COMMENT, SPOT_NEXT, FIELD_X, FIELD_Y, FIELD_Z, FIELD_YAW, FIELD,
    SPOT_SAVE, READY, DRAW_LOOP, PASS, TOKEN, EQUALS, EQUALS_LOOP, UNEQUAL,
    VECTOR, VECTOR_NEXT, VECTOR_NUMBER, VECTOR_SKIP, VECTOR_DELIMITER,
    VECTOR_OK, VECTOR_BAD, ADD_SPOT, ADD_PLACE, ADD_DONE, PREFIX,
    PREFIX_LOOP, PREFIX_MATCH, PREFIX_MISS, SKIP_COMMENT, SKIP_LOOP,
    SKIP_DONE, LABEL_COUNT
};

typedef struct {
    payload_t *payload;
    bool ok;
    uint32_t labels[LABEL_COUNT];
    struct { uint32_t at; unsigned label; } fixups[256];
    size_t fixup_count;
} emitter_t;

static uint32_t emit(emitter_t *e, const void *bytes, size_t size)
{
    uint32_t at = (uint32_t)e->payload->size;
    if (!e->ok || size > PAYLOAD_MAX - e->payload->size) {
        e->ok = false;
        return 0;
    }
    if (bytes)
        memcpy(e->payload->data + at, bytes, size);
    else
        memset(e->payload->data + at, 0, size);
    e->payload->size += size;
    return at;
}

#define BYTES(e, ...) emit(e, (const unsigned char[]){ __VA_ARGS__ }, \
                          sizeof((const unsigned char[]){ __VA_ARGS__ }))

static uint32_t word(emitter_t *e, uint32_t value)
{
    unsigned char bytes[4];
    put32(bytes, value);
    return emit(e, bytes, sizeof(bytes));
}

/* Encode native floats explicitly as little-endian payload words. */
static uint32_t real(emitter_t *e, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return word(e, bits);
}

static void label(emitter_t *e, unsigned name)
{
    e->labels[name] = (uint32_t)e->payload->size;
}

static void reference(emitter_t *e, unsigned name)
{
    uint32_t at = word(e, 0);
    if (e->fixup_count == ARRAY_SIZE(e->fixups)) {
        e->ok = false;
        return;
    }
    e->fixups[e->fixup_count].at = at;
    e->fixups[e->fixup_count++].label = name;
}

static void jump(emitter_t *e, unsigned name)
{
    BYTES(e, 0xe9);
    reference(e, name);
}

/* The condition is the low byte of a near Jcc opcode (JE = 0x84). */
static void branch(emitter_t *e, unsigned condition, unsigned name)
{
    BYTES(e, 0x0f, condition);
    reference(e, name);
}

static void call(emitter_t *e, unsigned name)
{
    BYTES(e, 0xe8);
    reference(e, name);
}

static void retail_call(emitter_t *e, uint32_t rva, uint32_t target)
{
    BYTES(e, 0xe8);
    word(e, target - (rva + (uint32_t)e->payload->size + 4));
}

/* LEA reg,[EBX+offset]; register numbers use their i386 encoding. */
static void address(emitter_t *e, unsigned reg, uint32_t offset)
{
    BYTES(e, 0x8d, 0x83 | (reg << 3));
    word(e, offset);
}

static void store(emitter_t *e, uint32_t offset, uint32_t value)
{
    BYTES(e, 0xc7, 0x83, B4(offset), B4(value));
}

static void compare(emitter_t *e, uint32_t offset, uint32_t value)
{
    BYTES(e, 0x81, 0xbb, B4(offset), B4(value));
}

static void engine_call(emitter_t *e, unsigned number, unsigned argc)
{
    BYTES(e, 0x6a, number, 0xff, 0x53, 8, 0x83, 0xc4, (argc + 1) * 4);
}

static void token(emitter_t *e, uint32_t buffer, unsigned eof)
{
    address(e, 0, buffer);
    call(e, TOKEN);
    BYTES(e, 0x85, 0xc0);
    branch(e, 0x84, eof);
}

static void match(emitter_t *e, uint32_t buffer, uint32_t text, unsigned yes)
{
    address(e, 0, buffer);
    address(e, 2, text);
    call(e, EQUALS);
    BYTES(e, 0x85, 0xc0);
    branch(e, 0x85, yes);
}

/* Jump to yes and advance ESI past text when the XML at ESI starts with it. */
static void prefix(emitter_t *e, uint32_t text, unsigned yes)
{
    address(e, 2, text);
    call(e, PREFIX);
    BYTES(e, 0x85, 0xc0);
    branch(e, 0x85, yes);
}

/* EAX = the serverinfo value of key, through retail Info_ValueForKey,
 * which takes the info string in ECX and the key in EBX and keeps ESI and
 * EDI. Its two static result buffers alternate, so each result must be
 * used before the next call but one.
 */
static void server_value(emitter_t *e, uint32_t rva, uint32_t info,
                         uint32_t key)
{
    BYTES(e, 0x8b, 0x8b, B4(info), 0x53); /* ecx=info; save payload base */
    BYTES(e, 0x8d, 0x9b, B4(key)); /* lea ebx,[ebx+key] */
    retail_call(e, rva, RETAIL_INFO_VALUE);
    BYTES(e, 0x5b); /* restore payload base */
}

static bool build_markers(payload_t *payload, uint32_t rva,
                          uint32_t next, uint32_t *entry)
{
    const float offsets[] = {
        PICKUP_MARKER_OFFSET_X, PICKUP_MARKER_OFFSET_Y, PICKUP_MARKER_OFFSET_Z,
    };
    const float size = PICKUP_MARKER_SIZE;
    const float lift = PICKUP_MARKER_TRACE_LIFT;
    if (!(size > 0 && size <= FLT_MAX)) {
        fputs("Pickup marker size must be finite and positive\n", stderr);
        return false;
    }
    if (!(lift >= 0 && lift <= FLT_MAX)) {
        fputs("Pickup marker trace lift must be finite and non-negative\n", stderr);
        return false;
    }
    for (size_t i = 0; i < ARRAY_SIZE(offsets); ++i) {
        if (!(offsets[i] >= -FLT_MAX && offsets[i] <= FLT_MAX)) {
            fputs("Pickup marker offsets must be finite\n", stderr);
            return false;
        }
    }
    emitter_t e = { .payload = payload, .ok = true };
    for (size_t i = 0; i < LABEL_COUNT; ++i)
        e.labels[i] = UINT32_MAX;

    uint32_t loaded = word(&e, 0), count = word(&e, 0);
    uint32_t dynamite_model = word(&e, 0), rocket_model = word(&e, 0);
    uint32_t radio_model = word(&e, 0);
    uint32_t dynamite = word(&e, 0), valid_origin = word(&e, 0);
    /* Origin, axis and model handle are contiguous and laid out like a
     * cached spot, so a finished spot needs one copy.
     */
    uint32_t origin = emit(&e, NULL, 12), axis = emit(&e, NULL, 36);
    uint32_t spot_model = word(&e, 0);
    uint32_t angles = emit(&e, NULL, 12);
    uint32_t start = emit(&e, NULL, 12), end = emit(&e, NULL, 12);
    uint32_t trace = emit(&e, NULL, 56);
    uint32_t point = emit(&e, NULL, 12); /* zero mins and maxs */
    uint32_t raise = real(&e, lift), drop = real(&e, 4096.0f);
    uint32_t scale = real(&e, size), offset = (uint32_t)payload->size;
    for (size_t i = 0; i < ARRAY_SIZE(offsets); ++i)
        real(&e, offsets[i]);
    uint32_t info = word(&e, 0), gametype = word(&e, 0);
    uint32_t letter = word(&e, 0), handle = word(&e, 0);
    uint32_t length = word(&e, 0), read = word(&e, 0);
    uint32_t radio_count = word(&e, 0), rocket_count = word(&e, 0);
    uint32_t bias = word(&e, 0), integer = word(&e, 0);
    uint32_t key = emit(&e, NULL, 1024), value = emit(&e, NULL, 1024);
    uint32_t entity = emit(&e, NULL, REF_ENTITY_SIZE);
    uint32_t spots = emit(&e, NULL, MAX_SPOTS * SPOT_SIZE);
    /* "scripts/waves/", the map name, "-" and the difficulty letter, then
     * ".xml" and its terminator.
     */
    const char waves[] = "scripts/waves/";
    uint32_t path = emit(&e, waves, sizeof(waves) - 1);
    emit(&e, NULL, MAPNAME_MAX + 8);
    uint32_t xml = emit(&e, NULL, XML_MAX + 1);
    const char strings[] =
        "classname\0origin\0angles\0angle\0weapon_dynamite\0"
        "g_gametype\0sv_expansion\0sv_difficulty\0mapname\0emhbg\0"
        "<!--\0-->\0<radiospot>\0<rocketspot>\0</radiospot>\0</rocketspot>\0"
        "<x>\0<y>\0<z>\0<yaw>";
    uint32_t names = emit(&e, strings, sizeof(strings));
    enum {
        NAME_CLASS = 0, NAME_ORIGIN = 10, NAME_ANGLES = 17, NAME_ANGLE = 24,
        NAME_DYNAMITE = 30, NAME_GAMETYPE = 46, NAME_EXPANSION = 57,
        NAME_DIFFICULTY = 70, NAME_MAPNAME = 84, NAME_LETTERS = 92,
        TAG_COMMENT = 98, TAG_COMMENT_END = 103, TAG_RADIO = 107,
        TAG_ROCKET = 119, TAG_RADIO_END = 132, TAG_ROCKET_END = 145,
        TAG_X = 159, TAG_Y = 163, TAG_Z = 167, TAG_YAW = 171,
    };
    _Static_assert(sizeof(strings) == 177, "Pickup marker string offsets");
    uint32_t dynamite_path = emit(&e, PICKUP_MARKER_DYNAMITE_MODEL,
                                  sizeof(PICKUP_MARKER_DYNAMITE_MODEL));
    uint32_t rocket_path = emit(&e, PICKUP_MARKER_ROCKET_MODEL,
                                sizeof(PICKUP_MARKER_ROCKET_MODEL));
    uint32_t radio_path = emit(&e, PICKUP_MARKER_RADIO_MODEL,
                               sizeof(PICKUP_MARKER_RADIO_MODEL));
    if (!e.ok)
        goto failed;
    put32(payload->data + entity + 4, 64); /* RF_NOSHADOW */
    put32(payload->data + entity + 0x40, 1); /* nonNormalizedAxes */
    put32(payload->data + entity + 0x74, UINT32_MAX); /* shaderRGBA */

    *entry = (uint32_t)payload->size;
    BYTES(&e, 0x83, 0x7d, 8, LOAD_WORLD);
    branch(&e, 0x84, RESET);
    BYTES(&e, 0x83, 0x7d, 8, RENDER_SCENE);
    branch(&e, 0x84, DRAW);
    jump(&e, PASS);

    label(&e, RESET);
    store(&e, loaded, 0);
    jump(&e, PASS);

    label(&e, DRAW);
    address(&e, 0, MAIN_REFDEF - rva);
    BYTES(&e, 0x39, 0x45, 12); /* only the main world's refdef */
    branch(&e, 0x85, PASS);
    BYTES(&e, 0xf6, 0x40, 0x4c, 5); /* no world / hyperspace */
    branch(&e, 0x85, PASS);
    compare(&e, loaded, 0);
    branch(&e, 0x85, READY);
    store(&e, loaded, 1);
    store(&e, count, 0);
    const uint32_t models[][2] = {
        { dynamite_path, dynamite_model }, { rocket_path, rocket_model },
        { radio_path, radio_model },
    };
    for (size_t i = 0; i < ARRAY_SIZE(models); ++i) {
        address(&e, 0, models[i][0]);
        BYTES(&e, 0x50);
        engine_call(&e, REGISTER_MODEL, 1);
        BYTES(&e, 0x89, 0x83, B4(models[i][1]));
    }

    label(&e, NEXT_ENTITY);
    token(&e, key, WAVES);
    BYTES(&e, 0x80, 0xbb, B4(key), '{');
    branch(&e, 0x85, NEXT_ENTITY);
    store(&e, dynamite, 0);
    store(&e, valid_origin, 0);
    for (unsigned i = 0; i < 3; ++i)
        store(&e, angles + i * 4, 0);

    label(&e, NEXT_KEY);
    token(&e, key, WAVES);
    BYTES(&e, 0x80, 0xbb, B4(key), '}');
    branch(&e, 0x84, SAVE);
    token(&e, value, WAVES);
    match(&e, key, names + NAME_CLASS, CLASSNAME);
    match(&e, key, names + NAME_ORIGIN, ORIGIN);
    match(&e, key, names + NAME_ANGLES, ANGLES);
    match(&e, key, names + NAME_ANGLE, ANGLE);
    jump(&e, NEXT_KEY);

    label(&e, CLASSNAME);
    address(&e, 0, value);
    address(&e, 2, names + NAME_DYNAMITE);
    call(&e, EQUALS);
    BYTES(&e, 0x89, 0x83, B4(dynamite));
    jump(&e, NEXT_KEY);

    label(&e, ORIGIN);
    address(&e, 6, value);
    address(&e, 7, origin);
    call(&e, VECTOR);
    BYTES(&e, 0x89, 0x83, B4(valid_origin));
    jump(&e, NEXT_KEY);

    label(&e, ANGLES);
    address(&e, 6, value);
    address(&e, 7, angles);
    call(&e, VECTOR);
    jump(&e, NEXT_KEY);

    label(&e, ANGLE);
    address(&e, 0, value);
    BYTES(&e, 0x50);
    retail_call(&e, rva, RETAIL_ATOF);
    BYTES(&e, 0x83, 0xc4, 4, 0xd9, 0x9b, B4(angles + 4)); /* fstp yaw */
    jump(&e, NEXT_KEY);

    label(&e, SAVE);
    compare(&e, dynamite, 0); branch(&e, 0x84, NEXT_ENTITY);
    compare(&e, valid_origin, 0); branch(&e, 0x84, NEXT_ENTITY);
    BYTES(&e, 0x8b, 0x83, B4(dynamite_model), 0x89, 0x83, B4(spot_model));
    call(&e, ADD_SPOT);
    jump(&e, NEXT_ENTITY);

    /* Choose the wave script exactly as retail G_InitGame does. */
    label(&e, WAVES);
    BYTES(&e, 0x31, 0xf6); /* CG_ConfigString takes its index in ESI */
    retail_call(&e, rva, RETAIL_CONFIG_STRING); /* CS_SERVERINFO */
    BYTES(&e, 0x89, 0x83, B4(info));
    const uint32_t settings[][2] = {
        { names + NAME_GAMETYPE, gametype }, { names + NAME_EXPANSION, 0 },
    };
    for (size_t i = 0; i < ARRAY_SIZE(settings); ++i) {
        server_value(&e, rva, info, settings[i][0]);
        BYTES(&e, 0x50);
        retail_call(&e, rva, RETAIL_ATOI);
        BYTES(&e, 0x83, 0xc4, 4);
        if (i == 0)
            BYTES(&e, 0x89, 0x83, B4(settings[i][1]));
        BYTES(&e, 0x85, 0xc0); /* free for all; no expansion */
        branch(&e, 0x84, READY);
    }
    BYTES(&e, 0x8b, 0x83, B4(gametype));
    BYTES(&e, 0x83, 0xf8, 4); /* survival */
    branch(&e, 0x84, MEDIUM);
    BYTES(&e, 0x83, 0xf8, 5); /* teamkill */
    branch(&e, 0x84, EASY);
    server_value(&e, rva, info, names + NAME_DIFFICULTY);
    BYTES(&e, 0x50);
    retail_call(&e, rva, RETAIL_ATOI);
    BYTES(&e, 0x83, 0xc4, 4);
    BYTES(&e, 0x83, 0xf8, 4); /* negative values compare above 4, too */
    branch(&e, 0x86, LETTER); /* jbe */
    label(&e, EASY);
    BYTES(&e, 0x31, 0xc0);
    jump(&e, LETTER);
    label(&e, MEDIUM);
    BYTES(&e, 0xb8, B4(1u));
    label(&e, LETTER);
    BYTES(&e, 0x8d, 0x93, B4(names + NAME_LETTERS)); /* lea edx,letters */
    BYTES(&e, 0x8a, 0x04, 0x02, 0x88, 0x83, B4(letter)); /* store letter */

    /* Append the lowercased map name, as G_InitGame lowercases it. */
    server_value(&e, rva, info, names + NAME_MAPNAME);
    BYTES(&e, 0x89, 0xc6); /* esi=map name */
    address(&e, 7, path + sizeof(waves) - 1);
    BYTES(&e, 0xb9, B4((uint32_t)MAPNAME_MAX));
    label(&e, COPY);
    BYTES(&e, 0x8a, 0x06, 0x84, 0xc0);
    branch(&e, 0x84, COPIED);
    BYTES(&e, 0x3c, 'A');
    branch(&e, 0x82, LOWERED); /* jb */
    BYTES(&e, 0x3c, 'Z');
    branch(&e, 0x87, LOWERED); /* ja */
    BYTES(&e, 0x04, 0x20);
    label(&e, LOWERED);
    BYTES(&e, 0x88, 0x07, 0x46, 0x47, 0x49); /* store; next; dec ecx */
    branch(&e, 0x85, COPY);
    jump(&e, READY); /* too long for any shipped wave script */
    label(&e, COPIED);
    BYTES(&e, 0xc6, 0x07, '-', 0x8a, 0x83, B4(letter), 0x88, 0x47, 1);
    BYTES(&e, 0xc7, 0x47, 2, '.', 'x', 'm', 'l', 0xc6, 0x47, 6, 0);

    /* Read at most XML_MAX bytes; the shipped scripts stay below 8 KiB. */
    store(&e, handle, 0);
    store(&e, read, 0);
    BYTES(&e, 0x6a, 0); /* FS_READ */
    address(&e, 0, handle); BYTES(&e, 0x50);
    address(&e, 0, path); BYTES(&e, 0x50);
    engine_call(&e, FS_FOPEN, 3);
    BYTES(&e, 0x89, 0x83, B4(length));
    compare(&e, handle, 0);
    branch(&e, 0x84, READY);
    BYTES(&e, 0x85, 0xc0);
    branch(&e, 0x8e, CLOSE); /* jle */
    BYTES(&e, 0x3d, B4((uint32_t)XML_MAX));
    branch(&e, 0x86, READ); /* jbe */
    BYTES(&e, 0xb8, B4((uint32_t)XML_MAX));
    label(&e, READ);
    BYTES(&e, 0x89, 0x83, B4(read));
    BYTES(&e, 0xff, 0xb3, B4(handle), 0x50); /* push handle, length */
    address(&e, 1, xml); BYTES(&e, 0x51);
    engine_call(&e, FS_READ, 3);
    label(&e, CLOSE);
    BYTES(&e, 0xff, 0xb3, B4(handle));
    engine_call(&e, FS_CLOSE, 1);
    BYTES(&e, 0x8b, 0x83, B4(read));
    address(&e, 2, xml);
    BYTES(&e, 0xc6, 0x04, 0x02, 0); /* terminate the text read */

    /* Scan for top-level spot elements, skipping XML comments: the easy
     * spiral script comments out a rocket spot this way.
     */
    store(&e, radio_count, 0);
    store(&e, rocket_count, 0);
    address(&e, 6, xml);
    label(&e, SCAN);
    BYTES(&e, 0x8a, 0x06, 0x84, 0xc0);
    branch(&e, 0x84, READY);
    BYTES(&e, 0x3c, '<');
    branch(&e, 0x85, SCAN_NEXT);
    prefix(&e, names + TAG_COMMENT, SCAN_COMMENT);
    prefix(&e, names + TAG_RADIO, RADIO);
    prefix(&e, names + TAG_ROCKET, ROCKET);
    label(&e, SCAN_NEXT);
    BYTES(&e, 0x46);
    jump(&e, SCAN);
    label(&e, SCAN_COMMENT);
    call(&e, SKIP_COMMENT);
    jump(&e, SCAN);

    /* Like retail, count a spot as soon as its element starts and ignore
     * the kind's elements past the tenth.
     */
    const struct { unsigned name; uint32_t counter, model; } kinds[] = {
        { RADIO, radio_count, radio_model },
        { ROCKET, rocket_count, rocket_model },
    };
    for (size_t i = 0; i < ARRAY_SIZE(kinds); ++i) {
        label(&e, kinds[i].name);
        compare(&e, kinds[i].counter, MAX_WAVE_SPOTS);
        branch(&e, 0x83, SCAN); /* jae */
        BYTES(&e, 0xff, 0x83, B4(kinds[i].counter));
        BYTES(&e, 0x8b, 0x83, B4(kinds[i].model), 0x89, 0x83, B4(spot_model));
        jump(&e, SPOT);
    }

    label(&e, SPOT);
    for (unsigned i = 0; i < 3; ++i) {
        store(&e, origin + i * 4, 0);
        store(&e, angles + i * 4, 0);
    }
    label(&e, SPOT_LOOP);
    BYTES(&e, 0x8a, 0x06, 0x84, 0xc0);
    branch(&e, 0x84, READY); /* unterminated element */
    BYTES(&e, 0x3c, '<');
    branch(&e, 0x85, SPOT_NEXT);
    prefix(&e, names + TAG_RADIO_END, SPOT_SAVE);
    prefix(&e, names + TAG_ROCKET_END, SPOT_SAVE);
    prefix(&e, names + TAG_COMMENT, SPOT_COMMENT);
    prefix(&e, names + TAG_X, FIELD_X);
    prefix(&e, names + TAG_Y, FIELD_Y);
    prefix(&e, names + TAG_Z, FIELD_Z);
    prefix(&e, names + TAG_YAW, FIELD_YAW);
    label(&e, SPOT_NEXT);
    BYTES(&e, 0x46);
    jump(&e, SPOT_LOOP);
    label(&e, SPOT_COMMENT);
    call(&e, SKIP_COMMENT);
    jump(&e, SPOT_LOOP);

    /* Retail reads every value with atoi and raises only z by 2. */
    const struct { unsigned name; uint32_t field, bias; } fields[] = {
        { FIELD_X, origin, 0 }, { FIELD_Y, origin + 4, 0 },
        { FIELD_Z, origin + 8, 2 }, { FIELD_YAW, angles + 4, 0 },
    };
    for (size_t i = 0; i < ARRAY_SIZE(fields); ++i) {
        label(&e, fields[i].name);
        address(&e, 7, fields[i].field);
        store(&e, bias, fields[i].bias);
        jump(&e, FIELD);
    }
    label(&e, FIELD);
    BYTES(&e, 0x80, 0x3e, '<'); /* an empty element keeps its 0 */
    branch(&e, 0x84, SPOT_LOOP);
    BYTES(&e, 0x56);
    retail_call(&e, rva, RETAIL_ATOI);
    BYTES(&e, 0x83, 0xc4, 4, 0x03, 0x83, B4(bias)); /* add bias */
    BYTES(&e, 0x89, 0x83, B4(integer), 0xdb, 0x83, B4(integer)); /* fild */
    BYTES(&e, 0xd9, 0x1f); /* fstp [edi] */
    jump(&e, SPOT_LOOP);

    label(&e, SPOT_SAVE);
    call(&e, ADD_SPOT);
    jump(&e, SCAN);

    label(&e, READY);
    compare(&e, count, 0); branch(&e, 0x84, PASS);
    BYTES(&e, 0x8b, 0xbb, B4(count)); /* edi=remaining spots */
    address(&e, 6, spots);

    label(&e, DRAW_LOOP);
    /* The renderer copies refEntity_t. Preserve the spot and loop count
     * while copying its origins, scaled axes and model into the reusable
     * entity.
     */
    const unsigned destinations[] = { 0x0c, 0x44, 0x54, 0x1c };
    for (size_t i = 0; i < ARRAY_SIZE(destinations); ++i) {
        BYTES(&e, 0x56, 0x57);
        if (i == 3)
            BYTES(&e, 0x83, 0xc6, 12);
        address(&e, 7, entity + destinations[i]);
        BYTES(&e, 0xb9, B4(i == 3 ? 9u : 3u), 0xf3, 0xa5, 0x5f, 0x5e);
    }
    BYTES(&e, 0x8b, 0x46, 48, 0x89, 0x83, B4(entity + 8)); /* hModel */
    address(&e, 0, entity);
    BYTES(&e, 0x50);
    engine_call(&e, ADD_ENTITY, 1);
    BYTES(&e, 0x83, 0xc6, SPOT_SIZE, 0x4f);
    branch(&e, 0x85, DRAW_LOOP);

    label(&e, PASS);
    BYTES(&e, 0xe9, B4(next - ((uint32_t)payload->size + 5)));

    /* Cache the spot at origin and angles with spot_model, keeping ESI and
     * EDI for the callers' scans. Spots past the cache or without a
     * registered model are dropped.
     */
    label(&e, ADD_SPOT);
    BYTES(&e, 0x56, 0x57);
    compare(&e, count, MAX_SPOTS); branch(&e, 0x83, ADD_DONE); /* jae */
    compare(&e, spot_model, 0); branch(&e, 0x84, ADD_DONE);
    /* Like FinishSpawningItem, point-trace the world from above the spot
     * to 4096 units below it. Keep the spot when the start is inside solid
     * geometry or nothing is hit.
     */
    for (unsigned i = 0; i < 3; ++i) {
        BYTES(&e, 0x8b, 0x83, B4(origin + i * 4)); /* eax=origin component */
        BYTES(&e, 0x89, 0x83, B4(start + i * 4), 0x89, 0x83, B4(end + i * 4));
    }
    BYTES(&e, 0xd9, 0x83, B4(origin + 8), 0xd8, 0x83, B4(raise)); /* z+lift */
    BYTES(&e, 0xd9, 0x9b, B4(start + 8));
    BYTES(&e, 0xd9, 0x83, B4(origin + 8), 0xd8, 0xa3, B4(drop)); /* z-4096 */
    BYTES(&e, 0xd9, 0x9b, B4(end + 8));
    BYTES(&e, 0x68, B4((uint32_t)MASK_PLAYERSOLID), 0x6a, 0); /* world model */
    const uint32_t trace_args[] = { point, point, end, start, trace };
    for (size_t i = 0; i < ARRAY_SIZE(trace_args); ++i) {
        address(&e, 0, trace_args[i]);
        BYTES(&e, 0x50);
    }
    engine_call(&e, BOX_TRACE, 7);
    compare(&e, trace + 4, 0); branch(&e, 0x85, ADD_PLACE); /* startsolid */
    /* The fraction is never negative, so its bits order like integers. */
    compare(&e, trace + 8, FLOAT_ONE); branch(&e, 0x83, ADD_PLACE); /* jae */
    for (unsigned i = 0; i < 3; ++i) {
        BYTES(&e, 0x8b, 0x83, B4(trace + 12 + i * 4)); /* eax=endpos */
        BYTES(&e, 0x89, 0x83, B4(origin + i * 4));
    }
    label(&e, ADD_PLACE);
    address(&e, 0, axis);
    address(&e, 6, angles); /* retail AnglesToAxis takes angles in ESI */
    retail_call(&e, rva, RETAIL_ANGLES_TO_AXIS);
    for (unsigned i = 0; i < 9; ++i) {
        BYTES(&e, 0xd9, 0x83, B4(axis + i * 4)); /* fld axis */
        BYTES(&e, 0xd8, 0x8b, B4(scale)); /* fmul size */
        BYTES(&e, 0xd9, 0x9b, B4(axis + i * 4));
    }
    for (unsigned i = 0; i < 3; ++i) {
        BYTES(&e, 0xd9, 0x83, B4(origin + i * 4)); /* fld coordinate */
        BYTES(&e, 0xd8, 0x83, B4(offset + i * 4)); /* fadd offset */
        BYTES(&e, 0xd9, 0x9b, B4(origin + i * 4));
    }
    BYTES(&e, 0x8b, 0xbb, B4(count), 0x6b, 0xff, SPOT_SIZE);
    BYTES(&e, 0x8d, 0xbc, 0x3b, B4(spots)); /* edi=spots+count*52 */
    address(&e, 6, origin);
    BYTES(&e, 0xb9, B4(SPOT_SIZE / 4u), 0xf3, 0xa5); /* origin, axis, model */
    BYTES(&e, 0xff, 0x83, B4(count));
    label(&e, ADD_DONE);
    BYTES(&e, 0x5f, 0x5e, 0xc3);

    /* EAX = 1 with ESI past the text at EDX when ESI starts with it, else
     * EAX = 0 with ESI unchanged. The terminator never matches, so the
     * comparison stops at the end of the XML.
     */
    label(&e, PREFIX);
    BYTES(&e, 0x56);
    label(&e, PREFIX_LOOP);
    BYTES(&e, 0x8a, 0x02, 0x84, 0xc0);
    branch(&e, 0x84, PREFIX_MATCH);
    BYTES(&e, 0x3a, 0x06);
    branch(&e, 0x85, PREFIX_MISS);
    BYTES(&e, 0x46, 0x42);
    jump(&e, PREFIX_LOOP);
    label(&e, PREFIX_MATCH);
    BYTES(&e, 0x83, 0xc4, 4, 0xb8, B4(1u), 0xc3);
    label(&e, PREFIX_MISS);
    BYTES(&e, 0x5e, 0x31, 0xc0, 0xc3);

    /* Advance ESI past the next "-->", or to the end of the XML. */
    label(&e, SKIP_COMMENT);
    label(&e, SKIP_LOOP);
    BYTES(&e, 0x80, 0x3e, 0);
    branch(&e, 0x84, SKIP_DONE);
    address(&e, 2, names + TAG_COMMENT_END);
    call(&e, PREFIX);
    BYTES(&e, 0x85, 0xc0);
    branch(&e, 0x85, SKIP_DONE);
    BYTES(&e, 0x46);
    jump(&e, SKIP_LOOP);
    label(&e, SKIP_DONE);
    BYTES(&e, 0xc3);

    label(&e, TOKEN);
    BYTES(&e, 0x68, B4(1024), 0x50);
    engine_call(&e, GET_TOKEN, 2);
    BYTES(&e, 0xc3);

    label(&e, EQUALS);
    BYTES(&e, 0x56, 0x57, 0x89, 0xc6, 0x89, 0xd7);
    label(&e, EQUALS_LOOP);
    BYTES(&e, 0x8a, 0x06, 0x3a, 0x07);
    branch(&e, 0x85, UNEQUAL);
    BYTES(&e, 0x46, 0x47, 0x84, 0xc0);
    branch(&e, 0x85, EQUALS_LOOP);
    BYTES(&e, 0xb8, 1, 0, 0, 0, 0x5f, 0x5e, 0xc3);
    label(&e, UNEQUAL);
    BYTES(&e, 0x31, 0xc0, 0x5f, 0x5e, 0xc3);

    /* Parse exactly three whitespace-separated coordinates, balancing the
     * x87 stack and rejecting incomplete origins instead of stale values.
     */
    label(&e, VECTOR);
    BYTES(&e, 0x55, 0xbd, 3, 0, 0, 0);
    label(&e, VECTOR_NEXT);
    BYTES(&e, 0x8a, 0x06, 0x84, 0xc0);
    branch(&e, 0x84, VECTOR_BAD);
    BYTES(&e, 0x3c, 32);
    branch(&e, 0x87, VECTOR_NUMBER);
    BYTES(&e, 0x46);
    jump(&e, VECTOR_NEXT);
    label(&e, VECTOR_NUMBER);
    BYTES(&e, 0x56);
    retail_call(&e, rva, RETAIL_ATOF);
    BYTES(&e, 0x83, 0xc4, 4, 0xd9, 0x1f, 0x83, 0xc7, 4);
    label(&e, VECTOR_SKIP);
    BYTES(&e, 0x8a, 0x06, 0x3c, 32);
    branch(&e, 0x86, VECTOR_DELIMITER);
    BYTES(&e, 0x46);
    jump(&e, VECTOR_SKIP);
    label(&e, VECTOR_DELIMITER);
    BYTES(&e, 0x4d);
    branch(&e, 0x84, VECTOR_OK);
    jump(&e, VECTOR_NEXT);
    label(&e, VECTOR_OK);
    BYTES(&e, 0xb8, 1, 0, 0, 0, 0x5d, 0xc3);
    label(&e, VECTOR_BAD);
    BYTES(&e, 0x31, 0xc0, 0x5d, 0xc3);

    for (size_t i = 0; e.ok && i < e.fixup_count; ++i) {
        uint32_t target = e.labels[e.fixups[i].label], at = e.fixups[i].at;
        if (target == UINT32_MAX || at + 4 > payload->size)
            e.ok = false;
        else
            put32(payload->data + at, target - (at + 4));
    }
    if (e.ok)
        return true;
failed:
    fputs("Pickup marker payload exceeds its size or fixup limit\n", stderr);
    return false;
}

/* The name must sort before valuable_markers; see the comment above. */
static const payload_feature_t pickup_markers = {
    CGAME, "persistent dynamite, rocket launcher and radio spawn models",
    build_markers,
};

REGISTER_PAYLOAD_FEATURE(pickup_markers)
