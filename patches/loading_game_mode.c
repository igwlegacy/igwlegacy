#include "patches.h"

#include <stdio.h>

/* The UI's loading screen (uix86 RVA 0xa5c0 onwards) lists the level, then
 * "Maximum Players:" with the serverinfo's sv_humanClients and, while the
 * local g_gametype CVAR is 3 (coop), "Difficulty:" with sv_difficulty's
 * name.
 *
 * The second line now reads "Mode:" with the mode the server runs,
 * taken from its serverinfo: g_gametype 0 is Deathmatch, 4 Survival and
 * 5 Teamkill; 3 is Resistance when g_legacygametype is "1" and Vanilla
 * otherwise, also when the server does not know g_legacygametype. The
 * difficulty line only shows for Vanilla, and it follows the server's
 * gametype instead of the local CVAR, which on a client joining a remote
 * server could be anything.
 * With the difficulty line gone for Resistance, game_mode_resistance.c's
 * hook that names the mode in that line (RVA 0xa7bd) is no longer reached
 * on this screen.
 *
 * Four changes do it, none of them over relocated bytes:
 *  - "#levelload-max" at RVA 0x600a4, whose only reference is the label
 *    push at RVA 0xa777, becomes the untranslated literal "Mode:"; no
 *    language file has a key reading that.
 *  - The label drawer at RVA 0x5f90 formats "%s %s" from the label buffer
 *    at its [esp+0x1418] and the value. A label starting with '#' is looked
 *    up and copied there, but any other label is copied to [esp+0x418]
 *    instead, which leaves the label empty and drew " Resistance". That
 *    copy's destination becomes [esp+0x1418] as well. Every other caller
 *    of the drawer passes a '#' key, so only this label takes the path.
 *  - The Info_ValueForKey(serverinfo, "sv_humanClients") call at RVA
 *    0xa75f, whose result is the line's value, calls mode_name() instead,
 *    which has the same cdecl arguments and returns the mode's name. It
 *    also records whether the mode is Vanilla.
 *  - The Cvar_VariableValue("g_gametype") call at RVA 0xa792, which the
 *    screen compares with 3.0 to decide on the difficulty line, calls
 *    vanilla_gametype() instead, which returns 3.0 in st(0) for Vanilla
 *    and 0.0 otherwise.
 */

/* The UI's Info_ValueForKey(info, key), cdecl. */
#define UI_INFO_VALUE 0x4410u

static code_rule_t patch_loading_game_mode_hooks[] = {
    { UI, true, "loading game mode: mode name", 0xa75f, 5,
      { 0xe8, B4(UI_INFO_VALUE - 0xa764u) }, { 0xe8 } },
    { UI, true, "loading game mode: difficulty gate", 0xa792, 5,
      { 0xe8, B4(0x3a0c0u - 0xa797u) }, { 0xe8 } },
};

static const code_rule_t patch_loading_game_mode_label[] = {
    { UI, true, "loading game mode: label", 0x600a4, 15,
      "#levelload-max", "Mode:" },
    /* lea eax,[esp+0x41c] -> [esp+0x141c]: the destination of the drawer's
     * copy of a label not starting with '#', see above.
     */
    { UI, true, "loading game mode: literal label buffer", 0x6023, 7,
      { 0x8d, 0x84, 0x24, 0x1c, 0x04, 0x00, 0x00 },
      { 0x8d, 0x84, 0x24, 0x1c, 0x14, 0x00, 0x00 } },
};

/* A short forward branch emitted by emit_skip_if() lands here, failing the
 * build if the distance does not fit its rel8.
 */
static void land(payload_t *payload, uint32_t branch)
{
    if (payload->size - (branch + 2u) > 127u) {
        fputs("LOADING GAME MODE payload: short branch out of range\n", stderr);
        payload->failed = true;
    }
    emit_land(payload, branch);
}

/* op reg,[ebx+disp32], ebx holding the runtime address of the image RVA
 * base, so that the operand addresses the image RVA target.
 */
static void ebx_op(payload_t *payload, unsigned char opcode, unsigned reg,
                   uint32_t base, uint32_t target)
{
    EMIT(opcode, (unsigned char)(0x83 | reg << 3), B4(target - base));
}

static bool build_ui(payload_t *payload, uint32_t rva, uint32_t next, uint32_t *entry)
{
    if (!payload_reserve(payload, 256u, "LOADING GAME MODE"))
        return false;
    /* No syscalls to handle; the entry only passes them on. */
    *entry = emit_entry(payload, next);

    uint32_t gametype_key = rva + emit_string(payload, "g_gametype");
    uint32_t legacy_key = rva + emit_string(payload, "g_legacygametype");
    uint32_t vanilla = rva + emit_string(payload, "Vanilla");
    uint32_t resistance = rva + emit_string(payload, "Resistance");
    uint32_t deathmatch = rva + emit_string(payload, "Deathmatch");
    uint32_t survival = rva + emit_string(payload, "Survival");
    uint32_t teamkill = rva + emit_string(payload, "Teamkill");
    uint32_t unknown = rva + emit_string(payload, "Unknown");
    /* 1 while the last loading screen drawn showed Vanilla, else 0. The
     * injected section is writable.
     */
    uint32_t is_vanilla = rva + EMIT(0);

    /* mode_name(info, key): the name of the serverinfo's game mode. */
    uint32_t mode_name = EMIT(0x53, 0x56,               /* push ebx; push esi */
                              0x8b, 0x74, 0x24, 0x0c);  /* mov esi,[esp+0xc] */
    uint32_t base = emit_anchor(payload, rva, EBX);
    EMIT(0xc6, 0x83, B4(is_vanilla - base), 0);         /* mov byte [flag],0 */
    ebx_op(payload, 0x8d, EAX, base, gametype_key);     /* lea eax,[key] */
    EMIT(0x50, 0x56);                                   /* push eax; push esi */
    emit_call(payload, rva, UI_INFO_VALUE);
    EMIT(0x83, 0xc4, 0x08,                              /* add esp,8 */
         0x0f, 0xb7, 0x08);                             /* movzx ecx,word [eax] */

    /* The single-digit gametypes other than coop map straight to a name;
     * a value of any other length or digit is unknown.
     */
    const struct {
        char digit;
        uint32_t *name;
    } names[] = {
        { '0', &deathmatch }, { '4', &survival }, { '5', &teamkill },
    };
    uint32_t done[ARRAY_SIZE(names) + 2];
    size_t done_count = 0;
    for (size_t i = 0; i < ARRAY_SIZE(names); ++i) {
        ebx_op(payload, 0x8d, EDX, base, *names[i].name);   /* lea edx,[name] */
        EMIT(0x83, 0xf9, (unsigned char)names[i].digit);    /* cmp ecx,"<digit>" */
        done[done_count++] = emit_skip_if(payload, 0x74);   /* je done */
    }
    ebx_op(payload, 0x8d, EDX, base, unknown);
    EMIT(0x83, 0xf9, '3');                                  /* cmp ecx,"3" */
    done[done_count++] = emit_skip_if(payload, 0x75);       /* jne done */

    /* Coop: Resistance with g_legacygametype "1", else Vanilla. */
    ebx_op(payload, 0x8d, EAX, base, legacy_key);
    EMIT(0x50, 0x56);
    emit_call(payload, rva, UI_INFO_VALUE);
    EMIT(0x83, 0xc4, 0x08);
    ebx_op(payload, 0x8d, EDX, base, resistance);
    EMIT(0x66, 0x81, 0x38, '1', 0);                         /* cmp word [eax],"1" */
    done[done_count++] = emit_skip_if(payload, 0x74);       /* je done */
    ebx_op(payload, 0x8d, EDX, base, vanilla);
    EMIT(0xc6, 0x83, B4(is_vanilla - base), 1);             /* mov byte [flag],1 */

    for (size_t i = 0; i < done_count; ++i)
        land(payload, done[i]);
    EMIT(0x8b, 0xc2,                                        /* mov eax,edx */
         0x5e, 0x5b, 0xc3);                                 /* pop esi; pop ebx; ret */

    /* vanilla_gametype(name): 3.0 for Vanilla, 0.0 otherwise, in st(0)
     * like Cvar_VariableValue. Only clobbers the caller-saved eax and ecx.
     */
    uint32_t vanilla_gametype = (uint32_t)payload->size;
    uint32_t flag_base = emit_anchor(payload, rva, ECX);
    EMIT(0x0f, 0xb6, 0x81, B4(is_vanilla - flag_base),     /* movzx eax,byte [flag] */
         0x8d, 0x04, 0x40,                                  /* lea eax,[eax+eax*2] */
         0x50,                                              /* push eax */
         0xdb, 0x04, 0x24,                                  /* fild dword [esp] */
         0x58, 0xc3);                                       /* pop eax; ret */

    const uint32_t targets[] = { mode_name, vanilla_gametype };
    _Static_assert(ARRAY_SIZE(targets) == ARRAY_SIZE(patch_loading_game_mode_hooks),
                   "every hook needs a helper");
    resolve_hooks(patch_loading_game_mode_hooks, ARRAY_SIZE(targets), targets, rva);
    return true;
}

static const payload_feature_t loading_game_mode_ui = { UI, "LOADING GAME MODE", build_ui };

/* The payload, its hooks and the label must be enabled together. */
REGISTER_CODE_RULES(patch_loading_game_mode_hooks)
REGISTER_CODE_RULES(patch_loading_game_mode_label)
REGISTER_PAYLOAD_FEATURE(loading_game_mode_ui)
