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
 * Instead of god's skill-g.cfg and scripts/waves/<map>-g.xml, the server
 * uses the mode's own files: skill-res.cfg and scripts/waves/<map>-res.xml,
 * which a <map>-res.toml or <map>-res.lua next to it replaces like for
 * every level (wave_scripts.c). As with TITAN, a map without any of the
 * three fails to load. The client's CG_Init picks skill-res.cfg as well
 * when its level is RESISTANCE_LEVEL, which CG_ParseServerinfo stores for
 * the mode. That level is part of cgs, though, which a newly loaded
 * cgamex86.dll starts with zeroed, so like retail's levels and TITAN's
 * this only takes effect where CG_Init finds the level already set.
 * The engines' skill cfg checksum still compares skill-g.cfg.
 *
 * While RESISTANCE is selected, the host-game menu shows the mode's badge,
 * textures/ui/mmbdgres, where the three fixed controls it hides would be.
 *
 * The menu remembers the selected mode in the archived CVAR
 * ui_legacygamemode, the dropdown's index, and opens on it again, also
 * after a restart of the game.
 *
 * The server browser, the loading screens and the scoreboard name the
 * mode instead of the difficulty; the scoreboard also leaves out the diff
 * multiplier and friendly fire, which the mode fixes. The browser cannot learn it from the
 * gametype, which its "Regular" filter requires to be 3, so the engines'
 * info responses report difficulty 6 for it instead.
 *
 * Like TITAN's, every hook is a relative jump or call into the injected
 * section (or, in the engines, into .text's file-backed slack), whose code
 * addresses its data with call/pop; no hook overwrites relocated bytes.
 */

/* The difficulty the engines' info responses report for RESISTANCE. */
#define RESISTANCE_LEVEL 6

/* The UI's code, strings and data take about 1400 bytes, the game's 700. */
#define RESERVE 2048u

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
#define UI_MENU 0x8ec28u        /* the host-game menu's menuframework */
#define UI_MENU_ADD_ITEM 0x2c130u /* Menu_AddItem(menu, item), cdecl */
/* The display's aspect: 1 for 16:9, 2 for 16:10, 3 for 5:4, else 0.
 * Clobbers eax only.
 */
#define UI_ASPECT 0x3f770u
#define UI_VID_WIDTH 0x109e80u
#define UI_VID_HEIGHT 0x109e84u

/* A bitmap menu item (type 6), as far as the badge sets it up: the shader
 * name, the position and size, which Bitmap_Init converts to pixels when
 * the item is added, and the flags. The draw registers the shader lazily
 * into a later member, which zeroing the item resets.
 */
#define ITEM_SIZE 0xe4u
#define ITEM_BITMAP 6u
#define ITEM_NAME 0x04u
#define ITEM_X 0x0cu
#define ITEM_Y 0x10u
#define ITEM_FLAGS 0x2cu
#define ITEM_WIDTH 0xc8u
#define ITEM_HEIGHT 0xccu
#define ITEM_HIDDEN 0x1000u
/* Converted to pixels, inactive: the map preview's flags. */
#define ITEM_PICTURE 0x204000u

/* The badge's place in the 640x480 units of the menu's labels, which
 * widescreen displays show as a centered 4:3 area: from the labels'
 * left edge (116) to the dropdowns' right one (354), centered between
 * the server name's entry field and the Map label, at the texture's
 * aspect of 4:1.
 */
#define BADGE_X 120.0f
#define BADGE_Y 145.0f
#define BADGE_WIDTH 250.0f
#define BADGE_HEIGHT 100.0f

/* The menu's control values, which its initializer reads from the CVARs
 * whose names follow, and which each control's event writes back.
 */
#define UI_DIFFICULTY 0x92afcu
#define UI_FRIENDLY_FIRE 0x92f78u
#define UI_DIFF_MULTI 0x92fd4u
#define UI_NAME_DIFFICULTY 0x6007cu   /* "sv_difficulty" */
#define UI_NAME_FRIENDLY_FIRE 0x61660u /* "g_friendlyfire" */
#define UI_NAME_DIFF_MULTI 0x61670u   /* "sv_diffmulti" */
/* The "Game mode:" dropdown's selected index, which the initializer's
 * memset of the menu zeroes, and the CVAR its change event sets.
 */
#define UI_GAME_MODE 0x93184u
#define UI_NAME_GAMETYPE 0x60098u     /* "g_gametype" */
#define UI_GAME_MODES 3u

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
#define GAME_FTOL 0x94c30u          /* __ftol2_sse, st(0) -> eax */
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
    /* G_InitGame: call __ftol2_sse of sv_difficulty, the level in coop,
     * ahead of mov esi,eax and TITAN's hook in front of the skill-*.cfg
     * jump table.
     */
    { GAME, true, "resistance: exec skill-res.cfg", 0x24bef, 5,
      { 0xe8, B4(GAME_FTOL - 0x24bf4u) }, { 0xe8 } },
    /* G_InitWaves: cmp esi,4; jne easy, god's test in the chain of levels
     * that picks the wave script's format, esi being G_InitGame's level.
     */
    { GAME, true, "resistance: load scripts/waves/<map>-res.xml", 0x5f34c, 5,
      { 0x83, 0xfe, 0x04, 0x75, 0x0c }, { 0xe9 } },
};

/* -- cgamex86.dll ------------------------------------------------------ */

#define CGAME_INFO_VALUE 0x3d9b0u   /* Info_ValueForKey, key in ebx, info in ecx */
#define CGAME_CONNECT_NAMES 0xcae80u
/* Lang_Text(key, buf) with the size in edx and the direction and text
 * type outputs in ecx and eax; a key not starting with '#' is copied.
 */
#define CGAME_LANG_TEXT 0x33660u
#define CGAME_SET_COLOR 0x33140u    /* vec4_t in eax, NULL for the default */
#define CGAME_ATOI 0x82e32u
/* The level CG_Init execs the skill-*.cfg of, which CG_ParseServerinfo
 * sets from sv_difficulty. Nothing else reads it.
 */
#define CGAME_LEVEL 0xd749ecu

/* The scoreboard header (cgamex86 RVA 0x7626c onwards) draws its label and
 * value pairs left to right, advancing the x at [S+0x10], S being esp
 * between the pairs, ebp holding the serverinfo: "Difficulty:", "Diff
 * Multiplier:", "Players:" and "Friendly Fire:". Besides drawing, the diff
 * multiplier's pair leaves two floats the players' pair adds to its x:
 * [S+0x24], the int gap at [S+0x28], and [S+0x34], four times that. The
 * players' label starts with call set_color at RVA 0x76507 while the
 * multiplier's last Lang_Text arguments are still pushed, at S-0x14.
 */

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
    /* Scoreboard: the Lang_Text call that looks up the first label,
     * "#scorebrd-diff", pushed as its first stack argument.
     */
    { CGAME, true, "resistance: scoreboard mode label", 0x7628f, 5,
      { 0xe8, B4(CGAME_LANG_TEXT - 0x76294u) }, { 0xe8 } },
    /* Scoreboard: lea eax,[esp+0x44]; call set_color, the start of the
     * "Diff Multiplier:" label and value, also where survival skips to.
     */
    { CGAME, true, "resistance: scoreboard diff multiplier", 0x763aa, 9,
      { 0x8d, 0x44, 0x24, 0x44, 0xe8, B4(CGAME_SET_COLOR - 0x763b3u) },
      { 0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 } },
    /* Scoreboard: call set_color, the start of the "Friendly Fire:" label
     * and value, the header's last.
     */
    { CGAME, true, "resistance: scoreboard friendly fire", 0x76620, 5,
      { 0xe8, B4(CGAME_SET_COLOR - 0x76625u) }, { 0xe9 } },
    /* CG_ParseServerinfo: call atoi of sv_difficulty, ahead of storing
     * and clamping it as the level, with ebp the serverinfo.
     */
    { CGAME, true, "resistance: client level", 0x302a5, 5,
      { 0xe8, B4(CGAME_ATOI - 0x302aau) }, { 0xe8 } },
    /* CG_Init: cmp eax,2; jne next, with eax the level, in the chain of
     * level tests ahead of TITAN's hook and the skill-*.cfg exec.
     */
    { CGAME, true, "resistance: client exec skill-res.cfg", 0x27cef, 5,
      { 0x83, 0xf8, 0x02, 0x75, 0x07 }, { 0xe9 } },
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
    /* The dropdown's last selected index, for the next opening. */
    uint32_t mode = rva + emit_string(payload, "ui_legacygamemode");
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

    /* The badge's menu item and the floats that place it. */
    uint32_t badge_name = rva + emit_string(payload, "textures/ui/mmbdgres");
    while (payload->size & 3)
        EMIT(0);
    uint32_t item = rva + payload_append(payload, (const unsigned char[ITEM_SIZE]){ 0 },
                                         ITEM_SIZE);
    uint32_t four_thirds = rva + payload_append(payload, &(float){ 4.0f / 3.0f }, 4);
    uint32_t half_width = rva + payload_append(payload, &(float){ 320.0f }, 4);
    uint32_t badge_x = rva + payload_append(payload, &(float){ BADGE_X }, 4);
    uint32_t badge_y = rva + payload_append(payload, &(float){ BADGE_Y }, 4);
    uint32_t badge_width = rva + payload_append(payload, &(float){ BADGE_WIDTH }, 4);
    uint32_t badge_height = rva + payload_append(payload, &(float){ BADGE_HEIGHT }, 4);

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
     * server start, and called when the menu opens: remember the index,
     * apply the mode's rules, then map the index. Every caller passes the
     * dropdown's index.
     */
    uint32_t select = (uint32_t)payload->size;
    EMIT(0x53);                             /* push ebx */
    base = emit_anchor(payload, rva, EBX);
    ui_set_int(payload, rva, base, mode, UI_GAME_MODE);
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

    /* The grey-out update, part of the menu's draw callback ahead of
     * Menu_Draw, runs every frame: show the badge only for RESISTANCE's
     * index (1). It tested for deathmatch (0); any index but VANILLA's
     * now greys the controls: cmp dword [esp+4],1; sbb eax,eax.
     */
    uint32_t grey = (uint32_t)payload->size;
    uint32_t at = emit_anchor(payload, rva, EAX);
    EMIT(0x81, 0x88, B4(item + ITEM_FLAGS - at), B4(ITEM_HIDDEN)); /* or [eax+d] */
    EMIT(0x83, 0x7c, 0x24, 0x04, 0x01);     /* cmp dword [esp+4],1 */
    uint32_t other = emit_skip_if(payload, 0x75);
    EMIT(0x81, 0xa0, B4(item + ITEM_FLAGS - at), B4(~ITEM_HIDDEN)); /* and [eax+d] */
    land(payload, other);
    EMIT(0x83, 0x7c, 0x24, 0x04, 0x01, 0x1b, 0xc0, 0xc3);

    /* Unless the menu opens on RESISTANCE again, give back saved values
     * before the initializer reads the CVARs into the controls; for
     * RESISTANCE they stay saved and the controls read its rules.
     */
    uint32_t open = (uint32_t)payload->size;
    EMIT(0x53);                             /* push ebx */
    base = emit_anchor(payload, rva, EBX);
    ui_archive(payload, rva, base, mode, zero);
    ui_get(payload, rva, base, mode);
    EMIT(0x83, 0xf8, 0x01);                 /* cmp eax,1 */
    uint32_t keep = emit_skip_if(payload, 0x74);
    emit_call(payload, rva, rva + restore);
    land(payload, keep);
    EMIT(0x5b);                             /* pop ebx */
    emit_call(payload, rva, UI_MENU_INIT);
    EMIT(0x53, 0x57);                       /* push ebx; push edi */
    base = emit_anchor(payload, rva, EBX);

    /* Select the remembered index, VANILLA's for any out of range, after
     * the initializer zeroed it, then do what the dropdown's change event
     * does: select() applies the mode's rules, and g_gametype gets the
     * mapped index.
     */
    ui_get(payload, rva, base, mode);
    EMIT(0x83, 0xf8, UI_GAME_MODES - 1u);   /* cmp eax,last */
    uint32_t known = emit_skip_if(payload, 0x76); /* jbe */
    EMIT(0x31, 0xc0);                       /* xor eax,eax */
    land(payload, known);
    ebx_op(payload, 0x89, EAX, base, UI_GAME_MODE); /* mov [ebx+d],eax */
    EMIT(0x50);                             /* push eax */
    emit_call(payload, rva, rva + select);
    EMIT(0x89, 0x04, 0x24,                  /* mov [esp],eax */
         0xdb, 0x04, 0x24,                  /* fild dword [esp] */
         0xd9, 0x1c, 0x24);                 /* fstp dword [esp] */
    push_address(payload, base, UI_NAME_GAMETYPE);
    emit_call(payload, rva, UI_CVAR_SET_VALUE);
    EMIT(0x83, 0xc4, 0x08);                 /* add esp,8 */

    /* Add the badge as the menu's last item, hidden until the grey-out
     * update shows it.
     */
    /* Bitmap_Init overwrote the position and size in pixels, and the
     * draw cached the shader: start over from a zeroed item.
     */
    ebx_op(payload, 0x8d, EDI, base, item);  /* lea edi,[ebx+d] */
    EMIT(0x31, 0xc0,                        /* xor eax,eax */
         0xb9, B4(ITEM_SIZE / 4u),          /* mov ecx,dwords */
         0xf3, 0xab);                       /* rep stosd */
    ebx_op(payload, 0xc7, 0, base, item);   /* mov dword [ebx+d],imm */
    EMIT(B4(ITEM_BITMAP));
    ebx_op(payload, 0x8d, EAX, base, badge_name);
    ebx_op(payload, 0x89, EAX, base, item + ITEM_NAME);
    ebx_op(payload, 0xc7, 0, base, item + ITEM_FLAGS);
    EMIT(B4(ITEM_PICTURE | ITEM_HIDDEN));

    /* Bitmap_Init stretches 640x480 units over the screen, the labels'
     * centered 4:3 area being narrower on widescreen displays: there
     * scale horizontally by k = 4/3 * height / width around the center,
     * elsewhere k is 1, as for the menu's own layout.
     */
    emit_call(payload, rva, UI_ASPECT);
    EMIT(0xd9, 0xe8,                        /* fld1 */
         0x48, 0x83, 0xf8, 0x01);           /* dec eax; cmp eax,1 */
    uint32_t narrow = emit_skip_if(payload, 0x77); /* ja: neither 16:9 nor 16:10 */
    EMIT(0xdd, 0xd8);                       /* fstp st(0) */
    ebx_op(payload, 0xdb, 0, base, UI_VID_HEIGHT); /* fild dword [ebx+d] */
    ebx_op(payload, 0xd8, 1, base, four_thirds);   /* fmul dword [ebx+d] */
    ebx_op(payload, 0xda, 6, base, UI_VID_WIDTH);  /* fidiv dword [ebx+d] */
    land(payload, narrow);
    ebx_op(payload, 0xd9, 0, base, badge_width);   /* fld dword [ebx+d] */
    EMIT(0xd8, 0xc9);                       /* fmul st(0),st(1) */
    ebx_op(payload, 0xd9, 3, base, item + ITEM_WIDTH); /* fstp dword [ebx+d] */
    ebx_op(payload, 0xd9, 0, base, badge_x);
    ebx_op(payload, 0xd8, 4, base, half_width);    /* fsub dword [ebx+d] */
    EMIT(0xde, 0xc9);                       /* fmulp st(1),st(0) */
    ebx_op(payload, 0xd8, 0, base, half_width);    /* fadd dword [ebx+d] */
    ebx_op(payload, 0xd9, 3, base, item + ITEM_X);
    ebx_op(payload, 0xd9, 0, base, badge_y);
    ebx_op(payload, 0xd9, 3, base, item + ITEM_Y);
    ebx_op(payload, 0xd9, 0, base, badge_height);
    ebx_op(payload, 0xd9, 3, base, item + ITEM_HEIGHT);

    push_address(payload, base, item);
    push_address(payload, base, UI_MENU);
    emit_call(payload, rva, UI_MENU_ADD_ITEM);
    EMIT(0x83, 0xc4, 0x08,                  /* add esp,8 */
         0x5f, 0x5b, 0xc3);                 /* pop edi; pop ebx; ret */

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
    uint32_t skill_cfg = emit_string(payload, "exec skill-res.cfg\n");
    uint32_t waves = emit_string(payload, "scripts/waves/%s-res.xml");
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

    /* G_InitGame, in place of the call of __ftol2_sse: in RESISTANCE
     * set the level as mov esi,eax would and exec skill-res.cfg instead
     * of the level's cfg; otherwise return to mov esi,eax. The init hook
     * above has registered g_legacygametype and cleared it outside coop
     * by then. esi is free until mov esi,eax.
     */
    uint32_t skill = (uint32_t)payload->size;
    emit_call(payload, rva, GAME_FTOL);
    base = emit_anchor(payload, rva, ESI);
    EMIT(0x83, 0xbe, B4(integer - base), 0x01);    /* cmp dword [esi+d],1 */
    uint32_t level_cfg = emit_skip_if(payload, 0x75);
    EMIT(0x8b, 0xf0,                               /* mov esi,eax */
         0x83, 0xc4, 0x04);                        /* add esp,4: no return */
    emit_load_address(payload, rva, EAX, skill_cfg);
    emit_jump(payload, rva, 0x24c23u);             /* trap_SendConsoleCommand */
    land(payload, level_cfg);
    EMIT(0xc3);

    /* G_InitWaves: in RESISTANCE format the map's name into
     * scripts/waves/<map>-res.xml the way each level's case does;
     * otherwise replay god's test. ecx is free here, like in god's case.
     */
    uint32_t wave = (uint32_t)payload->size;
    base = emit_anchor(payload, rva, ECX);
    EMIT(0x83, 0xb9, B4(integer - base), 0x01);    /* cmp dword [ecx+d],1 */
    uint32_t resistance_waves = emit_skip_if(payload, 0x74);
    EMIT(0x83, 0xfe, 0x04);                        /* cmp esi,4 */
    emit_jump_if(payload, rva, 0x85, 0x5f35du);    /* jne: easy, or TITAN */
    emit_jump(payload, rva, 0x5f351u);             /* god */
    land(payload, resistance_waves);
    EMIT(0x8d, 0x4c, 0x24, 0x08, 0x51);            /* lea ecx,[esp+8]; push ecx */
    emit_load_address(payload, rva, ECX, waves);
    EMIT(0x51);                                    /* push ecx */
    emit_jump(payload, rva, 0x5f367u);             /* Com_sprintf */

    const uint32_t targets[] = { init, frame, vote, skill, wave };
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
    uint32_t mode = emit_string(payload, "Mode:");
    uint32_t skill_cfg = emit_string(payload, "exec skill-res.cfg\n");
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

    /* is_resistance(): ZF set when the serverinfo in ebp has
     * g_legacygametype "1"; keeps every register.
     */
    uint32_t is_resistance = EMIT(0x60);    /* pushad */
    emit_load_address(payload, rva, EBX, legacy);
    EMIT(0x8b, 0xcd);                       /* mov ecx,ebp */
    emit_call(payload, rva, CGAME_INFO_VALUE);
    EMIT(0x66, 0x81, 0x38, '1', 0,          /* cmp word [eax],"1" */
         0x61, 0xc3);                       /* popad; ret */

    /* Scoreboard label, called in place of Lang_Text: "Mode:" instead of
     * "#scorebrd-diff", which Lang_Text copies as it is.
     */
    uint32_t label = (uint32_t)payload->size;
    emit_call(payload, rva, rva + is_resistance);
    uint32_t lookup = emit_skip_if(payload, 0x75);
    EMIT(0x53);                             /* push ebx */
    emit_load_address(payload, rva, EBX, mode);
    EMIT(0x89, 0x5c, 0x24, 0x08,            /* mov [esp+8],ebx: the key */
         0x5b);                             /* pop ebx */
    land(payload, lookup);
    emit_jump(payload, rva, CGAME_LANG_TEXT);

    /* Scoreboard diff multiplier: skipped with its floats computed, on to
     * the players' pair with the stack it expects.
     */
    uint32_t diff_multi = (uint32_t)payload->size;
    emit_call(payload, rva, rva + is_resistance);
    uint32_t show_multi = emit_skip_if(payload, 0x75);
    EMIT(0xdb, 0x44, 0x24, 0x28,            /* fild dword [esp+0x28] */
         0xd9, 0x54, 0x24, 0x24,            /* fst dword [esp+0x24] */
         0xd8, 0xc0, 0xd8, 0xc0,            /* fadd st,st; fadd st,st */
         0xd9, 0x5c, 0x24, 0x34,            /* fstp dword [esp+0x34] */
         0x83, 0xec, 0x14,                  /* sub esp,0x14 */
         0x8d, 0x44, 0x24, 0x58);           /* lea eax,[esp+0x58] */
    emit_jump(payload, rva, 0x76507u);
    land(payload, show_multi);
    EMIT(0x8d, 0x44, 0x24, 0x44);           /* replayed */
    emit_call(payload, rva, CGAME_SET_COLOR);
    emit_jump(payload, rva, 0x763b3u);

    /* Scoreboard friendly fire: skipped by dropping the players' last
     * Lang_Text arguments, on to the header's set_color(NULL).
     */
    uint32_t friendly_fire = (uint32_t)payload->size;
    emit_call(payload, rva, rva + is_resistance);
    uint32_t show_ff = emit_skip_if(payload, 0x75);
    EMIT(0x83, 0xc4, 0x10);                 /* add esp,0x10 */
    emit_jump(payload, rva, 0x76744u);
    land(payload, show_ff);
    emit_call(payload, rva, CGAME_SET_COLOR); /* replayed */
    emit_jump(payload, rva, 0x76625u);

    /* CG_ParseServerinfo, in place of the call of atoi: in RESISTANCE
     * store RESISTANCE_LEVEL as the level, past the clamp that would turn
     * it into god, and return from the function, dropping this call's
     * return address and atoi's argument. ebx is popped there.
     */
    uint32_t client_level = (uint32_t)payload->size;
    EMIT(0xff, 0x74, 0x24, 0x04);           /* push [esp+4] */
    emit_call(payload, rva, CGAME_ATOI);
    EMIT(0x83, 0xc4, 0x04);
    emit_call(payload, rva, rva + is_resistance);
    uint32_t difficulty = emit_skip_if(payload, 0x75);
    uint32_t base = emit_anchor(payload, rva, EBX);
    EMIT(0xc7, 0x83, B4(CGAME_LEVEL - base), B4((uint32_t)RESISTANCE_LEVEL)); /* mov [ebx+d] */
    EMIT(0x83, 0xc4, 0x08);                 /* add esp,8 */
    emit_jump(payload, rva, 0x302d0u);      /* pop edi, esi, ebp, ebx; ret */
    land(payload, difficulty);
    EMIT(0xc3);

    /* CG_Init: exec skill-res.cfg for RESISTANCE_LEVEL; otherwise replay
     * the test for hard.
     */
    uint32_t skill = (uint32_t)payload->size;
    EMIT(0x83, 0xf8, RESISTANCE_LEVEL);
    uint32_t other = emit_skip_if(payload, 0x75);
    emit_load_address(payload, rva, EAX, skill_cfg);
    emit_jump(payload, rva, 0x27d16u);      /* trap_SendConsoleCommand */
    land(payload, other);
    EMIT(0x83, 0xf8, 0x02);                 /* cmp eax,2 */
    emit_jump_if(payload, rva, 0x85, 0x27cfbu); /* jne: brute, or TITAN */
    emit_jump(payload, rva, 0x27cf4u);      /* mov eax,"exec skill-h.cfg\n" */

    const uint32_t targets[] = {
        scoreboard, connect, label, diff_multi, friendly_fire, client_level, skill,
    };
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
