#include "patches.h"

/* Make the client's movement prediction collide with the deathhand, as
 * the server's movement does.
 *
 * The deathhand (G_BuildDeathhand, game 0x44b00) is an eType 15
 * "structure" with CONTENTS_SOLID, a (-80 -80 0) to (80 80 190) box and
 * s.otherEntityNum2 set to 1. The server moves players with syscall 591,
 * whose eType filter (SV_IGWPlayerTypesCollide, igwarlord-server
 * 0x40e60) lets players pass through eTypes 15 to 19, except an eType 15
 * whose otherEntityNum2 is 1: players are blocked by the deathhand and
 * walk through every other structure.
 *
 * The client predicts with CG_Trace (cgame 0x2df50, cg_pmove.trace),
 * whose CG_ClipMoveToEntities (cgame 0x2cef0) skips all of eTypes 15 to
 * 19 without looking at otherEntityNum2:
 *
 *   0x2cf71  mov eax,[esi+4]     ; cent->currentState.eType
 *   0x2cf74  cmp eax,0xf
 *   0x2cf77  jl  0x2cf82         ; clip against it
 *   0x2cf79  cmp eax,0x13
 *   0x2cf7c  jle 0x2d2e5         ; skip it
 *
 * So a client walks into the deathhand and the server pulls it back, a
 * prediction miss that grows with ping. The engine sends r.contents and
 * r.mins/r.maxs in the entity state, so the client already has the
 * deathhand's solid box, and otherEntityNum2 is a regular entityState
 * field; only this filter keeps the client from using them.
 *
 * Replace the eType tests with a call to a helper that leaves CF set
 * exactly for the entities the server's player filter skips, and turn
 * the jle into a jb with the same target. ESI is the centity, whose
 * currentState starts at +0 (otherEntityNum2 at +0xb4). EAX held the
 * eType and is dead afterwards, as is ECX: every path from 0x2cf82 and
 * from the skip target writes ECX before reading it. EDX, the loop
 * index the skip target increments, is left alone.
 *
 * Padding runs are at most 15 bytes, so the helper spans two
 * unreachable int3 runs that each follow a ret, chained with jmp rel32.
 * It uses no absolute addresses and needs no new relocations.
 *
 * Only the client's cgamex86.dll needs this patch; the server is
 * unchanged.
 */
enum {
    CLIP_HOOK = 0x2cf71u,       /* mov eax,[esi+4] */
    CLIP_SKIP_JCC = 0x2cf7cu,   /* jle 0x2d2e5 */
    HELPER_A = 0x2de92u,        /* 14 int3 after a ret at 0x2de91 */
    HELPER_B = 0x2df42u,        /* 14 int3 after a ret at 0x2df41 */
};

#define CC14 { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, \
               0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }

static const code_rule_t patch_deathhand_prediction[] = {
    /* The 6-byte nop pads the replaced cmp/jl/cmp up to the jcc. */
    { CGAME, true, "deathhand prediction: call eType filter", CLIP_HOOK, 11,
      { 0x8b, 0x46, 0x04,                   /* mov eax,[esi+4] */
        0x83, 0xf8, 0x0f,                   /* cmp eax,0xf */
        0x7c, 0x09,                         /* jl 0x2cf82 */
        0x83, 0xf8, 0x13 },                 /* cmp eax,0x13 */
      { 0xe8, B4(HELPER_A - (CLIP_HOOK + 5u)),  /* call helper */
        0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00 } },
    /* Same rel32 to the skip path, now taken when the helper sets CF. */
    { CGAME, true, "deathhand prediction: skip on carry", CLIP_SKIP_JCC, 2,
      { 0x0f, 0x8e },                       /* jle rel32 */
      { 0x0f, 0x82 } },                     /* jb rel32 */

    /* EAX = eType - 15, so eTypes 15 to 19 become 0 to 4 unsigned. */
    { CGAME, true, "deathhand prediction: eType filter, part 1", HELPER_A, 14,
      CC14,
      { 0x8b, 0x46, 0x04,                   /* mov eax,[esi+4] */
        0x83, 0xe8, 0x0f,                   /* sub eax,0xf */
        0x83, 0xf8, 0x04,                   /* cmp eax,4 */
        0xe9, B4(HELPER_B - (HELPER_A + 14u)) } },

    /* Outside 15 to 19, ja returns with CF clear from the cmp: clip.
     * Inside, (eType - 15) | (otherEntityNum2 - 1) is zero only for the
     * deathhand; neg sets CF for any nonzero value: skip.
     */
    { CGAME, true, "deathhand prediction: eType filter, part 2", HELPER_B, 14,
      CC14,
      { 0x77, 0x0b,                         /* ja ret */
        0x8b, 0x8e, B4(0xb4u),              /* mov ecx,[esi+0xb4] */
        0x49,                               /* dec ecx */
        0x09, 0xc8,                         /* or eax,ecx */
        0xf7, 0xd8,                         /* neg eax */
        0xc3 } },                           /* ret */
};

/* The hook, the jcc and both helper parts must be enabled together. */
REGISTER_CODE_RULES(patch_deathhand_prediction)
