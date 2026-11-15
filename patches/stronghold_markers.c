#include "patches.h"

#include <float.h>
#include <stdio.h>
#include <string.h>

/* Edit these settings and rebuild. Each marker drops onto the first world
 * surface a point trace hits straight below its stronghold origin. The trace
 * starts TRACE_LIFT map units above that origin, so strongholds placed on or
 * sunk into the terrain still find its surface. Offsets are world X/Y/Z map
 * units added to the dropped position. Size is a uniform multiplier: 1 is
 * native size, 2 doubles it. At any size, the model's bounds stay centered
 * horizontally and their bottom rests on the dropped position plus offsets.
 */
#define STRONGHOLD_MARKER_MODEL     "models/mapobjects/item_highlight/item_highlight_stronghold.md3"
#define STRONGHOLD_MARKER_OFFSET_X  0.0f
#define STRONGHOLD_MARKER_OFFSET_Y  0.0f
#define STRONGHOLD_MARKER_OFFSET_Z  0.0f
#define STRONGHOLD_MARKER_SIZE      1.0f
#define STRONGHOLD_MARKER_TRACE_LIFT 64.0f

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24,
               "Stronghold marker settings require IEEE binary32 floats");
_Static_assert(sizeof(STRONGHOLD_MARKER_MODEL) > 1,
               "Stronghold marker model path must not be empty");

/* Like valuable_markers.c, emit position-independent i386 code with EBX
 * pointing at the payload and EBP at the syscall dispatcher's frame.
 * Invalidate on world load, then read the BSP on the first main-world draw.
 * This lets the valuable marker's load handler finish normally. Consuming
 * the tokens through EOF rewinds the shared cursor for other BSP readers.
 * Strongholds keep their map origins, including when ruined or unbuilt, so
 * each marker's floor position is traced once along with the BSP parse.
 */
enum {
    MAX_SPOTS = 3, SPOT_SIZE = 48, REF_ENTITY_SIZE = 0xa68,
    LOAD_WORLD = 36, REGISTER_MODEL = 37, ADD_ENTITY = 41,
    BOX_TRACE = 25, RENDER_SCENE = 44, MODEL_BOUNDS = 47, GET_TOKEN = 86,
    CONTENTS_SOLID = 1, FLOAT_ONE = 0x3f800000,
    RETAIL_ATOF = 0x84dc9, RETAIL_ANGLES_TO_AXIS = 0x3c0d0,
    MAIN_REFDEF = 0xd9e7a0,
};

enum {
    RESET, DRAW, NEXT_ENTITY, NEXT_KEY, CLASSNAME, ORIGIN, ANGLES, ANGLE,
    SAVE, PLACE, READY, DRAW_LOOP, PASS, TOKEN, EQUALS, EQUALS_LOOP, UNEQUAL,
    VECTOR, VECTOR_NEXT, VECTOR_NUMBER, VECTOR_SKIP, VECTOR_DELIMITER,
    VECTOR_OK, VECTOR_BAD, LABEL_COUNT
};

typedef struct {
    payload_t *payload;
    bool ok;
    uint32_t labels[LABEL_COUNT];
    struct { uint32_t at; unsigned label; } fixups[128];
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

/* Register numbers use their i386 encoding. */
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

static bool build_markers(payload_t *payload, uint32_t rva,
                          uint32_t next, uint32_t *entry)
{
    const float offsets[] = {
        STRONGHOLD_MARKER_OFFSET_X, STRONGHOLD_MARKER_OFFSET_Y,
        STRONGHOLD_MARKER_OFFSET_Z,
    };
    const float size = STRONGHOLD_MARKER_SIZE;
    const float lift = STRONGHOLD_MARKER_TRACE_LIFT;
    if (!(size > 0 && size <= FLT_MAX)) {
        fputs("Stronghold marker size must be finite and positive\n", stderr);
        return false;
    }
    if (!(lift >= 0 && lift <= FLT_MAX)) {
        fputs("Stronghold marker trace lift must be finite and non-negative\n", stderr);
        return false;
    }
    for (size_t i = 0; i < ARRAY_SIZE(offsets); ++i) {
        if (!(offsets[i] >= -FLT_MAX && offsets[i] <= FLT_MAX)) {
            fputs("Stronghold marker offsets must be finite\n", stderr);
            return false;
        }
    }
    emitter_t e = { .payload = payload, .ok = true };
    for (size_t i = 0; i < LABEL_COUNT; ++i)
        e.labels[i] = UINT32_MAX;

    uint32_t loaded = word(&e, 0), count = word(&e, 0), model = word(&e, 0);
    uint32_t stronghold = word(&e, 0), valid_origin = word(&e, 0);
    /* Origin and axis are contiguous so a whole spot needs one copy. */
    uint32_t origin = emit(&e, NULL, 12), axis = emit(&e, NULL, 36);
    uint32_t angles = emit(&e, NULL, 12);
    uint32_t mins = emit(&e, NULL, 12), maxs = emit(&e, NULL, 12);
    uint32_t anchor = emit(&e, NULL, 12), half = real(&e, 0.5f);
    uint32_t start = emit(&e, NULL, 12), end = emit(&e, NULL, 12);
    uint32_t trace = emit(&e, NULL, 56);
    uint32_t point = emit(&e, NULL, 12); /* zero mins and maxs */
    uint32_t raise = real(&e, lift), drop = real(&e, 4096.0f);
    uint32_t scale = real(&e, size), offset = (uint32_t)payload->size;
    for (size_t i = 0; i < ARRAY_SIZE(offsets); ++i)
        real(&e, offsets[i]);
    uint32_t key = emit(&e, NULL, 1024), value = emit(&e, NULL, 1024);
    uint32_t entity = emit(&e, NULL, REF_ENTITY_SIZE);
    uint32_t spots = emit(&e, NULL, MAX_SPOTS * SPOT_SIZE);
    const char strings[] = "classname\0origin\0angles\0angle\0ig_info_stronghold";
    uint32_t names = emit(&e, strings, sizeof(strings));
    uint32_t marker_model = emit(&e, STRONGHOLD_MARKER_MODEL,
                                 sizeof(STRONGHOLD_MARKER_MODEL));
    enum { NAME_CLASS = 0, NAME_ORIGIN = 10, NAME_ANGLES = 17,
           NAME_ANGLE = 24, NAME_STRONGHOLD = 30 };
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
    address(&e, 0, marker_model);
    BYTES(&e, 0x50);
    engine_call(&e, REGISTER_MODEL, 1);
    BYTES(&e, 0x89, 0x83, B4(model), 0x89, 0x83, B4(entity + 8));
    BYTES(&e, 0x85, 0xc0);
    branch(&e, 0x84, PASS);
    address(&e, 2, maxs); BYTES(&e, 0x52);
    address(&e, 2, mins); BYTES(&e, 0x52, 0x50);
    engine_call(&e, MODEL_BOUNDS, 3);
    /* Anchor the model at its bounds' bottom center, so it rests on the
     * traced floor instead of sinking half its height into it.
     */
    for (unsigned i = 0; i < 2; ++i) {
        BYTES(&e, 0xd9, 0x83, B4(mins + i * 4)); /* fld min */
        BYTES(&e, 0xd8, 0x83, B4(maxs + i * 4)); /* fadd max */
        BYTES(&e, 0xd8, 0x8b, B4(half)); /* fmul 0.5 */
        BYTES(&e, 0xd9, 0x9b, B4(anchor + i * 4)); /* fstp anchor */
    }
    BYTES(&e, 0x8b, 0x83, B4(mins + 8), 0x89, 0x83, B4(anchor + 8)); /* bottom */

    label(&e, NEXT_ENTITY);
    token(&e, key, READY);
    BYTES(&e, 0x80, 0xbb, B4(key), '{');
    branch(&e, 0x85, NEXT_ENTITY);
    store(&e, stronghold, 0);
    store(&e, valid_origin, 0);
    for (unsigned i = 0; i < 3; ++i)
        store(&e, angles + i * 4, 0);

    label(&e, NEXT_KEY);
    token(&e, key, READY);
    BYTES(&e, 0x80, 0xbb, B4(key), '}');
    branch(&e, 0x84, SAVE);
    token(&e, value, READY);
    match(&e, key, names + NAME_CLASS, CLASSNAME);
    match(&e, key, names + NAME_ORIGIN, ORIGIN);
    match(&e, key, names + NAME_ANGLES, ANGLES);
    match(&e, key, names + NAME_ANGLE, ANGLE);
    jump(&e, NEXT_KEY);

    label(&e, CLASSNAME);
    address(&e, 0, value);
    address(&e, 2, names + NAME_STRONGHOLD);
    call(&e, EQUALS);
    BYTES(&e, 0x89, 0x83, B4(stronghold));
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
    compare(&e, stronghold, 0); branch(&e, 0x84, NEXT_ENTITY);
    compare(&e, valid_origin, 0); branch(&e, 0x84, NEXT_ENTITY);
    /* Retail SP_ig_info_stronghold also keeps only the first three. */
    compare(&e, count, MAX_SPOTS); branch(&e, 0x83, NEXT_ENTITY);
    /* Point-trace the visible world geometry from above the stronghold
     * origin to 4096 units below it, ignoring player clips. Keep the map
     * origin when the start is inside solid geometry or nothing is hit.
     */
    for (unsigned i = 0; i < 3; ++i) {
        BYTES(&e, 0x8b, 0x83, B4(origin + i * 4)); /* eax=origin component */
        BYTES(&e, 0x89, 0x83, B4(start + i * 4), 0x89, 0x83, B4(end + i * 4));
    }
    BYTES(&e, 0xd9, 0x83, B4(origin + 8), 0xd8, 0x83, B4(raise)); /* z+lift */
    BYTES(&e, 0xd9, 0x9b, B4(start + 8));
    BYTES(&e, 0xd9, 0x83, B4(origin + 8), 0xd8, 0xa3, B4(drop)); /* z-4096 */
    BYTES(&e, 0xd9, 0x9b, B4(end + 8));
    BYTES(&e, 0x6a, CONTENTS_SOLID, 0x6a, 0); /* brush mask; world model */
    const uint32_t trace_args[] = { point, point, end, start, trace };
    for (size_t i = 0; i < ARRAY_SIZE(trace_args); ++i) {
        address(&e, 0, trace_args[i]);
        BYTES(&e, 0x50);
    }
    engine_call(&e, BOX_TRACE, 7);
    compare(&e, trace + 4, 0); branch(&e, 0x85, PLACE); /* startsolid */
    /* The fraction is never negative, so its bits order like integers. */
    compare(&e, trace + 8, FLOAT_ONE); branch(&e, 0x83, PLACE); /* jae */
    for (unsigned i = 0; i < 3; ++i) {
        BYTES(&e, 0x8b, 0x83, B4(trace + 12 + i * 4)); /* eax=endpos */
        BYTES(&e, 0x89, 0x83, B4(origin + i * 4));
    }

    label(&e, PLACE);
    address(&e, 0, axis);
    address(&e, 6, angles);
    retail_call(&e, rva, RETAIL_ANGLES_TO_AXIS);
    for (unsigned i = 0; i < 9; ++i) {
        BYTES(&e, 0xd9, 0x83, B4(axis + i * 4)); /* fld axis */
        BYTES(&e, 0xd8, 0x8b, B4(scale)); /* fmul size */
        BYTES(&e, 0xd9, 0x9b, B4(axis + i * 4));
    }
    /* Subtract the rotated, scaled bounds anchor from the target position.
     * Scaling then changes the extent without moving the marker's anchor.
     */
    for (unsigned i = 0; i < 3; ++i) {
        BYTES(&e, 0xd9, 0x83, B4(origin + i * 4));
        BYTES(&e, 0xd8, 0x83, B4(offset + i * 4));
        for (unsigned j = 0; j < 3; ++j) {
            BYTES(&e, 0xd9, 0x83, B4(anchor + j * 4));
            BYTES(&e, 0xd8, 0x8b, B4(axis + (j * 3 + i) * 4));
            BYTES(&e, 0xde, 0xe9); /* fsubp st(1),st */
        }
        BYTES(&e, 0xd9, 0x9b, B4(origin + i * 4));
    }
    BYTES(&e, 0x8b, 0xbb, B4(count), 0x6b, 0xff, SPOT_SIZE);
    BYTES(&e, 0x8d, 0xbc, 0x3b, B4(spots)); /* edi=spots+count*48 */
    address(&e, 6, origin);
    BYTES(&e, 0xb9, 12, 0, 0, 0, 0xf3, 0xa5); /* copy origin and axis */
    BYTES(&e, 0xff, 0x83, B4(count));
    jump(&e, NEXT_ENTITY);

    label(&e, READY);
    compare(&e, model, 0); branch(&e, 0x84, PASS);
    compare(&e, count, 0); branch(&e, 0x84, PASS);
    BYTES(&e, 0x8b, 0xbb, B4(count)); /* edi=remaining spots */
    address(&e, 6, spots);

    label(&e, DRAW_LOOP);
    /* The renderer copies refEntity_t. Preserve the spot and loop count
     * while copying its origins and scaled axes into the reusable entity.
     */
    const unsigned destinations[] = { 0x0c, 0x44, 0x54, 0x1c };
    for (size_t i = 0; i < ARRAY_SIZE(destinations); ++i) {
        BYTES(&e, 0x56, 0x57);
        if (i == 3)
            BYTES(&e, 0x83, 0xc6, 12);
        address(&e, 7, entity + destinations[i]);
        BYTES(&e, 0xb9, B4(i == 3 ? 9u : 3u), 0xf3, 0xa5, 0x5f, 0x5e);
    }
    address(&e, 0, entity);
    BYTES(&e, 0x50);
    engine_call(&e, ADD_ENTITY, 1);
    BYTES(&e, 0x83, 0xc6, SPOT_SIZE, 0x4f);
    branch(&e, 0x85, DRAW_LOOP);

    label(&e, PASS);
    BYTES(&e, 0xe9, B4(next - ((uint32_t)payload->size + 5)));

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
    fputs("Stronghold marker payload exceeds its size or fixup limit\n", stderr);
    return false;
}

static const payload_feature_t stronghold_markers = {
    CGAME, "persistent centered stronghold models", build_markers,
};

REGISTER_PAYLOAD_FEATURE(stronghold_markers)
