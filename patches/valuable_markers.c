#include "patches.h"

#include <float.h>
#include <stdio.h>
#include <string.h>

/* Edit these settings and rebuild. Offsets use world X/Y/Z map units and
 * are applied after floor placement, including to suspended spawn points.
 */
// #define VALUABLE_MARKER_MODEL     "models/props/jewelry01.md3"
#define VALUABLE_MARKER_MODEL     "models/mapobjects/item_highlight/item_highlight.md3"
#define VALUABLE_MARKER_OFFSET_X  0.0f
#define VALUABLE_MARKER_OFFSET_Y  0.0f
#define VALUABLE_MARKER_OFFSET_Z  -15.0f

_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24,
               "Valuable marker offsets require IEEE binary32 floats");
_Static_assert(sizeof(VALUABLE_MARKER_MODEL) > 1,
               "Valuable marker model path must not be empty");

/* Persistent client-side models at the BSP's item_jewel positions. The
 * retail syscalls are confirmed by reigw's cg_syscalls.c; CG_DrawActive
 * (0x12680) renders cg.refdef at RVA 0xd9e7a0. Intercepting that exact
 * refdef excludes HUD models and the map's separate render target.
 *
 * Read the entity stream immediately after R_LoadWorldMap. Reading through
 * EOF rewinds the renderer's token cursor, so CG_GetSkyBoxData still reads
 * the original stream. No server entities, pickups or snapshots change.
 *
 * Emit position-independent i386 code as the existing CVAR payload does.
 * EBX is the injected section's runtime base, EBP the dispatcher's frame,
 * and [EBX+8] the engine syscall. Calls to retail helpers use relative RVAs.
 */
enum {
    MAX_SPOTS = 1024, SPOT_SIZE = 48, REF_ENTITY_SIZE = 0xa68,
    LOAD_WORLD = 36, REGISTER_MODEL = 37, ADD_ENTITY = 41, RENDER_SCENE = 44,
    BOX_TRACE = 25, GET_TOKEN = 86,
    RETAIL_ATOF = 0x84dc9, RETAIL_ATOI = 0x82e32,
    RETAIL_ANGLES_TO_AXIS = 0x3c0d0, MAIN_REFDEF = 0xd9e7a0,
};

enum {
    LOAD, NEXT_ENTITY, NEXT_KEY, CLASSNAME, ORIGIN, ANGLES, ANGLE,
    SPAWNFLAGS, SAVE, KEEP_ORIGIN, LOADED, DRAW, DRAW_LOOP, PASS,
    TOKEN, EQUALS, EQUALS_LOOP, UNEQUAL, VECTOR, VECTOR_NEXT,
    VECTOR_NUMBER, VECTOR_SKIP, VECTOR_DELIMITER, VECTOR_OK, VECTOR_BAD,
    LABEL_COUNT
};

typedef struct {
    payload_t *payload;
    bool ok;
    uint32_t rva;
    uint32_t labels[LABEL_COUNT];
    struct { uint32_t at; unsigned label; } fixups[128];
    size_t fixup_count;
} markers_emitter_t;

static uint32_t emit(markers_emitter_t *e, const void *bytes, size_t size)
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

static uint32_t word(markers_emitter_t *e, uint32_t value)
{
    unsigned char bytes[4];
    put32(bytes, value);
    return emit(e, bytes, sizeof(bytes));
}

static void reference(markers_emitter_t *e, unsigned label)
{
    uint32_t at = word(e, 0);
    if (e->fixup_count == ARRAY_SIZE(e->fixups)) {
        e->ok = false;
        return;
    }
    e->fixups[e->fixup_count].at = at;
    e->fixups[e->fixup_count++].label = label;
}

static void label(markers_emitter_t *e, unsigned name)
{
    e->labels[name] = (uint32_t)e->payload->size;
}

static void jump(markers_emitter_t *e, unsigned name)
{
    BYTES(e, 0xe9);
    reference(e, name);
}

/* The condition byte is the low byte of a near Jcc opcode (JE = 0x84). */
static void branch(markers_emitter_t *e, unsigned condition, unsigned name)
{
    BYTES(e, 0x0f, condition);
    reference(e, name);
}

static void call(markers_emitter_t *e, unsigned name)
{
    BYTES(e, 0xe8);
    reference(e, name);
}

static void retail_call(markers_emitter_t *e, uint32_t target)
{
    BYTES(e, 0xe8);
    word(e, target - (e->rva + (uint32_t)e->payload->size + 4));
}

/* LEA reg,[EBX+offset]; register numbers are the i386 encoding. */
static void address(markers_emitter_t *e, unsigned reg, uint32_t offset)
{
    BYTES(e, 0x8d, 0x83 | (reg << 3));
    word(e, offset);
}

static void store(markers_emitter_t *e, uint32_t offset, uint32_t value)
{
    BYTES(e, 0xc7, 0x83);
    word(e, offset);
    word(e, value);
}

static void store_eax(markers_emitter_t *e, uint32_t offset)
{
    BYTES(e, 0x89, 0x83);
    word(e, offset);
}

static void compare(markers_emitter_t *e, uint32_t offset, uint32_t value)
{
    BYTES(e, 0x81, 0xbb);
    word(e, offset);
    word(e, value);
}

static void engine_call(markers_emitter_t *e, unsigned number, unsigned argc)
{
    BYTES(e, 0x6a, number, 0xff, 0x53, 8, 0x83, 0xc4, (argc + 1) * 4);
}

static void token(markers_emitter_t *e, uint32_t buffer, unsigned eof)
{
    address(e, 0, buffer);
    call(e, TOKEN);
    BYTES(e, 0x85, 0xc0); /* test eax,eax */
    branch(e, 0x84, eof);
}

static void match(markers_emitter_t *e, uint32_t buffer, uint32_t text, unsigned yes)
{
    address(e, 0, buffer);
    address(e, 2, text);
    call(e, EQUALS);
    BYTES(e, 0x85, 0xc0);
    branch(e, 0x85, yes);
}

static void copy_vector(markers_emitter_t *e, uint32_t source, uint32_t destination)
{
    address(e, 6, source);
    address(e, 7, destination);
    BYTES(e, 0xb9, 3, 0, 0, 0, 0xf3, 0xa5); /* ecx=3; rep movsd */
}

static bool build_markers(payload_t *payload, uint32_t rva, uint32_t next, uint32_t *entry)
{
    markers_emitter_t e = { .payload = payload, .ok = true, .rva = rva };
    for (size_t i = 0; i < LABEL_COUNT; ++i)
        e.labels[i] = UINT32_MAX;

    /* Cache origin and its three axis vectors once per spot. Reuse one
     * full-sized retail refEntity_t when submitting the static models.
     */
    uint32_t count = word(&e, 0), model = word(&e, 0);
    uint32_t jewel = word(&e, 0), valid_origin = word(&e, 0), flags = word(&e, 0);
    uint32_t origin = emit(&e, NULL, 12), angles = emit(&e, NULL, 12);
    uint32_t end = emit(&e, NULL, 12), trace = emit(&e, NULL, 56);
    uint32_t mins = (uint32_t)payload->size;
    word(&e, 0xc1a00000); word(&e, 0xc1a00000); word(&e, 0xc1700000); /* -20,-20,-15 */
    uint32_t maxs = (uint32_t)payload->size;
    word(&e, 0x41a00000); word(&e, 0x41a00000); word(&e, 0x41700000); /* 20,20,15 */
    uint32_t drop = word(&e, 0x45800000); /* 4096.0 */
    uint32_t offset = (uint32_t)payload->size;
    const float offsets[] = {
        VALUABLE_MARKER_OFFSET_X, VALUABLE_MARKER_OFFSET_Y, VALUABLE_MARKER_OFFSET_Z,
    };
    /* Store the native float representation as little-endian payload data. */
    for (size_t i = 0; i < ARRAY_SIZE(offsets); ++i) {
        uint32_t bits;
        memcpy(&bits, &offsets[i], sizeof(bits));
        word(&e, bits);
    }
    uint32_t key = emit(&e, NULL, 1024), value = emit(&e, NULL, 1024);
    uint32_t entity = emit(&e, NULL, REF_ENTITY_SIZE);
    uint32_t spots = emit(&e, NULL, MAX_SPOTS * SPOT_SIZE);
    const char strings[] = "classname\0origin\0angles\0angle\0spawnflags\0item_jewel";
    uint32_t names = emit(&e, strings, sizeof(strings));
    const char model_path[] = VALUABLE_MARKER_MODEL;
    uint32_t marker_model = emit(&e, model_path, sizeof(model_path));
    enum { NAME_CLASS = 0, NAME_ORIGIN = 10, NAME_ANGLES = 17, NAME_ANGLE = 24,
           NAME_FLAGS = 30, NAME_JEWEL = 41 };
    if (!e.ok)
        goto failed;
    put32(payload->data + entity + 4, 64); /* RF_NOSHADOW */
    put32(payload->data + entity + 0x74, UINT32_MAX); /* shaderRGBA */

    *entry = (uint32_t)payload->size;
    BYTES(&e, 0x83, 0x7d, 8, LOAD_WORLD); /* cmp [ebp+8],LOAD_WORLD */
    branch(&e, 0x84, LOAD);
    BYTES(&e, 0x83, 0x7d, 8, RENDER_SCENE);
    branch(&e, 0x84, DRAW);
    jump(&e, PASS);

    label(&e, LOAD);
    /* Keep the engine's original return value below all helper calls. */
    BYTES(&e, 0xff, 0x75, 12); /* push original world-map path */
    engine_call(&e, LOAD_WORLD, 1);
    BYTES(&e, 0x50);
    store(&e, count, 0);
    address(&e, 0, marker_model);
    BYTES(&e, 0x50);
    engine_call(&e, REGISTER_MODEL, 1);
    store_eax(&e, model);
    store_eax(&e, entity + 8); /* refEntity.hModel */

    label(&e, NEXT_ENTITY);
    token(&e, key, LOADED);
    BYTES(&e, 0x80, 0xbb); word(&e, key); BYTES(&e, '{');
    branch(&e, 0x85, NEXT_ENTITY);
    store(&e, jewel, 0);
    store(&e, valid_origin, 0);
    store(&e, flags, 0);
    for (unsigned i = 0; i < 3; ++i)
        store(&e, angles + i * 4, 0);

    label(&e, NEXT_KEY);
    token(&e, key, LOADED);
    BYTES(&e, 0x80, 0xbb); word(&e, key); BYTES(&e, '}');
    branch(&e, 0x84, SAVE);
    token(&e, value, LOADED);
    match(&e, key, names + NAME_CLASS, CLASSNAME);
    match(&e, key, names + NAME_ORIGIN, ORIGIN);
    match(&e, key, names + NAME_ANGLES, ANGLES);
    match(&e, key, names + NAME_ANGLE, ANGLE);
    match(&e, key, names + NAME_FLAGS, SPAWNFLAGS);
    jump(&e, NEXT_KEY);

    label(&e, CLASSNAME);
    address(&e, 0, value);
    address(&e, 2, names + NAME_JEWEL);
    call(&e, EQUALS);
    store_eax(&e, jewel);
    jump(&e, NEXT_KEY);

    label(&e, ORIGIN);
    address(&e, 6, value);
    address(&e, 7, origin);
    call(&e, VECTOR);
    store_eax(&e, valid_origin);
    jump(&e, NEXT_KEY);

    label(&e, ANGLES);
    address(&e, 6, value);
    address(&e, 7, angles);
    call(&e, VECTOR);
    jump(&e, NEXT_KEY);

    label(&e, ANGLE);
    address(&e, 0, value);
    BYTES(&e, 0x50);
    retail_call(&e, RETAIL_ATOF);
    BYTES(&e, 0x83, 0xc4, 4, 0xd9, 0x9b); word(&e, angles + 4); /* fstp yaw */
    jump(&e, NEXT_KEY);

    label(&e, SPAWNFLAGS);
    address(&e, 0, value);
    BYTES(&e, 0x50);
    retail_call(&e, RETAIL_ATOI);
    BYTES(&e, 0x83, 0xc4, 4);
    store_eax(&e, flags);
    jump(&e, NEXT_KEY);

    label(&e, SAVE);
    compare(&e, jewel, 0); branch(&e, 0x84, NEXT_ENTITY);
    compare(&e, valid_origin, 0); branch(&e, 0x84, NEXT_ENTITY);
    compare(&e, count, MAX_SPOTS); branch(&e, 0x83, NEXT_ENTITY); /* jae */
    BYTES(&e, 0xf6, 0x83); word(&e, flags); BYTES(&e, 1); /* suspended item */
    branch(&e, 0x85, KEEP_ORIGIN);

    /* Match FinishSpawningItem's jewel box and downward world trace. Keep
     * the map origin when the trace starts inside solid geometry.
     */
    copy_vector(&e, origin, end);
    BYTES(&e, 0xd9, 0x83); word(&e, origin + 8); /* fld z */
    BYTES(&e, 0xd8, 0xa3); word(&e, drop); /* fsub 4096 */
    BYTES(&e, 0xd9, 0x9b); word(&e, end + 8);
    BYTES(&e, 0x68); word(&e, 0x2010801); /* MASK_PLAYERSOLID */
    BYTES(&e, 0x6a, 0); /* world collision model */
    const uint32_t trace_args[] = { maxs, mins, end, origin, trace };
    for (size_t i = 0; i < ARRAY_SIZE(trace_args); ++i) {
        address(&e, 0, trace_args[i]);
        BYTES(&e, 0x50);
    }
    engine_call(&e, BOX_TRACE, 7);
    compare(&e, trace + 4, 0); branch(&e, 0x85, KEEP_ORIGIN);
    copy_vector(&e, trace + 12, origin);

    label(&e, KEEP_ORIGIN);
    /* Offset only the final model position, leaving its floor trace at
     * the original spawn coordinates. Cache it once to avoid accumulation.
     */
    for (unsigned i = 0; i < 3; ++i) {
        BYTES(&e, 0xd9, 0x83); word(&e, origin + i * 4); /* fld coordinate */
        BYTES(&e, 0xd8, 0x83); word(&e, offset + i * 4); /* fadd offset */
        BYTES(&e, 0xd9, 0x9b); word(&e, origin + i * 4); /* fstp coordinate */
    }
    BYTES(&e, 0x8b, 0xbb); word(&e, count); /* edi=count*48+spots */
    BYTES(&e, 0x6b, 0xff, SPOT_SIZE, 0x8d, 0xbc, 0x3b); word(&e, spots);
    address(&e, 6, origin);
    BYTES(&e, 0xb9, 3, 0, 0, 0, 0xf3, 0xa5); /* copy origin; edi now axis */
    BYTES(&e, 0x89, 0xf8); /* eax=axis */
    address(&e, 6, angles); /* retail AnglesToAxis takes angles in ESI */
    retail_call(&e, RETAIL_ANGLES_TO_AXIS);
    BYTES(&e, 0xff, 0x83); word(&e, count);
    jump(&e, NEXT_ENTITY);

    label(&e, LOADED);
    BYTES(&e, 0x58, 0x5f, 0x5e, 0x5b, 0xc9, 0xc3); /* result; restore dispatcher; ret */

    label(&e, DRAW);
    address(&e, 0, MAIN_REFDEF - rva);
    BYTES(&e, 0x39, 0x45, 12); /* only the main world's refdef */
    branch(&e, 0x85, PASS);
    BYTES(&e, 0xf6, 0x40, 0x4c, 5); /* no world / hyperspace */
    branch(&e, 0x85, PASS);
    compare(&e, model, 0); branch(&e, 0x84, PASS);
    compare(&e, count, 0); branch(&e, 0x84, PASS);
    BYTES(&e, 0x8b, 0xbb); word(&e, count); /* edi=remaining spots */
    address(&e, 6, spots);

    label(&e, DRAW_LOOP);
    /* Engine submission copies refEntity_t. ESI/EDI retain the spot and
     * loop count across engine calls; REP MOVSD's scratch is saved here.
     */
    const unsigned destinations[] = { 0x0c, 0x44, 0x54, 0x1c };
    for (size_t i = 0; i < ARRAY_SIZE(destinations); ++i) {
        BYTES(&e, 0x56, 0x57); /* save spot and remaining count */
        if (i == 3)
            BYTES(&e, 0x83, 0xc6, 12); /* spot.axis */
        address(&e, 7, entity + destinations[i]);
        BYTES(&e, 0xb9); word(&e, i == 3 ? 9 : 3);
        BYTES(&e, 0xf3, 0xa5, 0x5f, 0x5e);
    }
    address(&e, 0, entity);
    BYTES(&e, 0x50);
    engine_call(&e, ADD_ENTITY, 1);
    BYTES(&e, 0x83, 0xc6, SPOT_SIZE, 0x4f); /* next spot; dec edi */
    branch(&e, 0x85, DRAW_LOOP);

    label(&e, PASS);
    BYTES(&e, 0xe9);
    word(&e, next - ((uint32_t)payload->size + 4));

    label(&e, TOKEN);
    BYTES(&e, 0x68); word(&e, 1024);
    BYTES(&e, 0x50);
    engine_call(&e, GET_TOKEN, 2);
    BYTES(&e, 0xc3);

    label(&e, EQUALS);
    BYTES(&e, 0x56, 0x57, 0x89, 0xc6, 0x89, 0xd7); /* save; esi=a; edi=b */
    label(&e, EQUALS_LOOP);
    BYTES(&e, 0x8a, 0x06, 0x3a, 0x07); /* al=*a; cmp al,*b */
    branch(&e, 0x85, UNEQUAL);
    BYTES(&e, 0x46, 0x47, 0x84, 0xc0);
    branch(&e, 0x85, EQUALS_LOOP);
    BYTES(&e, 0xb8, 1, 0, 0, 0, 0x5f, 0x5e, 0xc3);
    label(&e, UNEQUAL);
    BYTES(&e, 0x31, 0xc0, 0x5f, 0x5e, 0xc3);

    /* Parse the map's three whitespace-separated coordinates using the
     * retail C runtime's atof. Each x87 result is popped into one float.
     * Missing components reject an origin instead of reusing stale data.
     */
    label(&e, VECTOR);
    BYTES(&e, 0x55, 0xbd, 3, 0, 0, 0); /* save ebp; three components */
    label(&e, VECTOR_NEXT);
    BYTES(&e, 0x8a, 0x06, 0x84, 0xc0);
    branch(&e, 0x84, VECTOR_BAD);
    BYTES(&e, 0x3c, 32);
    branch(&e, 0x87, VECTOR_NUMBER); /* ja: non-whitespace */
    BYTES(&e, 0x46);
    jump(&e, VECTOR_NEXT);
    label(&e, VECTOR_NUMBER);
    BYTES(&e, 0x56);
    retail_call(&e, RETAIL_ATOF);
    BYTES(&e, 0x83, 0xc4, 4, 0xd9, 0x1f, 0x83, 0xc7, 4); /* fstp [edi]; next float */
    label(&e, VECTOR_SKIP);
    BYTES(&e, 0x8a, 0x06, 0x3c, 32);
    branch(&e, 0x86, VECTOR_DELIMITER); /* jbe */
    BYTES(&e, 0x46);
    jump(&e, VECTOR_SKIP);
    label(&e, VECTOR_DELIMITER);
    BYTES(&e, 0x4d); /* dec ebp */
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
    fputs("Valuable marker payload exceeds its size or fixup limit\n", stderr);
    return false;
}

static const payload_feature_t valuable_markers = {
    CGAME, "persistent valuable spawn models", build_markers,
};

REGISTER_PAYLOAD_FEATURE(valuable_markers)
