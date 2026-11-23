#include "patches.h"

/* World-space grid spacing in map units. Rebuild the patcher and install
 * cgamex86.dll after editing these constants. Z still follows the ground.
 * Whole units keep the preview exact when the build command serializes
 * its coordinates as integers. The grid is anchored at world (0, 0).
 */
#define RTS_GRID_SIZE_X 32
#define RTS_GRID_SIZE_Y 32
#define RTS_TURRET_ANGLE_STEP 45

_Static_assert(RTS_GRID_SIZE_X > 0 && RTS_GRID_SIZE_X <= 32768,
               "RTS X grid spacing must be between 1 and 32768 map units");
_Static_assert(RTS_GRID_SIZE_Y > 0 && RTS_GRID_SIZE_Y <= 32768,
               "RTS Y grid spacing must be between 1 and 32768 map units");
_Static_assert(RTS_TURRET_ANGLE_STEP > 0 &&
               360 % RTS_TURRET_ANGLE_STEP == 0,
               "RTS turret angle step must be a positive divisor of 360");

enum { TRACE = 0x2e000, BUILD_TYPE = 0xda02ec, BUILD_YAW = 0xda02d8 };

/* Redirect the first cursor ray in the ordinary placement checks. The
 * existing checks then consume the snapped trace, including its ground
 * height, surface normal, buildable flags and entity hits. Trip mines and
 * stronghold selection retain their separate, original placement rays.
 *
 * The interpolation hook replaces fld [esp+0x74]; fld st(0) in the preview's
 * interpolation. Both the preview and the build command read its result;
 * interpolating between grid points would let a click submit off-grid XY.
 * The helper keeps the original interpolation for trip mines and strongholds.
 */
static code_rule_t patch_rts_grid_hooks[] = {
    { CGAME, true, "RTS grid: ground placement trace", 0x73e71, 5,
      { 0xe8, B4(TRACE - 0x73e76u) }, { 0xe8, 0, 0, 0, 0 } },
    { CGAME, true, "RTS grid: keep the preview on its grid point", 0x74524, 6,
      { 0xd9, 0x44, 0x24, 0x74, 0xd9, 0xc0 },
      { 0xe8, 0, 0, 0, 0, 0x90 } },
    /* Snap the computed yaw before the rotation handler returns. Hook its
     * epilogue so the preceding absolute yaw store keeps its relocation.
     */
    { CGAME, true, "RTS grid: turret rotation", 0x737e4, 5,
      { 0x5f, 0x5e, 0x5b, 0x8b, 0xe5 }, { 0xe9, 0, 0, 0, 0 } },
};

/* All helpers live in the existing injected section. Relative calls and
 * call/pop addressing let the DLL rebase without additional relocations.
 */
static void trace_call(payload_t *payload, uint32_t rva)
{
    /* CG_Trace uses ECX=start, EDX=end, EBX=mins, with result, maxs,
     * skipped entity and contents mask on the stack. Copy those arguments
     * from our frame for each call; the caller retains its own copies.
     */
    EMIT(0xff, 0x75, 20, 0xff, 0x75, 16,
         0xff, 0x75, 12, 0xff, 0x75, 8);
    emit_call(payload, rva, TRACE);
    EMIT(0x83, 0xc4, 16);
}

static bool build_rts_grid(payload_t *payload, uint32_t rva,
                           uint32_t next, uint32_t *entry)
{
    /* This fixed helper sequence needs less than 512 bytes, including
     * both axes and rotation. Reserve it before emitting any instructions.
     */
    if (!payload_reserve(payload, 512, "RTS grid"))
        return false;
    *entry = emit_entry(payload, next);

    uint32_t trace = (uint32_t)payload->size;
    EMIT(0x55, 0x89, 0xe5, 0x83, 0xec, 36); /* frame and private ray */
    /* Retain the cursor ray's vertical range. Its first hit supplies XY;
     * a second, vertical ray finds the ground at the snapped coordinates.
     */
    EMIT(0x8b, 0x41, 8, 0x89, 0x45, 0xf4); /* start.z at ebp-12 */
    EMIT(0x8b, 0x42, 8, 0x89, 0x45, 0xe8); /* end.z at ebp-24 */
    trace_call(payload, rva);
    EMIT(0x8b, 0x45, 8);                    /* eax=result trace */

    /* Round to nearest, with halfway cases going to the even grid index.
     * Save and restore the caller's control word rather than relying on
     * its rounding mode or changing later floating-point calculations.
     */
    EMIT(0xd9, 0x7d, 0xdc,                  /* fnstcw [ebp-36] */
         0x0f, 0xb7, 0x55, 0xdc,            /* movzx edx,word [ebp-36] */
         0x81, 0xe2, B4(0xf3ffu),           /* clear rounding mode */
         0x66, 0x89, 0x55, 0xde,            /* mov [ebp-34],dx */
         0xd9, 0x6d, 0xde);                 /* fldcw [ebp-34] */
    const uint32_t spacing[] = { RTS_GRID_SIZE_X, RTS_GRID_SIZE_Y };
    for (unsigned axis = 0; axis < ARRAY_SIZE(spacing); ++axis) {
        EMIT(0xd9, 0x40, 12 + axis * 4);    /* fld trace.endpos[axis] */
        EMIT(0x68, B4(spacing[axis]),        /* push integer spacing */
             0xda, 0x34, 0x24,              /* fidiv dword [esp] */
             0xd9, 0xfc,                    /* frndint */
             0xda, 0x0c, 0x24,              /* fimul dword [esp] */
             0x83, 0xc4, 4);
        EMIT(0xd9, 0x55, 0xec + axis * 4,   /* fst start[axis] */
             0xd9, 0x5d, 0xe0 + axis * 4);  /* fstp end[axis] */
    }
    EMIT(0xd9, 0x6d, 0xdc);                 /* restore control word */
    EMIT(0x8d, 0x4d, 0xec, 0x8d, 0x55, 0xe0); /* private start/end */
    trace_call(payload, rva);
    EMIT(0xc9, 0xc3);                       /* leave; ret */

    uint32_t interpolation = (uint32_t)payload->size;
    EMIT(0x50, 0xe8, 0, 0, 0, 0);           /* save eax; call next */
    uint32_t anchor = rva + (uint32_t)payload->size;
    EMIT(0x58, 0x8b, 0x80, B4(BUILD_TYPE - anchor)); /* eax=build type */
    /* Trip mines (5) and strongholds (7) use their native free placement
     * and map attachment behavior, including the original smoothing.
     */
    EMIT(0x83, 0xf8, 5, 0x74, 9,            /* trip mine: original fraction */
         0x83, 0xf8, 7);
    EMIT(0x74, 4,                           /* je original fraction */
         0xd9, 0xe8,                       /* fld1: no interpolation */
         0xeb, 4,
         0xd9, 0x44, 0x24, 0x7c,            /* fld [esp+0x74+8] */
         0xd9, 0xc0, 0x58, 0xc3);           /* fld st(0); pop eax; ret */

    uint32_t rotation = (uint32_t)payload->size;
    EMIT(0x50, 0x51, 0xe8, 0, 0, 0, 0);     /* save eax/ecx; call next */
    anchor = rva + (uint32_t)payload->size;
    EMIT(0x59, 0x8b, 0x81, B4(BUILD_TYPE - anchor)); /* ecx=anchor; eax=type */
    /* Build types 1 and 2 are the machine-gun and anti-tank turrets.
     * Snap the absolute world yaw, including the camera's yaw contribution,
     * rather than rounding mouse deltas that may never reach a full step.
     */
    EMIT(0x48, 0x83, 0xf8, 1);              /* dec eax; cmp eax,1 */
    uint32_t other_structure = EMIT(0x0f, 0x87, 0, 0, 0, 0);
    EMIT(0x83, 0xec, 4,
         0xd9, 0x3c, 0x24,                 /* fnstcw [esp] */
         0x0f, 0xb7, 0x04, 0x24,            /* movzx eax,word [esp] */
         0x25, B4(0xf3ffu),                 /* round to nearest */
         0x66, 0x89, 0x44, 0x24, 2,
         0xd9, 0x6c, 0x24, 2);             /* fldcw [esp+2] */
    EMIT(0xd9, 0x81, B4(BUILD_YAW - anchor)); /* fld placement yaw */
    EMIT(0x68, B4(RTS_TURRET_ANGLE_STEP),
         0xda, 0x34, 0x24,                  /* fidiv dword [esp] */
         0xd9, 0xfc,                        /* frndint */
         0xda, 0x0c, 0x24,                  /* fimul dword [esp] */
         0x83, 0xc4, 4);
    EMIT(0xd9, 0x99, B4(BUILD_YAW - anchor)); /* fstp placement yaw */
    EMIT(0xd9, 0x2c, 0x24, 0x83, 0xc4, 4); /* restore control word */
    uint32_t rotation_done = (uint32_t)payload->size;
    put32(payload->data + other_structure + 2,
          rotation_done - (other_structure + 6u));
    EMIT(0x59, 0x58,                        /* restore ecx/eax */
         0x5f, 0x5e, 0x5b, 0x8b, 0xe5);     /* original epilogue */
    emit_jump(payload, rva, 0x737e9u);

    /* image_patch builds the payload before applying guarded code rules,
     * so their relative call targets can use this section's actual RVA.
     */
    const uint32_t targets[] = { trace, interpolation, rotation };
    for (size_t i = 0; i < ARRAY_SIZE(patch_rts_grid_hooks); ++i) {
        code_rule_t *hook = &patch_rts_grid_hooks[i];
        put32(hook->replacement + 1, rva + targets[i] - (hook->rva + 5u));
    }
    return true;
}

static const payload_feature_t rts_grid = {
    CGAME, "RTS building placement grid", build_rts_grid,
};

/* The payload and its hooks must be enabled together. */
REGISTER_CODE_RULES(patch_rts_grid_hooks)
REGISTER_PAYLOAD_FEATURE(rts_grid)
