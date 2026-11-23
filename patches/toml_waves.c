#include "patches.h"
#include "game_blob.h"

/* TOML wave scripts: scripts/waves/<map>-<level>.toml takes precedence over
 * the .xml of the same name. The parser and the sequential wave scheduler
 * are C in gamex86's blob, around the vendored tomlc17, see
 * toml_waves/waves.c for the format's semantics.
 *
 * Both hooks are relative jumps or calls into the payload section, whose
 * bytes no base relocation covers, so the DLL still rebases correctly.
 */

static code_rule_t patch_toml_waves_hooks[] = {
    /* G_LoadWaveScript: push ebp; mov ebp,esp; and esp,-8, with the file
     * name in edi.
     */
    { GAME, true, "toml waves: load scripts/waves/<map>-<level>.toml", 0x64730, 6,
      { 0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8 }, { 0xe9, 0, 0, 0, 0, 0x90 } },
    /* G_RunWaves: call G_SpawnRoamers, after the debug and round end
     * checks and ahead of the XML scheduler.
     */
    { GAME, true, "toml waves: sequential waves", 0x6d7c3, 5,
      { 0xe8, B4(0x6d340u - 0x6d7c8u) }, { 0xe8, 0, 0, 0, 0 } },
};

static bool build_game(payload_t *payload, uint32_t rva, uint32_t next, uint32_t *entry)
{
    /* The two short stubs below; payload_build() appends the blob. */
    if (!payload_reserve(payload, 128, "TOML waves"))
        return false;
    *entry = emit_entry(payload, next);

    /* eax = toml_waves_load(edi). -1 replays the replaced prologue and
     * continues into the XML loader; otherwise return its result.
     */
    uint32_t load = (uint32_t)payload->size;
    EMIT(0x57);                             /* push edi */
    emit_blob_call(payload, &game_blob, GAME_BLOB_TOML_WAVES_LOAD);
    EMIT(0x83, 0xc4, 0x04,                  /* add esp,4 */
         0x83, 0xf8, 0xff,                  /* cmp eax,-1 */
         0x74, 0x01,                        /* je xml */
         0xc3,                              /* ret */
         0x55, 0x8b, 0xec, 0x83, 0xe4, 0xf8); /* xml: the prologue */
    emit_jump(payload, rva, 0x64736u);

    /* Spawn the roamers as before; when toml_waves_run() has handled the
     * waves, return from G_RunWaves itself: drop this call's return address
     * and undo its push ecx.
     */
    uint32_t run = (uint32_t)payload->size;
    emit_call(payload, rva, 0x6d340u);      /* G_SpawnRoamers */
    emit_blob_call(payload, &game_blob, GAME_BLOB_TOML_WAVES_RUN);
    EMIT(0x85, 0xc0,                        /* test eax,eax */
         0x75, 0x01,                        /* jne handled */
         0xc3,                              /* ret: the XML scheduler */
         0x83, 0xc4, 0x04,                  /* handled: add esp,4 */
         0x59,                              /* pop ecx */
         0xc3);                             /* ret */

    const uint32_t targets[] = { load, run };
    resolve_hooks(patch_toml_waves_hooks, ARRAY_SIZE(targets), targets, rva);
    return true;
}

static const payload_feature_t toml_waves_game = {
    GAME, "TOML wave scripts", build_game,
};

/* The payload and its hooks must be enabled together. */
REGISTER_CODE_RULES(patch_toml_waves_hooks)
REGISTER_PAYLOAD_FEATURE(toml_waves_game)
