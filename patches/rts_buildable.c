#include "patches.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The main world refdef carries the overlay shader handle in the unused
 * upper half of rdflags. Other scenes keep their own flags. Registering the
 * shader on demand also works after a renderer restart, without a stale
 * cached handle. The syscall chain still submits the valuable models.
 */
static bool build_rts_buildable(payload_t *payload, uint32_t rva,
                               uint32_t next, uint32_t *entry)
{
    enum {
        MAIN_REFDEF = 0xd9e7a0, PM_TYPE = 0xd9df3c,
        REGISTER_SHADER = 39, RENDER_SCENE = 44,
    };
    const char name[] = "igwlegacy/buildable";
    /* The fixed instruction sequence and string fit within this bound. */
    if (!payload_reserve(payload, 256, "RTS buildable"))
        return false;
    uint32_t shader = payload_append(payload, name, sizeof(name));
    *entry = (uint32_t)payload->size;
    EMIT(0x83, 0x7d, 8, RENDER_SCENE);
    uint32_t other_call = EMIT(0x0f, 0x85, 0, 0, 0, 0);
    EMIT(0x8d, 0x83, B4(MAIN_REFDEF - rva));          /* eax=&cg.refdef */
    EMIT(0x39, 0x45, 12);
    uint32_t other_scene = EMIT(0x0f, 0x85, 0, 0, 0, 0);
    EMIT(0x81, 0x60, 0x4c, B4(0xffffu));             /* clear old handle */
    EMIT(0xf6, 0x40, 0x4c, 5);                      /* no world / hyperspace */
    uint32_t no_world = EMIT(0x0f, 0x85, 0, 0, 0, 0);
    EMIT(0x83, 0xbb, B4(PM_TYPE - rva), 4);          /* PM_RTS */
    uint32_t not_rts = EMIT(0x0f, 0x85, 0, 0, 0, 0);
    EMIT(0x89, 0xc6);                               /* esi=refdef */
    EMIT(0x8d, 0x83, B4(shader), 0x50, 0x6a, REGISTER_SHADER);
    EMIT(0xff, 0x53, 8, 0x83, 0xc4, 8);
    EMIT(0xc1, 0xe0, 16, 0x09, 0x46, 0x4c);         /* rdflags |= handle<<16 */
    uint32_t pass = (uint32_t)payload->size;
    const uint32_t branches[] = { other_call, other_scene, no_world, not_rts };
    for (size_t i = 0; i < ARRAY_SIZE(branches); ++i)
        put32(payload->data + branches[i] + 2, pass - (branches[i] + 6));
    EMIT(0xe9, B4(next - (pass + 5)));
    return true;
}

static const payload_feature_t rts_buildable = {
    CGAME, "translucent green buildable surfaces in RTS", build_rts_buildable,
};

/* R_AddDrawSurf takes the shader in EDX and a flag in EAX, with surface,
 * fog index and dlight bits on the stack. Hook only its shift/or pair,
 * which has no relocations. The helper retains the original draw and adds
 * a second draw of the same geometry with the translucent green shader.
 *
 * Retail shader_t: sortedIndex +0x48, surfaceFlags +0x58, contents +0x5c.
 * World surfaces have types 2..4 and geometry at +0x54. For planar faces,
 * geometry +0x0c is the plane's upward normal: match G_CheckBuildSpot's
 * minimum of 0.6. Curved and triangle surfaces use the shader's marks.
 * Models, sprites, HUD scenes and the map render target are excluded.
 *
 * Most maps mark buildable ground per brush side, not per shader: the BSP
 * brushside's flags word becomes collision surfaceFlags 0x80000 at load
 * (CM_LoadBrushSides), which the renderer never sees. Hence unflagged
 * upward faces are classified once with the same world trace the server's
 * build check uses: a short trace through the centroid of the face's first
 * triangle along its normal. The answer is cached in the face plane's
 * unused pad byte (+0x16, zeroed hunk): 0 unknown, 1 buildable, 2 not.
 * Brush-model faces trace the world only and thus stay unmarked.
 *
 * Retail srfSurfaceFace_t: plane +0x04, numIndices +0x1c, ofsIndices +0x20
 * (relative to the face), points +0x24 with a 60-byte stride.
 * CM_BoxTrace takes capsule in EAX and brushmask in ECX; the caller pops
 * results, start, end, mins, maxs and clip model. trace_t keeps
 * surfaceFlags at +0x2c and contents at +0x30.
 *
 * Use call/pop-relative addresses for renderer globals, preserving ASLR.
 * Helpers occupy guarded, unreachable alignment padding after returns.
 */
enum { DRAW_REST = 0xb5127, DRAW_HOOK = 0xb5121 };
enum { CM_BOX_TRACE = 0x1f610, VEC3_ORIGIN = 0xccbca0, BUILD_MASK = 0x2011801 };
enum { NO_LABEL, DONE, CACHED, ELIGIBLE, OVERLAY };
typedef enum { PLAIN, ANCHOR, RELATIVE, BRANCH, CALL } op_kind_t;
typedef struct {
    op_kind_t kind;
    unsigned char size, operand;
    unsigned char bytes[10];
    uint32_t target;
    unsigned label;
    bool terminal;
} op_t;

#define OP(...) { PLAIN, sizeof((unsigned char[]){ __VA_ARGS__ }), 0, \
                  { __VA_ARGS__ }, 0, 0, false }
#define JCC(cc, target) { BRANCH, 6, 2, { 0x0f, cc }, target, 0, false }
#define BASE(size, at, rva, ...) \
    { RELATIVE, size, at, { __VA_ARGS__ }, rva, 0, false }
/* Classification frame below the saved EAX/EDX: trace_t at +0, trace start
 * at +0x38, trace end at +0x44 and the integer 3 at +0x50.
 */
#define FRAME 0x54
/* Average one axis of the triangle's corners (EAX/ECX/EDX), then store
 * centroid + normal as trace start and centroid - normal as trace end.
 */
#define CENTROID_AXIS(o) \
    OP(0xd9, 0x40, o), OP(0xd8, 0x41, o), OP(0xd8, 0x42, o), \
    OP(0xda, 0x74, 0x24, 0x50), OP(0xd9, 0xc0), OP(0xd8, 0x46, 4 + (o)), \
    OP(0xd9, 0x5c, 0x24, 0x38 + (o)), OP(0xd8, 0x66, 4 + (o)), \
    OP(0xd9, 0x5c, 0x24, 0x44 + (o))

static const op_t overlay_ops[] = {
    OP(0x50, 0x53, 0x51, 0x56, 0x57),              /* save eax,ebx,ecx,esi,edi */
    { ANCHOR, 6, 0, { 0xe8, 0, 0, 0, 0, 0x5b }, 0, 0, false },
    BASE(6, 2, 0xd3f084, 0x8b, 0x83),             /* eax=tr.refdef.rdflags */
    OP(0xc1, 0xe8, 16),                            /* eax=shader handle */
    JCC(0x84, DONE),
    BASE(6, 2, 0xd44264, 0x3b, 0x83),              /* validate shader handle */
    JCC(0x83, DONE),
    OP(0x8b, 0x7c, 0x24, 28),                     /* edi=world surface */
    OP(0x8b, 0x0f, 0x83, 0xe9, 2),               /* surfaceType - SF_FACE */
    OP(0x83, 0xf9, 2),
    JCC(0x87, DONE),                               /* skip non-world geometry */
    OP(0xf7, 0x42, 0x58, B4(0x80000u)),            /* surfaceparm canbuild */
    JCC(0x85, ELIGIBLE),
    OP(0xf7, 0x42, 0x5c, B4(0x400u)),              /* legacy buildable contents */
    JCC(0x85, ELIGIBLE),
    OP(0x83, 0x3f, 2),                             /* only faces have sides */
    JCC(0x85, DONE),
    OP(0x8b, 0x77, 0x54),                          /* esi=face geometry */
    OP(0x81, 0x7e, 0x0c, B4(0x3f19999au)),         /* normal.z >= 0.6f */
    JCC(0x8c, DONE),
    OP(0x80, 0x7e, 0x16, 0),                       /* already classified? */
    JCC(0x85, CACHED),
    OP(0x83, 0x7e, 0x1c, 3),                       /* need one triangle */
    JCC(0x8c, DONE),
    OP(0x50, 0x52),                                /* keep handle and shader */
    OP(0x83, 0xec, FRAME),
    OP(0xc7, 0x44, 0x24, 0x50, B4(3)),
    OP(0x8b, 0x56, 0x20, 0x01, 0xf2),              /* edx=first indices */
    OP(0x6b, 0x02, 60),                            /* corner offsets */
    OP(0x6b, 0x4a, 4, 60),
    OP(0x6b, 0x52, 8, 60),
    OP(0x8d, 0x44, 0x06, 0x24),                    /* corner pointers */
    OP(0x8d, 0x4c, 0x0e, 0x24),
    OP(0x8d, 0x54, 0x16, 0x24),
    CENTROID_AXIS(0), CENTROID_AXIS(4), CENTROID_AXIS(8),
    OP(0x6a, 0),                                   /* world clip model */
    BASE(6, 2, VEC3_ORIGIN, 0x8d, 0x83),           /* point-sized trace */
    OP(0x50, 0x50),                                /* maxs, mins */
    OP(0x8d, 0x44, 0x24, 0x44 + 12, 0x50),         /* end */
    OP(0x8d, 0x44, 0x24, 0x38 + 16, 0x50),         /* start */
    OP(0x8d, 0x44, 0x24, 20, 0x50),                /* trace result */
    OP(0xb9, B4(BUILD_MASK), 0x31, 0xc0),          /* mask; no capsule */
    { CALL, 5, 1, { 0xe8 }, CM_BOX_TRACE, 0, false },
    OP(0x83, 0xc4, 24),
    OP(0x8b, 0x44, 0x24, 0x2c, 0xc1, 0xe8, 9),     /* canbuild side -> 0x400 */
    OP(0x0b, 0x44, 0x24, 0x30),                    /* | buildable contents */
    OP(0xc1, 0xe8, 10, 0x83, 0xe0, 1),
    OP(0xb1, 2, 0x28, 0xc1, 0x88, 0x4e, 0x16),     /* cache 1 or 2 */
    OP(0x83, 0xc4, FRAME, 0x5a, 0x58),
    { PLAIN, 4, 0, { 0x80, 0x7e, 0x16, 1 }, 0, CACHED, false },
    JCC(0x85, DONE),
    /* Buildable faces fall through to the normal check again (harmless). */
    { PLAIN, 3, 0, { 0x83, 0x3f, 2 }, 0, ELIGIBLE, false },
    JCC(0x85, OVERLAY),
    OP(0x8b, 0x77, 0x54),                          /* esi=face geometry */
    OP(0x81, 0x7e, 0x0c, B4(0x3f19999au)),         /* normal.z >= 0.6f */
    JCC(0x8c, DONE),
    { RELATIVE, 7, 3, { 0x8b, 0xb4, 0x83 }, 0xd44268, OVERLAY, false },
    /* Re-enter after the hook with a normal call frame and the original
     * arguments. This submits the terrain beneath the overlay exactly once.
     */
    OP(0xff, 0x74, 0x24, 36),                      /* dlight bits */
    OP(0xff, 0x74, 0x24, 36),                      /* fog index */
    OP(0xff, 0x74, 0x24, 36),                      /* surface */
    OP(0x8b, 0x44, 0x24, 28),                      /* original eax */
    OP(0x8b, 0x4c, 0x24, 20),                      /* original draw index */
    OP(0xc1, 0xe0, 14, 0x0b, 0x42, 0x48),         /* displaced shift/or */
    { CALL, 5, 1, { 0xe8 }, DRAW_REST, 0, false },
    OP(0x83, 0xc4, 12, 0x89, 0xf2),                /* edx=overlay shader */
    BASE(6, 2, 0xd3f1c8, 0x8b, 0x83),              /* new draw-surface index */
    OP(0x89, 0x44, 0x24, 8),                       /* refresh saved ecx */
    OP(0xc7, 0x44, 0x24, 32, B4(0)),               /* overlay has no fog */
    OP(0xc7, 0x44, 0x24, 36, B4(0)),               /* overlay has no dlight */
    { PLAIN, 5, 0, { 0x5f, 0x5e, 0x59, 0x5b, 0x58 }, 0, DONE, false },
    OP(0xc1, 0xe0, 14, 0x0b, 0x42, 0x48),
    { PLAIN, 1, 0, { 0xc3 }, 0, 0, true },
};

#define CC15 { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, \
               0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }
#define SLOT(rva) { ENGINE, true, "RTS buildable: overlay helper", rva, 15, CC15, CC15 }
static code_rule_t overlay_helper[] = {
    SLOT(0x80591), SLOT(0x80881), SLOT(0x81a61), SLOT(0x81b81),
    SLOT(0x824a1), SLOT(0x8fb51), SLOT(0x90af1), SLOT(0x92351),
    SLOT(0x9a1c1), SLOT(0x9b861), SLOT(0x9f471), SLOT(0x9fa51),
    SLOT(0xa77d1), SLOT(0xaf031), SLOT(0xb06b1), SLOT(0xb27b1),
    SLOT(0xb87a1), SLOT(0xba281), SLOT(0xbad91), SLOT(0xbc5e1),
    SLOT(0xbe431), SLOT(0xbe501), SLOT(0xbe751), SLOT(0xbff11),
    SLOT(0xc19e1), SLOT(0xc2481), SLOT(0xc5711), SLOT(0xc5821),
    SLOT(0xc5ad1), SLOT(0xc6ec1), SLOT(0xc87e1), SLOT(0xc90f1),
    SLOT(0xc9a51), SLOT(0xcad01), SLOT(0xcb0f1), SLOT(0xcb831),
    SLOT(0xcbe11), SLOT(0xccb61), SLOT(0xcdaf1), SLOT(0xd12c1),
    SLOT(0xd2031), SLOT(0xd2891), SLOT(0xd5c91), SLOT(0xd6101),
    SLOT(0xd6a01), SLOT(0xdac01), SLOT(0xdb841), SLOT(0xdcf91),
    SLOT(0xde131), SLOT(0xde331), SLOT(0xdee11), SLOT(0xe0031),
    SLOT(0xe09c1), SLOT(0xe2161), SLOT(0xe2721), SLOT(0xe27e1),
    SLOT(0xe2ce1), SLOT(0xe3c01), SLOT(0xe4111), SLOT(0xe4f61),
};

static const code_rule_t overlay_hook = {
    ENGINE, true, "RTS buildable: submit translucent overlay", DRAW_HOOK, 6,
    { 0xc1, 0xe0, 14, 0x0b, 0x42, 0x48 },
    { 0xe8, B4(0x80591u - (DRAW_HOOK + 5u)), 0x90 },
};

/* The retail compiler inlines R_AddDrawSurf in R_RecursiveWorldNode and
 * the brush-model loop. Redirect their shift/or pairs through the shared
 * function so terrain reaches the overlay hook. Keep the unused inline
 * instructions intact because they still have loader relocation entries.
 * EAX already contains the renderer flag; the other arguments are live in
 * ESI (surface), EDX (fog) and EDI/EBX (world/brush dlight bits).
 */
static const code_rule_t overlay_inline_paths[] = {
    { ENGINE, true, "RTS buildable: world submission", 0xb6452, 6,
      { 0xc1, 0xe0, 14, 0x0b, 0x43, 0x48 },
      { 0xe9, B4(0xc3ea1u - 0xb6457u), 0x90 } },
    { ENGINE, true, "RTS buildable: world submission helper", 0xc3ea1, 15,
      CC15,
      { 0x57, 0x52, 0x56, 0x89, 0xda,           /* push args; edx=ebx */
        0xe8, B4(0xb5110u - 0xc3eabu),
        0xe9, B4(0xc4351u - 0xc3eb0u) } },
    { ENGINE, true, "RTS buildable: world loop continuation", 0xc4351, 15,
      CC15,
      { 0x83, 0xc4, 12, 0xe9, B4(0xb6487u - 0xc4359u),
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc } },
    { ENGINE, true, "RTS buildable: brush submission", 0xb60ee, 6,
      { 0xc1, 0xe0, 14, 0x0b, 0x45, 0x48 },
      { 0xe9, B4(0xc45f1u - 0xb60f3u), 0x90 } },
    { ENGINE, true, "RTS buildable: brush submission helper", 0xc45f1, 15,
      CC15,
      { 0x53, 0x52, 0x56, 0x89, 0xea,           /* push args; edx=ebp */
        0xe8, B4(0xb5110u - 0xc45fbu),
        0xe9, B4(0xc4971u - 0xc4600u) } },
    { ENGINE, true, "RTS buildable: brush loop continuation", 0xc4971, 15,
      CC15,
      { 0x83, 0xc4, 12, 0xe9, B4(0xb6123u - 0xc4979u),
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc } },
};

PATCH_CONSTRUCTOR(pack_overlay_helper)
{
    size_t slots[ARRAY_SIZE(overlay_ops)], positions[ARRAY_SIZE(overlay_ops)];
    uint32_t labels[OVERLAY + 1] = {0}, anchor = 0;
    size_t slot = 0, pos = 0;

    /* Lay out whole instructions before resolving branches across slots. */
    for (size_t i = 0; i < ARRAY_SIZE(overlay_ops); ++i) {
        const op_t *op = &overlay_ops[i];
        if (pos + op->size + (op->terminal ? 0 : 5) > 15) {
            code_rule_t *from = &overlay_helper[slot];
            if (++slot == ARRAY_SIZE(overlay_helper)) {
                fputs("RTS buildable helper exceeds its padding slots\n", stderr);
                exit(EXIT_FAILURE);
            }
            from->replacement[pos] = 0xe9;
            put32(from->replacement + pos + 1,
                  overlay_helper[slot].rva - (from->rva + (uint32_t)pos + 5));
            pos = 0;
        }
        slots[i] = slot;
        positions[i] = pos;
        uint32_t at = overlay_helper[slot].rva + (uint32_t)pos;
        if (op->label)
            labels[op->label] = at;
        if (op->kind == ANCHOR)
            anchor = at + 5;
        memcpy(overlay_helper[slot].replacement + pos, op->bytes, op->size);
        pos += op->size;
    }
    for (size_t i = slot + 1; i < ARRAY_SIZE(overlay_helper); ++i)
        overlay_helper[i].enabled = false;

    for (size_t i = 0; i < ARRAY_SIZE(overlay_ops); ++i) {
        const op_t *op = &overlay_ops[i];
        code_rule_t *rule = &overlay_helper[slots[i]];
        uint32_t at = rule->rva + (uint32_t)positions[i];
        unsigned char *out = rule->replacement + positions[i] + op->operand;
        if (op->kind == BRANCH)
            put32(out, labels[op->target] - (at + op->size));
        else if (op->kind == CALL)
            put32(out, op->target - (at + op->size));
        else if (op->kind == RELATIVE)
            put32(out, op->target - anchor);
    }
}

REGISTER_PAYLOAD_FEATURE(rts_buildable)
REGISTER_CODE_RULES(overlay_helper)
REGISTER_CODE_RULE(overlay_hook)
REGISTER_CODE_RULES(overlay_inline_paths)
