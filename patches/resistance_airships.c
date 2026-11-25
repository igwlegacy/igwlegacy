#include "patches.h"
#include "cgame_blob.h"

/* In RESISTANCE the sky fills with airships over time: besides retail's
 * three, another one joins every four minutes, up to twenty, at a random
 * place on the route all of them follow, and grows from tiny to the retail
 * size over a minute. The flight is C in cgamex86's blob, see
 * resistance_airships/airships.c; it tells RESISTANCE by the level that
 * game_mode_resistance.c makes CG_ParseServerinfo store.
 *
 * Both hooks are relative branches into the payload section, whose bytes
 * no base relocation covers, so the DLL still rebases correctly.
 */

#define CGAME_AIRSHIPS_LOOP 0x17e30u
#define CGAME_ADD_REF_ENTITY 0x32eb0u  /* trap_R_AddRefEntityToScene, eax */

static code_rule_t patch_resistance_airships_hooks[] = {
    /* CG_AddPacketEntities' tail jump into the retail loop over the
     * airships, past its pops; that loop's own entry is relocated bytes.
     */
    { CGAME, true, "resistance airships: fly them", 0x21e04, 5,
      { 0xe9, B4(CGAME_AIRSHIPS_LOOP - 0x21e09u) }, { 0xe9 } },
    /* The airship update's call of trap_R_AddRefEntityToScene with the
     * refEntity_t in eax, after it scaled the axes to the sky portal.
     */
    { CGAME, true, "resistance airships: grow them", 0x17e0e, 5,
      { 0xe8, B4(CGAME_ADD_REF_ENTITY - 0x17e13u) }, { 0xe8 } },
};

static bool build_cgame(payload_t *payload, uint32_t rva, uint32_t next, uint32_t *entry)
{
    /* The two short stubs below; payload_build() appends the blob. */
    if (!payload_reserve(payload, 64, "resistance airships"))
        return false;
    *entry = emit_entry(payload, next);

    /* airships_frame(), keeping every register, as nothing tells what
     * CG_AddPacketEntities' callers expect of the retail loop.
     */
    uint32_t frame = EMIT(0x60);            /* pushad */
    emit_blob_call(payload, &cgame_blob, CGAME_BLOB_AIRSHIPS_FRAME);
    EMIT(0x61,                              /* popad */
         0xc3);                             /* ret */

    /* eax = airships_scale(eax), then the trap, which returns into the
     * update.
     */
    uint32_t grow = EMIT(0x50);             /* push eax */
    emit_blob_call(payload, &cgame_blob, CGAME_BLOB_AIRSHIPS_SCALE);
    EMIT(0x83, 0xc4, 0x04);                 /* add esp,4 */
    emit_jump(payload, rva, CGAME_ADD_REF_ENTITY);

    const uint32_t targets[] = { frame, grow };
    _Static_assert(ARRAY_SIZE(targets) == ARRAY_SIZE(patch_resistance_airships_hooks),
                   "every hook needs a stub");
    resolve_hooks(patch_resistance_airships_hooks, ARRAY_SIZE(targets), targets, rva);
    return true;
}

static const payload_feature_t resistance_airships_cgame = {
    CGAME, "resistance airships", build_cgame,
};

/* The payload and its hooks must be enabled together. */
REGISTER_CODE_RULES(patch_resistance_airships_hooks)
REGISTER_PAYLOAD_FEATURE(resistance_airships_cgame)
