#include "patches.h"
#include "game_blob.h"

/* Wave scripts beyond the XML: scripts/waves/<map>-<level>.lua takes
 * precedence over the .toml of the same name, which takes precedence over
 * the .xml. The loaders and schedulers are C in gamex86's blob, see
 * wave_scripts/scripts.c for how the formats take turns,
 * wave_scripts/lua.c for the Lua scripts and wave_scripts/toml.c for the
 * TOML's semantics. A Lua script spawns everything itself, so with one
 * the round's lose waves stay away too.
 *
 * All hooks are relative jumps or calls into the payload section, whose
 * bytes no base relocation covers, so the DLL still rebases correctly.
 */

static code_rule_t patch_wave_scripts_hooks[] = {
    /* G_LoadWaveScript: push ebp; mov ebp,esp; and esp,-8, with the file
     * name in edi.
     */
    { GAME, true, "wave scripts: load scripts/waves/<map>-<level>.toml", 0x64730, 6,
      { 0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8 }, { 0xe9, 0, 0, 0, 0, 0x90 } },
    /* G_RunWaves: call G_SpawnRoamers, after the debug and round end
     * checks and ahead of the XML scheduler.
     */
    { GAME, true, "wave scripts: run the frame's waves", 0x6d7c3, 5,
      { 0xe8, B4(0x6d340u - 0x6d7c8u) }, { 0xe8, 0, 0, 0, 0 } },
    /* G_CheckRound: call G_SendLoseWaves, once a lost round is over. */
    { GAME, true, "wave scripts: no lose waves with a Lua script", 0x41d0d, 5,
      { 0xe8, B4(0x5d500u - 0x41d12u) }, { 0xe8, 0, 0, 0, 0 } },
};

static bool build_game(payload_t *payload, uint32_t rva, uint32_t next, uint32_t *entry)
{
    /* The three short stubs below; payload_build() appends the blob. */
    if (!payload_reserve(payload, 128, "wave scripts"))
        return false;
    *entry = emit_entry(payload, next);

    /* eax = wave_scripts_load(edi). -1 replays the replaced prologue and
     * continues into the XML loader; otherwise return its result.
     */
    uint32_t load = (uint32_t)payload->size;
    EMIT(0x57);                             /* push edi */
    emit_blob_call(payload, &game_blob, GAME_BLOB_WAVE_SCRIPTS_LOAD);
    EMIT(0x83, 0xc4, 0x04,                  /* add esp,4 */
         0x83, 0xf8, 0xff,                  /* cmp eax,-1 */
         0x74, 0x01,                        /* je xml */
         0xc3,                              /* ret */
         0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8); /* xml: the prologue */
    emit_jump(payload, rva, 0x64736u);

    /* When wave_scripts_run() has handled the frame, return from
     * G_RunWaves itself: drop this call's return address and undo its
     * push ecx. Otherwise spawn the roamers as before, returning into the
     * XML scheduler.
     */
    uint32_t run = (uint32_t)payload->size;
    emit_blob_call(payload, &game_blob, GAME_BLOB_WAVE_SCRIPTS_RUN);
    EMIT(0x85, 0xc0);                       /* test eax,eax */
    emit_jump_if(payload, rva, 0x84, 0x6d340u); /* je G_SpawnRoamers */
    EMIT(0x83, 0xc4, 0x04,                  /* add esp,4 */
         0x59,                              /* pop ecx */
         0xc3);                             /* ret */

    /* G_SendLoseWaves unless wave_scripts_lose_waves() leaves them out,
     * a tail jump that returns into G_CheckRound.
     */
    uint32_t lose = (uint32_t)payload->size;
    emit_blob_call(payload, &game_blob, GAME_BLOB_WAVE_SCRIPTS_LOSE_WAVES);
    EMIT(0x85, 0xc0);                       /* test eax,eax */
    emit_jump_if(payload, rva, 0x84, 0x5d500u); /* je G_SendLoseWaves */
    EMIT(0xc3);                             /* ret */

    const uint32_t targets[] = { load, run, lose };
    resolve_hooks(patch_wave_scripts_hooks, ARRAY_SIZE(targets), targets, rva);
    return true;
}

static const payload_feature_t wave_scripts_game = {
    GAME, "wave scripts", build_game,
};

/* The payload and its hooks must be enabled together. */
REGISTER_CODE_RULES(patch_wave_scripts_hooks)
REGISTER_PAYLOAD_FEATURE(wave_scripts_game)
