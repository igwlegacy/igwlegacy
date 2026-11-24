#include "patches.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* RESISTANCE is a second coop game mode. The host-game menu's "Game mode:"
 * dropdown lists VANILLA, RESISTANCE and DEATHMATCH; RESISTANCE still
 * starts g_gametype 3, but also sets the new CVAR g_legacygametype to 1,
 * which VANILLA and DEATHMATCH set to 0. g_legacygametype is
 * CVAR_SERVERINFO | CVAR_LATCH, so it takes effect on the next map, like
 * g_gametype, and dedicated servers set it in their configs.
 *
 * Its fixed rules are friendly fire off, no difficulty multiplier and the
 * god difficulty. The menu sets and greys out those three controls, as
 * DEATHMATCH greys them, and gives back the previous values when another
 * mode is chosen or the menu reopens. The game enforces them every frame
 * and refuses difficulty votes, also on dedicated servers.
 *
 * The server browser, the loading screens and the scoreboard name the
 * mode instead of the difficulty. The browser cannot learn it from the
 * gametype, which its "Regular" filter requires to be 3, so the engines'
 * info responses report difficulty 6 for it instead.
 *
 * Like TITAN's, every hook is a relative jump or call into the injected
 * section (or, in the engines, into .text's file-backed slack), whose code
 * addresses its data with call/pop; no hook overwrites relocated bytes.
 */

/* The difficulty the engines' info responses report for RESISTANCE. */
#define RESISTANCE_LEVEL 6

/* The UI's code, strings and data take about 900 bytes, the game's 600. */
#define RESERVE 1536u

/* -- uix86.dll --------------------------------------------------------- */

#define UI_MAPPER 0x11c90u      /* dropdown index -> g_gametype, cdecl */
#define UI_MENU_INIT 0x13b70u   /* host-game menu initializer */
#define UI_INFO_VALUE 0x4410u   /* Info_ValueForKey(info, key) */
#define UI_TRANSLATE 0x3ab90u   /* the loading screen's text lookup */
#define UI_CVAR_REGISTER 0x3a060u
#define UI_CVAR_SET 0x3a0a0u
#define UI_CVAR_VALUE 0x3a0c0u  /* float in st(0) */
#define UI_CVAR_SET_VALUE 0x3a100u
#define UI_FTOL 0x404d0u        /* st(0) -> eax */

/* The menu's control values, which its initializer reads from the CVARs
 * whose names follow, and which each control's event writes back.
 */
#define UI_DIFFICULTY 0x92afcu
#define UI_FRIENDLY_FIRE 0x92f78u
#define UI_DIFF_MULTI 0x92fd4u
#define UI_NAME_DIFFICULTY 0x6007cu   /* "sv_difficulty" */
#define UI_NAME_FRIENDLY_FIRE 0x61660u /* "g_friendlyfire" */
#define UI_NAME_DIFF_MULTI 0x61670u   /* "sv_diffmulti" */

/* The browser's difficulty column, before TITAN's or the retail level
 * chain: mov eax,[reg+d8]; add esp,0x10 after the previous column's
 * Com_sprintf. At +6 the chain tests the level; at +15, past the push of
 * "Easy" or TITAN's push of the indexed name, the column's sprintf
 * arguments continue, the same for every level.
 */
#define UI_BROWSER(at, modrm, disp)                                         \
    { UI, true, "resistance: server browser name", (at), 6,                 \
      { 0x8b, (modrm), (disp), 0x83, 0xc4, 0x10 },                          \
      { 0xe9, 0, 0, 0, 0, 0x90 } }

/* Hook replacements receive their rel32 or address once the payload has a
 * known RVA.
 */
static code_rule_t patch_resistance_ui_hooks[] = {
    /* The three calls of the index mapper: the dropdown's change event,
     * the start of a server and the update that greys out controls.
     */
    { UI, true, "resistance: game mode change", 0x1306a, 5,
      { 0xe8, B4(UI_MAPPER - 0x1306fu) }, { 0xe8 } },
    { UI, true, "resistance: start server", 0x12d27, 5,
      { 0xe8, B4(UI_MAPPER - 0x12d2cu) }, { 0xe8 } },
    { UI, true, "resistance: grey out controls", 0x1322b, 5,
      { 0xe8, B4(UI_MAPPER - 0x13230u) }, { 0xe8 } },
    /* The menu's only opening path, ahead of the initializer. */
    { UI, true, "resistance: menu opens", 0x15cb0, 5,
      { 0xe8, B4(UI_MENU_INIT - 0x15cb5u) }, { 0xe8 } },
    /* The loading screen's add esp,8; mov esi,eax after it looked up
     * sv_difficulty, ahead of its chain of level names.
     */
    { UI, true, "resistance: loading screen name", 0xa7bd, 5,
      { 0x83, 0xc4, 0x08, 0x8b, 0xf0 }, { 0xe9 } },
    UI_BROWSER(0x323cb, 0x47, 0x1e),
    UI_BROWSER(0x3257b, 0x47, 0x1e),
    UI_BROWSER(0x32738, 0x47, 0x08),
    UI_BROWSER(0x328c7, 0x46, 0xf8),
};
enum { UI_BROWSER_FIRST = 5 };

/* The dropdown's second name slot, DEATHMATCH's, points to RESISTANCE's
 * name in the payload; the slot keeps its relocation.
 */
static code_rule_t patch_resistance_ui_name[] = {
    { UI, true, "resistance: game mode second entry", 0x6d0bc, 4,
      { B4(0x10061334u) }, { 0 } },
};

static const code_rule_t patch_resistance_ui[] = {
    /* mov dword [count],2; the embedded list copies the count. */
    { UI, true, "resistance: game mode entries", 0x1577e, 4,
      { B4(2u) }, { B4(3u) } },
    /* The mapper turned index 1 into deathmatch (0) and the rest into
     * coop (3); now index 2 is deathmatch. The rest of its original code
     * stays in place, unreached.
     */
    { UI, true, "resistance: game mode mapping", UI_MAPPER, 15,
      { 0x8b, 0x44, 0x24, 0x04, 0x83, 0xe8, 0x00, 0x74,
        0x06, 0x83, 0xe8, 0x01, 0x75, 0x01, 0xc3 },
      { 0x83, 0x7c, 0x24, 0x04, 0x02,       /* cmp dword [esp+4],2 */
        0xb8, B4(3u),                       /* mov eax,3 */
        0x75, 0x02,                         /* jne ret */
        0x31, 0xc0,                         /* xor eax,eax */
        0xc3 } },
};

/* DEATHMATCH moves to the table's third slot, a zero dword without a
 * relocation that the count-based dropdown never read.
 */
static const address_rule_t patch_resistance_ui_addresses[] = {
    { UI, "resistance: game mode third entry", 0x6d0c0, 0, 0x10061334u },
};

/* -- gamex86.dll ------------------------------------------------------- */

#define GAME_SYSCALL 0xb58bcu       /* the engine's syscall pointer */
#define GAME_REGISTER_CVARS 0x24a60u
#define GAME_UPDATE_CVARS 0x24b10u
#define GAME_GAMETYPE 0x15b07ccu    /* g_gametype.integer */
#define GAME_ENTITIES 0x14b15a0u    /* g_entities */
#define G_CVAR_REGISTER 4
#define G_CVAR_UPDATE 5
#define G_CVAR_SET 6

static code_rule_t patch_resistance_game_hooks[] = {
    /* G_InitGame's call of G_RegisterCvars. */
    { GAME, true, "resistance: register g_legacygametype", 0x24bc0, 5,
      { 0xe8, B4(GAME_REGISTER_CVARS - 0x24bc5u) }, { 0xe8 } },
    /* G_RunFrame's call of G_UpdateCvars. */
    { GAME, true, "resistance: enforce rules", 0x265cd, 5,
      { 0xe8, B4(GAME_UPDATE_CVARS - 0x265d2u) }, { 0xe8 } },
    /* Cmd_CallVote_f's jne past its "difficulty" case. */
    { GAME, true, "resistance: refuse difficulty votes", 0x1f350, 6,
      { 0x0f, 0x85, B4(0x1f412u - 0x1f356u) }, { 0xe9, 0, 0, 0, 0, 0x90 } },
};

/* -- cgamex86.dll ------------------------------------------------------ */

#define CGAME_INFO_VALUE 0x3d9b0u   /* Info_ValueForKey, key in ebx, info in ecx */
#define CGAME_CONNECT_NAMES 0xcae80u

static code_rule_t patch_resistance_cgame_hooks[] = {
    /* Scoreboard: add esp,4; push edx; lea eax,[esp+0x34], edx being the
     * level's name from the table or TITAN's helper and ebp the
     * serverinfo.
     */
    { CGAME, true, "resistance: scoreboard name", 0x76320, 8,
      { 0x83, 0xc4, 0x04, 0x52, 0x8d, 0x44, 0x24, 0x34 },
      { 0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90 } },
    /* Connection screen: mov ebx,eax; add esp,0x14 after atoi of
     * sv_difficulty, ahead of the clamp, with esi the serverinfo.
     */
    { CGAME, true, "resistance: connection screen name", 0x9ad5, 5,
      { 0x8b, 0xd8, 0x83, 0xc4, 0x14 }, { 0xe9 } },
};

/* -- igwarlord.exe and igwarlord-server.exe ---------------------------- */

/* SVC_Info adds sv_difficulty to the info response from
 * mov eax,[edx+0x20]; add esp,0xc, edx being sv_difficulty's cvar_t. A
 * cave in .text's zero slack reports RESISTANCE_LEVEL instead while the
 * g_legacygametype CVAR is 1. Cvar_FindVar takes the name in edi and keeps
 * esi; ecx and edx are dead at the hook.
 */
static const struct {
    module_t module;
    uint32_t virtual_size;   /* .text's VirtualSize field */
    uint32_t text_end;       /* its value, the end of the code */
    uint32_t raw_size;       /* .text's SizeOfRawData */
    uint32_t hook;
    uint32_t cave;
    uint32_t find_var;       /* Cvar_FindVar */
} engine_sites[] = {
    { ENGINE, 0x1f8u, 0x1a8f1bu, 0x1a9000u, 0x3ecadu, 0x1a9f20u, 0x243f0u },
    { SERVER, 0x1f0u, 0x102d5bu, 0x102e00u, 0x3e18du, 0x103d60u, 0x238a0u },
};

static const unsigned char engine_cave[] = {
    0x8b, 0x42, 0x20,                       /* mov eax,[edx+0x20] */
    0x83, 0xc4, 0x0c,                       /* add esp,0xc */
    0x50, 0x57,                             /* push eax; push edi */
    0xe8, B4(17u),                          /* call over the name */
    'g', '_', 'l', 'e', 'g', 'a', 'c', 'y',
    'g', 'a', 'm', 'e', 't', 'y', 'p', 'e', 0,
    0x5f,                                   /* pop edi: the name */
    0xe8, 0, 0, 0, 0,                       /* call Cvar_FindVar */
    0x85, 0xc0, 0x74, 0x0e,                 /* test eax,eax; je out */
    0x83, 0x78, 0x20, 0x01, 0x75, 0x08,     /* cmp [eax+integer],1; jne out */
    0xc7, 0x44, 0x24, 0x04, B4((uint32_t)RESISTANCE_LEVEL), /* saved eax */
    0x5f, 0x58,                             /* out: pop edi; pop eax */
    0xe9, 0, 0, 0, 0,                       /* jmp back */
};
enum {
    CAVE_FIND_VAR = 31,
    CAVE_BACK = sizeof(engine_cave) - 5,
    CAVE_CHUNKS = (sizeof(engine_cave) + PATCH_MAX - 1) / PATCH_MAX,
    ENGINE_RULES = 2 + CAVE_CHUNKS,
};
_Static_assert(sizeof(engine_cave) == 61, "the cave's offsets need updating");

static code_rule_t patch_resistance_engine[ARRAY_SIZE(engine_sites) * ENGINE_RULES];

/* Each engine gets its VirtualSize extension, the hook and the cave split
 * into guarded patch-sized units.
 */
PATCH_CONSTRUCTOR(configure_resistance_engine)
{
    for (size_t i = 0; i < ARRAY_SIZE(engine_sites); ++i) {
        uint32_t cave = engine_sites[i].cave;
        if (cave < 0x1000u + engine_sites[i].text_end ||
            cave + sizeof(engine_cave) > 0x1000u + engine_sites[i].raw_size)
            abort();
        code_rule_t *rule = &patch_resistance_engine[i * ENGINE_RULES];

        /* .text starts at RVA 0x1000; map its whole raw size. */
        rule[0] = (code_rule_t){ engine_sites[i].module, true,
                                 "resistance: map info response cave",
                                 engine_sites[i].virtual_size, 4, { 0 }, { 0 } };
        put32(rule[0].expected, engine_sites[i].text_end);
        put32(rule[0].replacement, engine_sites[i].raw_size);

        rule[1] = (code_rule_t){ engine_sites[i].module, true,
                                 "resistance: info response difficulty",
                                 engine_sites[i].hook, 6,
                                 { 0x8b, 0x42, 0x20, 0x83, 0xc4, 0x0c },
                                 { 0xe9, 0, 0, 0, 0, 0x90 } };
        put32(rule[1].replacement + 1, cave - (engine_sites[i].hook + 5u));

        /* Guard the rel32 offsets against edits of the cave's bytes. */
        if (engine_cave[CAVE_FIND_VAR] != 0xe8 || engine_cave[CAVE_BACK] != 0xe9)
            abort();
        unsigned char code[sizeof(engine_cave)];
        memcpy(code, engine_cave, sizeof(code));
        put32(code + CAVE_FIND_VAR + 1,
              engine_sites[i].find_var - (cave + CAVE_FIND_VAR + 5u));
        put32(code + CAVE_BACK + 1, engine_sites[i].hook + 6u - (cave + CAVE_BACK + 5u));
        for (size_t j = 0; j < CAVE_CHUNKS; ++j) {
            size_t offset = j * PATCH_MAX;
            size_t size = sizeof(code) - offset < PATCH_MAX ? sizeof(code) - offset : PATCH_MAX;
            rule[2 + j] = (code_rule_t){ engine_sites[i].module, true,
                                         "resistance: info response cave",
                                         cave + (uint32_t)offset, size, { 0 }, { 0 } };
            memcpy(rule[2 + j].replacement, code + offset, size);
        }
    }
}

/* -- Payload helpers --------------------------------------------------- */

/* op reg,[ebx+disp32], ebx holding the runtime address of the RVA base,
 * so that the operand addresses the image RVA target.
 */
static void ebx_op(payload_t *payload, unsigned char opcode, unsigned reg,
                   uint32_t base, uint32_t target)
{
    EMIT(opcode, (unsigned char)(0x83 | reg << 3), B4(target - base));
}

/* push the runtime address of the image RVA target, through eax. */
static void push_address(payload_t *payload, uint32_t base, uint32_t target)
{
    ebx_op(payload, 0x8d, EAX, base, target);   /* lea eax,[ebx+d] */
    EMIT(0x50);
}

/* Point a short forward branch at the current offset, failing the build
 * if it is out of reach.
 */
static void land(payload_t *payload, uint32_t branch)
{
    if (payload->size - (branch + 2u) > 127u) {
        fputs("RESISTANCE payload: short branch out of range\n", stderr);
        payload->failed = true;
    }
    emit_land(payload, branch);
}

/* -- UI payload -------------------------------------------------------- */

/* trap_Cvar_Set(name, value) for image RVAs. */
static void ui_set(payload_t *payload, uint32_t rva, uint32_t base, uint32_t name,
                   uint32_t value)
{
    push_address(payload, base, value);
    push_address(payload, base, name);
    emit_call(payload, rva, UI_CVAR_SET);
    EMIT(0x83, 0xc4, 0x08);
}

/* eax = (int)trap_Cvar_VariableValue(name). */
static void ui_get(payload_t *payload, uint32_t rva, uint32_t base, uint32_t name)
{
    push_address(payload, base, name);
    emit_call(payload, rva, UI_CVAR_VALUE);
    EMIT(0x83, 0xc4, 0x04);
    emit_call(payload, rva, UI_FTOL);
}

/* trap_Cvar_SetValue(name, the int at control). */
static void ui_set_int(payload_t *payload, uint32_t rva, uint32_t base, uint32_t name,
                       uint32_t control)
{
    ebx_op(payload, 0xdb, 0, base, control);     /* fild dword [ebx+d] */
    EMIT(0x50, 0xd9, 0x1c, 0x24);                /* push; fstp dword [esp] */
    push_address(payload, base, name);
    emit_call(payload, rva, UI_CVAR_SET_VALUE);
    EMIT(0x83, 0xc4, 0x08);
}

/* trap_Cvar_Register(NULL, name, "0", CVAR_ARCHIVE), so that the saved
 * values survive a restart of the game.
 */
static void ui_archive(payload_t *payload, uint32_t rva, uint32_t base, uint32_t name,
                       uint32_t zero)
{
    EMIT(0x6a, 0x01);
    push_address(payload, base, zero);
    push_address(payload, base, name);
    EMIT(0x6a, 0x00);
    emit_call(payload, rva, UI_CVAR_REGISTER);
    EMIT(0x83, 0xc4, 0x10);
}

static bool build_ui(payload_t *payload, uint32_t rva, uint32_t next, uint32_t *entry)
{
    if (!payload_reserve(payload, RESERVE, "RESISTANCE"))
        return false;
    *entry = emit_entry(payload, next);

    uint32_t menu_name = emit_string(payload, "RESISTANCE");
    /* The browser formats the column into 8 bytes with "%3s". */
    uint32_t browser_name = emit_string(payload, "Resist");
    uint32_t loading_name = rva + emit_string(payload, "Resistance");
    uint32_t legacy = rva + emit_string(payload, "g_legacygametype");
    uint32_t zero = rva + emit_string(payload, "0");
    uint32_t one = rva + emit_string(payload, "1");
    uint32_t god = rva + emit_string(payload, "4");
    /* Whether the next three hold the values from before RESISTANCE. */
    uint32_t saved = rva + emit_string(payload, "ui_resistancesaved");
    const struct { uint32_t name, control, cvar; } controls[] = {
        { rva + emit_string(payload, "ui_resistancedifficulty"), UI_DIFFICULTY,
          UI_NAME_DIFFICULTY },
        { rva + emit_string(payload, "ui_resistancefriendlyfire"), UI_FRIENDLY_FIRE,
          UI_NAME_FRIENDLY_FIRE },
        { rva + emit_string(payload, "ui_resistancediffmulti"), UI_DIFF_MULTI,
          UI_NAME_DIFF_MULTI },
    };

    /* restore(): if values are saved, put them back into the controls and
     * their CVARs and forget them. Keeps ebx.
     */
    uint32_t restore = (uint32_t)payload->size;
    EMIT(0x53);                             /* push ebx */
    uint32_t base = emit_anchor(payload, rva, EBX);
    ui_get(payload, rva, base, saved);
    EMIT(0x85, 0xc0, 0x75, 0x02, 0x5b, 0xc3); /* test; jnz; pop ebx; ret */
    for (size_t i = 0; i < ARRAY_SIZE(controls); ++i) {
        ui_get(payload, rva, base, controls[i].name);
        ebx_op(payload, 0x89, EAX, base, controls[i].control); /* mov [control],eax */
        ui_set_int(payload, rva, base, controls[i].cvar, controls[i].control);
    }
    ui_set(payload, rva, base, saved, zero);
    EMIT(0x5b, 0xc3);                       /* pop ebx; ret */

    /* select(index), in place of the mapper for the change event and the
     * server start: apply the mode's rules, then map the index.
     */
    uint32_t select = (uint32_t)payload->size;
    EMIT(0x53);                             /* push ebx */
    base = emit_anchor(payload, rva, EBX);
    EMIT(0x83, 0x7c, 0x24, 0x08, 0x01);     /* cmp dword [esp+8],1 */
    uint32_t resistance = emit_skip_if(payload, 0x74);
    emit_call(payload, rva, rva + restore);
    ui_set(payload, rva, base, legacy, zero);
    EMIT(0x5b);                             /* pop ebx */
    emit_jump(payload, rva, UI_MAPPER);
    land(payload, resistance);

    /* Save the values once, until a restore. The saving is too long for
     * a short branch, so jz hops over a near jmp past it.
     */
    ui_get(payload, rva, base, saved);
    EMIT(0x85, 0xc0, 0x74, 0x05);           /* test eax,eax; jz save */
    uint32_t already = EMIT(0xe9, 0, 0, 0, 0);
    for (size_t i = 0; i < ARRAY_SIZE(controls); ++i) {
        ui_archive(payload, rva, base, controls[i].name, zero);
        ui_set_int(payload, rva, base, controls[i].name, controls[i].control);
    }
    ui_archive(payload, rva, base, saved, zero);
    ui_set(payload, rva, base, saved, one);
    put32(payload->data + already + 1, (uint32_t)payload->size - (already + 5u));

    /* God, friendly fire off, no multiplier: controls, then CVARs. */
    ebx_op(payload, 0xc7, 0, base, UI_DIFFICULTY);
    EMIT(B4(4u));
    ebx_op(payload, 0xc7, 0, base, UI_FRIENDLY_FIRE);
    EMIT(B4(0u));
    ebx_op(payload, 0xc7, 0, base, UI_DIFF_MULTI);
    EMIT(B4(0u));
    ui_set(payload, rva, base, UI_NAME_DIFFICULTY, god);
    ui_set(payload, rva, base, UI_NAME_FRIENDLY_FIRE, zero);
    ui_set(payload, rva, base, UI_NAME_DIFF_MULTI, zero);
    ui_set(payload, rva, base, legacy, one);
    EMIT(0x5b);                             /* pop ebx */
    emit_jump(payload, rva, UI_MAPPER);

    /* The grey-out update tested for deathmatch (0); any index but
     * VANILLA's now greys the controls: cmp dword [esp+4],1; sbb eax,eax.
     */
    uint32_t grey = EMIT(0x83, 0x7c, 0x24, 0x04, 0x01, 0x1b, 0xc0, 0xc3);

    /* The menu always opens on VANILLA, so give back saved values before
     * the initializer reads the CVARs into the controls.
     */
    uint32_t open = (uint32_t)payload->size;
    emit_call(payload, rva, rva + restore);
    emit_jump(payload, rva, UI_MENU_INIT);

    /* Loading screen: for RESISTANCE translate its name into the buffer
     * like a level's and skip the level names; otherwise resume the chain,
     * which tests eax.
     */
    uint32_t loading = EMIT(0x83, 0xc4, 0x08, 0x8b, 0xf0); /* replayed */
    emit_load_address(payload, rva, EAX, legacy - rva);
    EMIT(0x8d, 0x8c, 0x24, B4(0xb8u));      /* lea ecx,[esp+0xb8]: serverinfo */
    EMIT(0x50, 0x51);
    emit_call(payload, rva, UI_INFO_VALUE);
    EMIT(0x83, 0xc4, 0x08);
    EMIT(0x66, 0x81, 0x38, '1', 0);         /* cmp word [eax],"1" */
    uint32_t named = emit_skip_if(payload, 0x74);
    EMIT(0x8b, 0xc6);                       /* mov eax,esi */
    emit_jump(payload, rva, 0xa7c2u);
    land(payload, named);
    EMIT(0x8d, 0x54, 0x24, 0x1c, 0x52,      /* the level cases' arguments */
         0x8d, 0x44, 0x24, 0x24, 0x50,
         0x6a, 0x40,
         0x8d, 0x4c, 0x24, 0x44, 0x51);
    emit_load_address(payload, rva, EAX, loading_name - rva);
    EMIT(0x50);
    emit_call(payload, rva, UI_TRANSLATE);
    EMIT(0x83, 0xc4, 0x14);
    emit_jump(payload, rva, 0xa947u);       /* past the last level case */

    uint32_t targets[ARRAY_SIZE(patch_resistance_ui_hooks)] = {
        select, select, grey, open, loading,
    };
    /* Browser: replay the hooked bytes; for RESISTANCE_LEVEL push its
     * name and continue past the level chain's first push.
     */
    for (size_t i = UI_BROWSER_FIRST; i < ARRAY_SIZE(targets); ++i) {
        uint32_t at = patch_resistance_ui_hooks[i].rva + 6u;
        targets[i] = payload_append(payload, patch_resistance_ui_hooks[i].expected, 6);
        EMIT(0x83, 0xf8, RESISTANCE_LEVEL);
        emit_jump_if(payload, rva, 0x85, at);
        emit_load_address(payload, rva, EAX, browser_name);
        EMIT(0x50);
        emit_jump(payload, rva, at + 9u);
    }

    resolve_hooks(patch_resistance_ui_hooks, ARRAY_SIZE(targets), targets, rva);
    put32(patch_resistance_ui_name[0].replacement, 0x10000000u + rva + menu_name);
    return true;
}

/* -- Game payload ------------------------------------------------------ */

/* syscall(G_CVAR_SET, name, value) for image RVAs. */
static void game_set(payload_t *payload, uint32_t base, uint32_t name, uint32_t value)
{
    push_address(payload, base, value);
    push_address(payload, base, name);
    EMIT(0x6a, G_CVAR_SET);
    ebx_op(payload, 0xff, 2, base, GAME_SYSCALL);   /* call [ebx+d] */
    EMIT(0x83, 0xc4, 0x0c);
}

static bool build_game(payload_t *payload, uint32_t rva, uint32_t next, uint32_t *entry)
{
    if (!payload_reserve(payload, RESERVE, "RESISTANCE"))
        return false;
    *entry = emit_entry(payload, next);

    uint32_t legacy = rva + emit_string(payload, "g_legacygametype");
    uint32_t zero = rva + emit_string(payload, "0");
    uint32_t god = rva + emit_string(payload, "4");
    uint32_t friendly_fire = rva + emit_string(payload, "g_friendlyFire");
    uint32_t diff_multi = rva + emit_string(payload, "sv_diffmulti");
    uint32_t difficulty = rva + emit_string(payload, "sv_difficulty");
    uint32_t message = emit_string(payload,
                                   "print \"The difficulty is fixed in RESISTANCE.\n\"");
    /* g_legacygametype's vmCvar_t; integer follows handle,
     * modificationCount and value.
     */
    while (payload->size & 3)
        EMIT(0);
    uint32_t vmcvar = rva + payload_append(payload, (const unsigned char[272]){ 0 }, 272);
    uint32_t integer = vmcvar + 12u;

    /* force(): the three rules' CVARs; a Cvar_Set to the current value
     * returns at once. Clobbers ebx.
     */
    uint32_t force = (uint32_t)payload->size;
    uint32_t base = emit_anchor(payload, rva, EBX);
    game_set(payload, base, friendly_fire, zero);
    game_set(payload, base, diff_multi, zero);
    game_set(payload, base, difficulty, god);
    EMIT(0xc3);

    /* G_InitGame: after G_RegisterCvars register g_legacygametype, which
     * also applies its latched value. It only holds in coop; there the
     * rules apply, and G_RegisterCvars runs again to read them back.
     */
    uint32_t init = (uint32_t)payload->size;
    emit_call(payload, rva, GAME_REGISTER_CVARS);
    EMIT(0x60);                             /* pushad */
    base = emit_anchor(payload, rva, EBX);
    EMIT(0x6a, 0x24);                       /* CVAR_SERVERINFO | CVAR_LATCH */
    push_address(payload, base, zero);
    push_address(payload, base, legacy);
    push_address(payload, base, vmcvar);
    EMIT(0x6a, G_CVAR_REGISTER);
    ebx_op(payload, 0xff, 2, base, GAME_SYSCALL);
    EMIT(0x83, 0xc4, 0x14);
    ebx_op(payload, 0x83, 7, base, integer);    /* cmp dword [integer],1 */
    EMIT(0x01);
    uint32_t off = emit_skip_if(payload, 0x75);
    ebx_op(payload, 0x83, 7, base, GAME_GAMETYPE); /* cmp dword [g_gametype],3 */
    EMIT(0x03);
    uint32_t coop = emit_skip_if(payload, 0x74);
    game_set(payload, base, legacy, zero);
    push_address(payload, base, vmcvar);
    EMIT(0x6a, G_CVAR_UPDATE);
    ebx_op(payload, 0xff, 2, base, GAME_SYSCALL);
    EMIT(0x83, 0xc4, 0x08);
    uint32_t cleared = emit_skip_if(payload, 0xeb);
    land(payload, coop);
    emit_call(payload, rva, rva + force);
    emit_call(payload, rva, GAME_REGISTER_CVARS);
    land(payload, off);
    land(payload, cleared);
    EMIT(0x61, 0xc3);                       /* popad; ret */

    /* G_RunFrame: enforce the rules ahead of G_UpdateCvars, which then
     * reads back any change a command or vote made.
     */
    uint32_t frame = (uint32_t)payload->size;
    EMIT(0x60);
    base = emit_anchor(payload, rva, EBX);
    ebx_op(payload, 0x83, 7, base, integer);
    EMIT(0x01);
    uint32_t skip = emit_skip_if(payload, 0x75);
    emit_call(payload, rva, rva + force);
    land(payload, skip);
    EMIT(0x61);
    emit_jump(payload, rva, GAME_UPDATE_CVARS);

    /* Cmd_CallVote_f: in RESISTANCE answer a difficulty vote's caller like
     * its "already" case does, through trap_SendServerCommand(ecx, eax),
     * which then returns from the command. esi is the caller's entity.
     */
    uint32_t vote = (uint32_t)payload->size;
    emit_jump_if(payload, rva, 0x85, 0x1f412u);
    base = emit_anchor(payload, rva, EAX);
    EMIT(0x83, 0xb8, B4(integer - base), 0x01);    /* cmp dword [eax+d],1 */
    emit_jump_if(payload, rva, 0x85, 0x1f356u);
    EMIT(0x8d, 0x80, B4(GAME_ENTITIES - base));    /* lea eax,[eax+d] */
    EMIT(0x2b, 0xf0,                               /* sub esi,eax */
         0xb8, B4(0x10624dd3u), 0xf7, 0xee,        /* imul: / sizeof(gentity_t) */
         0xc1, 0xfa, 0x06, 0x8b, 0xca, 0xc1, 0xe9, 0x1f, 0x03, 0xca);
    emit_load_address(payload, rva, EAX, message);
    emit_jump(payload, rva, 0x1f323u);

    const uint32_t targets[] = { init, frame, vote };
    _Static_assert(ARRAY_SIZE(targets) == ARRAY_SIZE(patch_resistance_game_hooks),
                   "every GAME hook needs a helper");
    resolve_hooks(patch_resistance_game_hooks, ARRAY_SIZE(targets), targets, rva);
    return true;
}

/* -- Client game payload ----------------------------------------------- */

static bool build_cgame(payload_t *payload, uint32_t rva, uint32_t next, uint32_t *entry)
{
    if (!payload_reserve(payload, RESERVE, "RESISTANCE"))
        return false;
    *entry = emit_entry(payload, next);

    uint32_t name = emit_string(payload, "Resistance");
    uint32_t legacy = emit_string(payload, "g_legacygametype");
    /* The connection screen reads its name from names[level]; this slot
     * receives the name's runtime address, and the level becomes the
     * slot's index from the table. Both RVAs are dword-aligned.
     */
    while ((rva + payload->size) & 3)
        EMIT(0);
    uint32_t slot = EMIT(0, 0, 0, 0);
    uint32_t slot_index = (rva + slot - CGAME_CONNECT_NAMES) / 4u;

    /* Scoreboard: with g_legacygametype "1" in the serverinfo, replace the
     * level's name in edx.
     */
    uint32_t scoreboard = EMIT(0x83, 0xc4, 0x04, 0x60); /* add esp,4; pushad */
    emit_load_address(payload, rva, EBX, legacy);
    EMIT(0x8b, 0xcd);                       /* mov ecx,ebp */
    emit_call(payload, rva, CGAME_INFO_VALUE);
    EMIT(0x66, 0x81, 0x38, '1', 0);         /* cmp word [eax],"1" */
    uint32_t level = emit_skip_if(payload, 0x75);
    emit_load_address(payload, rva, EDX, name);
    EMIT(0x89, 0x54, 0x24, 0x14);           /* pushad's edx */
    land(payload, level);
    EMIT(0x61, 0x52, 0x8d, 0x44, 0x24, 0x34); /* popad; replayed */
    emit_jump(payload, rva, 0x76328u);

    /* Connection screen: with g_legacygametype "1" skip the clamp with the
     * slot's index as the level.
     */
    uint32_t connect = EMIT(0x8b, 0xd8, 0x83, 0xc4, 0x14, 0x53); /* replayed; push ebx */
    emit_load_address(payload, rva, EBX, legacy);
    EMIT(0x8b, 0xce);                       /* mov ecx,esi */
    emit_call(payload, rva, CGAME_INFO_VALUE);
    EMIT(0x5b);                             /* pop ebx */
    EMIT(0x66, 0x81, 0x38, '1', 0);
    emit_jump_if(payload, rva, 0x85, 0x9adau);  /* the clamp */
    emit_load_address(payload, rva, EAX, name);
    emit_load_address(payload, rva, ECX, slot);
    EMIT(0x89, 0x01);                       /* mov [ecx],eax */
    EMIT(0xbb, B4(slot_index));             /* mov ebx,index */
    emit_jump(payload, rva, 0x9aecu);

    const uint32_t targets[] = { scoreboard, connect };
    _Static_assert(ARRAY_SIZE(targets) == ARRAY_SIZE(patch_resistance_cgame_hooks),
                   "every CGAME hook needs a helper");
    resolve_hooks(patch_resistance_cgame_hooks, ARRAY_SIZE(targets), targets, rva);
    return true;
}

static const payload_feature_t resistance_ui = { UI, "RESISTANCE", build_ui };
static const payload_feature_t resistance_game = { GAME, "RESISTANCE", build_game };
static const payload_feature_t resistance_cgame = { CGAME, "RESISTANCE", build_cgame };

/* The payloads and their hooks must be enabled together. */
REGISTER_CODE_RULES(patch_resistance_ui_hooks)
REGISTER_CODE_RULES(patch_resistance_ui_name)
REGISTER_CODE_RULES(patch_resistance_ui)
REGISTER_ADDRESS_RULES(patch_resistance_ui_addresses)
REGISTER_CODE_RULES(patch_resistance_game_hooks)
REGISTER_CODE_RULES(patch_resistance_cgame_hooks)
REGISTER_CODE_RULES(patch_resistance_engine)
REGISTER_PAYLOAD_FEATURE(resistance_ui)
REGISTER_PAYLOAD_FEATURE(resistance_game)
REGISTER_PAYLOAD_FEATURE(resistance_cgame)
