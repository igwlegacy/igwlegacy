#include "patches.h"

/* Keep an engineer's deathhand goal index inside igw_deathhandGoals.
 *
 * An engineer's routeIndex (igwNPC_t +0x378) indexes different tables
 * depending on its state. While a deathhand stands, G_EngineerDefendArea
 * (game 0x50050) roams through G_NPCRoam (0x4f550), whose
 * G_NPCRandomCamp (0x4ed80) stores rand() % igw_numConfedCamps there:
 * a confederate camp index, up to 165 on palace3. Once the deathhand is
 * gone, G_EngineerBuildDeathhand (0x50170) reuses the field as an index
 * into igw_deathhandGoals (10 bot_goal_t of 64 bytes at 0x13115a0),
 * picking a fresh one only when it is exactly -1:
 *
 *   0x501a9  cmp dword ptr [ebp+0x378],-1
 *   0x501b0  jne 0x501c4                  ; keep routeIndex
 *   0x501b2  call rand
 *   0x501b7  cdq
 *   0x501b8  idiv dword ptr [igw_numDeathhandGoals]
 *   0x501be  mov [ebp+0x378],edx
 *   0x501c4  mov esi,[ebp+0x378]
 *   0x501ca  shl esi,6
 *   0x501cd  add esi,igw_deathhandGoals   ; goal, unchecked
 *
 * A stale camp index reads past the array into igw_npcs (0x1311820),
 * so the engineer moves to another NPC's player state as its goal. A
 * float there taken as goal->areanum crashes the engine's
 * AAS_AreaDoNotEnter; within 100 units G_BuildDeathhand also writes
 * the dropped origin into that NPC.
 *
 * Replace the -1 test with a call to a helper that compares routeIndex
 * unsigned against igw_numDeathhandGoals, and turn the jne into a jb with
 * the same target: an index inside the table is kept, -1 and any stale
 * index draw a new one through retail's rand() path. The function has
 * already returned for a count of 0 (0x50173). EAX, ECX and EDX are dead
 * at the hook: EAX holds G_NPCDebugInfo's unused result and both paths
 * from here write EAX, ECX and EDX before reading them. ret keeps the
 * helper's flags. The replaced bytes carry no base relocation, and the
 * helper reaches the count through call/pop, so no relocation is added.
 */
enum {
    BUILD_DEATHHAND_HOOK = 0x501a9u,
    NUM_DEATHHAND_GOALS = 0x11c890u,  /* igw_numDeathhandGoals */
    NPC_ROUTE_INDEX = 0x378u,
};

static code_rule_t patch_engineer_deathhand_goal_hooks[] = {
    /* call helper; 2-byte nop; jb 0x501c4 (the original jne's rel8). */
    { GAME, true, "engineer deathhand goal: bounds check", BUILD_DEATHHAND_HOOK, 9,
      { 0x83, 0xbd, B4(NPC_ROUTE_INDEX), 0xff, 0x75, 0x12 },
      { 0xe8, 0, 0, 0, 0, 0x66, 0x90, 0x72, 0x12 } },
};

static bool build_game(payload_t *payload, uint32_t rva, uint32_t next, uint32_t *entry)
{
    /* The helper needs 19 bytes, the dispatcher entry 5. */
    if (!payload_reserve(payload, 32, "engineer deathhand goal"))
        return false;
    *entry = emit_entry(payload, next);

    /* With the NPC in EBP: CF = (unsigned)routeIndex < count. */
    uint32_t check = (uint32_t)payload->size;
    EMIT(0x8b, 0x85, B4(NPC_ROUTE_INDEX));  /* mov eax,[ebp+routeIndex] */
    uint32_t base = emit_anchor(payload, rva, ECX);
    EMIT(0x3b, 0x81, B4(NUM_DEATHHAND_GOALS - base)); /* cmp eax,[count] */
    EMIT(0xc3);                             /* ret */

    const uint32_t targets[] = { check };
    _Static_assert(ARRAY_SIZE(targets) == ARRAY_SIZE(patch_engineer_deathhand_goal_hooks),
                   "every hook needs a helper");
    resolve_hooks(patch_engineer_deathhand_goal_hooks, ARRAY_SIZE(targets), targets, rva);
    return true;
}

static const payload_feature_t engineer_deathhand_goal_game = {
    GAME, "engineer deathhand goal", build_game,
};

/* The payload and its hook must be enabled together. */
REGISTER_CODE_RULES(patch_engineer_deathhand_goal_hooks)
REGISTER_PAYLOAD_FEATURE(engineer_deathhand_goal_game)
