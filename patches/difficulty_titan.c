#include "patches.h"

/* TITAN is difficulty 5, one level past god. Like the retail levels
 * 0-4 (easy, medium, hard, brute, god), it consists of two files in the
 * game's search path: skill-t.cfg, which both the server and the client
 * exec, and scripts/waves/<map>-t.xml. The host-game menu, the in-game
 * vote menu, "callvote difficulty" and the engine's "changedifficulty"
 * accept 5, and sv_difficulty 5 selects those files. The loading screen,
 * the scoreboard and the server browser name it.
 *
 * Survival and teamkill keep their own levels: G_InitGame plays medium or
 * easy there regardless of sv_difficulty, and G_RaiseDifficulty still
 * scales NPC health beyond god instead of loading any files.
 *
 * Every hook is a relative jump or call into the injected section, whose
 * helpers address their strings with call/pop. No hook overwrites bytes
 * that a base relocation covers, so the DLLs still rebase correctly.
 */

/* Strings and helpers take about 250 bytes per module. */
#define RESERVE 512u

/* Hook replacements receive their rel32 once the payload has a known RVA. */
static code_rule_t patch_titan_game_hooks[] = {
    /* G_InitGame: cmp esi,4; ja exec_easy, ahead of the skill-*.cfg jump
     * table, with esi being sv_difficulty outside survival and teamkill.
     */
    { GAME, true, "titan: exec skill-t.cfg", 0x24bf6, 5,
      { 0x83, 0xfe, 0x04, 0x77, 0x23 }, { 0xe9, 0, 0, 0, 0 } },
    /* G_InitWaves: the easy fallback's lea edx,[esp+8]; push edx. */
    { GAME, true, "titan: load scripts/waves/<map>-t.xml", 0x5f35d, 5,
      { 0x8d, 0x54, 0x24, 0x08, 0x52 }, { 0xe9, 0, 0, 0, 0 } },
    /* Cmd_CallVote_f indexes the 20-byte names at 0xb6cf0 by the level;
     * index 5 would read unrelated data. Both lookups load the name with
     * lea scratch,[edi+edi*4]; lea dst,[scratch*4+names].
     */
    { GAME, true, "titan: vote name, unchanged level", 0x1f37d, 5,
      { 0x8d, 0x04, 0xbf, 0x8d, 0x0c }, { 0xe9, 0, 0, 0, 0 } },
    { GAME, true, "titan: vote name, vote message", 0x1f3ca, 5,
      { 0x8d, 0x14, 0xbf, 0x8d, 0x04 }, { 0xe9, 0, 0, 0, 0 } },
};

static code_rule_t patch_titan_cgame_hooks[] = {
    /* The client's own skill-*.cfg exec, for the cost and limit cvars it
     * shows: cmp eax,3; jne next, inside the chain of level tests.
     */
    { CGAME, true, "titan: client exec skill-t.cfg", 0x27cfb, 5,
      { 0x83, 0xf8, 0x03, 0x75, 0x07 }, { 0xe9, 0, 0, 0, 0 } },
    /* Scoreboard: atoi(sv_difficulty), which then indexes five names. */
    { CGAME, true, "titan: scoreboard name", 0x76314, 5,
      { 0xe8, B4(0x82e32u - 0x76319u) }, { 0xe8, 0, 0, 0, 0 } },
    /* The connection screen's sixth name, "MAX", is never shown because
     * that screen clamps the level to god. Point it to TITAN's name;
     * the slot keeps its relocation.
     */
    { CGAME, true, "titan: connection screen name", 0xcae94, 4,
      { B4(0x300a86d8u) }, { 0 } },
};

static const code_rule_t patch_titan_levels[] = {
    { GAME, true, "titan: callvote difficulty up to 5", 0x1f374, 3,
      { 0x83, 0xff, 0x04 }, { 0x83, 0xff, 0x05 } },
    { GAME, true, "titan: callvote help", 0xa01b5, 6,
      { '[', '0', '.', '.', '4', ']' }, { '[', '0', '.', '.', '5', ']' } },
    { GAME, true, "titan: callvote error", 0xa0281, 6,
      { '[', '0', '.', '.', '4', ']' }, { '[', '0', '.', '.', '5', ']' } },

    /* SV_ChangeDifficulty_f, which "callvote difficulty" passes on. */
    { ENGINE, true, "titan: changedifficulty up to 5", 0x3750f, 3,
      { 0x83, 0xf8, 0x04 }, { 0x83, 0xf8, 0x05 } },
    { ENGINE, true, "titan: changedifficulty usage", 0x1d4a20, 6,
      { '<', '0', '.', '.', '4', '>' }, { '<', '0', '.', '.', '5', '>' } },
    { SERVER, true, "titan: changedifficulty up to 5", 0x3692f, 3,
      { 0x83, 0xf8, 0x04 }, { 0x83, 0xf8, 0x05 } },
    { SERVER, true, "titan: changedifficulty usage", 0x118f58, 6,
      { '<', '0', '.', '.', '4', '>' }, { '<', '0', '.', '.', '5', '>' } },

    /* CG_ParseServerinfo resets levels from 5 on to easy (cmp eax,5; jl). */
    { CGAME, true, "titan: client keeps level 5", 0x302c5, 3,
      { 0x83, 0xf8, 0x05 }, { 0x83, 0xf8, 0x06 } },
    /* The connection screen clamps its name index to god. */
    { CGAME, true, "titan: connection screen level", 0x9ae2, 10,
      { 0x83, 0xfb, 0x04, 0x7e, 0x05, 0xbb, B4(4u) },
      { 0x83, 0xfb, 0x05, 0x7e, 0x05, 0xbb, B4(5u) } },
};

/* The UI has no payload, so its names go into the zero bytes ahead of the
 * Git label at 0x6a500. version.c's VirtualSize extension of .rdata maps
 * them; without it the loader would zero-fill them.
 */
#define UI_MENU_NAME 0x6a460u
#define UI_VOTE_NAME 0x6a470u
#define UI_BROWSER_NAMES 0x6a480u
#define UI_BROWSER_TITAN 0x6a498u

/* The server browser fills its difficulty column in four copies of an
 * if-chain over the levels 0-4 ("Easy" for any level up to 0), pushing
 * the short names it formats with "%3s"; other levels leave the column
 * empty. Each copy's "Easy" push becomes push [eax*4+names], its operand
 * being the six-entry table below instead of "Easy", which keeps that
 * slot's relocation. A clamp in bytes of the now unused chain raises
 * levels below 0 to 0 and skips the column for those above 5, as before.
 * The other names' pushes and their relocations stay in place, unused.
 *
 * In the first two copies each case pushes all four arguments; the clamp
 * replaces the hard case's tail and the brute test, both now unreached.
 */
#define UI_BROWSER_LONG(at, lea, push)                                      \
    { UI, true, "titan: server browser level", (at), 5,                     \
      { 0x85, 0xc0, 0x7f, 0x12, 0x68 },                                     \
      { 0xeb, 0x2f,                         /* jmp clamp */                 \
        0xff, 0x34, 0x85 } },               /* push [eax*4+names] */       \
    { UI, true, "titan: server browser names", (at) + 5u, 4,                \
      { B4(0x100644c4u) }, { B4(0x10000000u + UI_BROWSER_NAMES) } },        \
    { UI, true, "titan: server browser clamp", (at) + 0x31u, 13,            \
      { 0x6a, 0x08, 0x8d, (lea), 0xf0, (push), 0xeb, 0x2c,                  \
        0x83, 0xf8, 0x03, 0x75, 0x12 },                                     \
      { 0x85, 0xc0, 0x7f, 0x02, 0x31, 0xc0, /* test; jg; xor eax,eax */    \
        0x83, 0xf8, 0x05, 0x77, 0x31,       /* cmp eax,5; ja no column */   \
        0xeb, 0xc4 } }                      /* jmp push */

/* The last two copies push only the name in each case. Their clamp is
 * split around the unused medium and hard pushes.
 */
#define UI_BROWSER_SHORT(at)                                                \
    { UI, true, "titan: server browser level", (at), 5,                     \
      { 0x85, 0xc0, 0x7f, 0x07, 0x68 },                                     \
      { 0xeb, 0x13,                         /* jmp clamp */                 \
        0xff, 0x34, 0x85 } },               /* push [eax*4+names] */       \
    { UI, true, "titan: server browser names", (at) + 5u, 4,                \
      { B4(0x100644c4u) }, { B4(0x10000000u + UI_BROWSER_NAMES) } },        \
    { UI, true, "titan: server browser clamp", (at) + 0x15u, 8,             \
      { 0xeb, 0x22, 0x83, 0xf8, 0x02, 0x75, 0x07, 0x68 },                   \
      { 0x85, 0xc0, 0x7f, 0x02, 0x31, 0xc0, /* test; jg; xor eax,eax */    \
        0xeb, 0x04 } },                     /* jmp clamp tail */            \
    { UI, true, "titan: server browser clamp tail", (at) + 0x21u, 7,        \
      { 0xeb, 0x16, 0x83, 0xf8, 0x03, 0x75, 0x07 },                         \
      { 0x83, 0xf8, 0x05, 0x77, 0x23,       /* cmp eax,5; ja no column */   \
        0xeb, 0xda } }                      /* jmp push */

static const code_rule_t patch_titan_ui[] = {
    { UI, true, "titan: host-game menu name", UI_MENU_NAME, 6,
      { 0 }, { 'T', 'I', 'T', 'A', 'N', 0 } },
    { UI, true, "titan: vote menu name", UI_VOTE_NAME, 6,
      { 0 }, { 'T', 'i', 't', 'a', 'n', 0 } },

    /* The host-game difficulty dropdown counts its entries twice, for the
     * box and for its embedded list.
     */
    { UI, true, "titan: host-game menu entries", 0x15537, 10,
      { 0xc7, 0x05, B4(0x10092b00u), B4(5u) },
      { 0xc7, 0x05, B4(0x10092b00u), B4(6u) } },
    { UI, true, "titan: host-game menu list entries", 0x15513, 10,
      { 0xc7, 0x05, B4(0x10092f10u), B4(5u) },
      { 0xc7, 0x05, B4(0x10092f10u), B4(6u) } },
    /* Its initial selection, from sv_difficulty, is clamped to god. */
    { UI, true, "titan: host-game menu clamp test", 0x15a82, 3,
      { 0x83, 0xf8, 0x04 }, { 0x83, 0xf8, 0x05 } },
    { UI, true, "titan: host-game menu clamp value", 0x15a87, 10,
      { 0xc7, 0x05, B4(0x10092afcu), B4(4u) },
      { 0xc7, 0x05, B4(0x10092afcu), B4(5u) } },

    /* mov edx,5 is the vote dropdown's count for both box and list. */
    { UI, true, "titan: vote menu entries", 0x3f0ed, 5,
      { 0xba, B4(5u) }, { 0xba, B4(6u) } },

    /* The loading screen translates sv_difficulty "0".."4" through a chain
     * of inline strcmp loops into the buffer it shows; for any other value
     * the buffer keeps the map name. The "0" test, from mov ecx,"0" on, is
     * rewritten as two word compares against "0" and "5" (esi holds the
     * value). The "5" case repeats the easy case's pushes with the name in
     * place of "#easy", whose operand is the relocated address below, and
     * joins the easy case at its translate call. Those pass literal names
     * through unchanged. The mov ecx,"0" ahead keeps its relocation.
     */
    { UI, true, "titan: loading screen tests", 0xa7c7, 14,
      { 0x8a, 0x10, 0x3a, 0x11, 0x75, 0x1a, 0x84, 0xd2,
        0x74, 0x12, 0x8a, 0x50, 0x01, 0x3a },
      { 0x66, 0x81, 0x3e, '0', 0,           /* cmp word [esi],"0" */
        0x74, 0x22,                         /* je easy case */
        0x66, 0x81, 0x3e, '5', 0,           /* cmp word [esi],"5" */
        0x75, 0x39 } },                     /* jne "1" test */
    { UI, true, "titan: loading screen buffer", 0xa7d5, 10,
      { 0x51, 0x01, 0x75, 0x0e, 0x83, 0xc0, 0x02, 0x83, 0xc1, 0x02 },
      { 0x8d, 0x54, 0x24, 0x1c, 0x52,       /* lea edx,[esp+0x1c]; push */
        0x8d, 0x44, 0x24, 0x24, 0x50 } },   /* lea eax,[esp+0x24]; push */
    { UI, true, "titan: loading screen name push", 0xa7df, 8,
      { 0x84, 0xd2, 0x75, 0xe4, 0x33, 0xc0, 0xeb, 0x05 },
      { 0x6a, 0x40,                         /* push 0x40 */
        0x8d, 0x4c, 0x24, 0x44, 0x51,       /* lea ecx,[esp+0x44]; push */
        0x68 } },                           /* push "Titan" */
    { UI, true, "titan: loading screen translate", 0xa7eb, 5,
      { 0xff, 0x85, 0xc0, 0x75, 0x1e },
      { 0xeb, 0x19, 0x90, 0x90, 0x90 } },   /* jmp easy case's call */

    { UI, true, "titan: server browser short name", UI_BROWSER_TITAN, 6,
      { 0 }, { 'T', 'i', 't', 'a', 'n', 0 } },
    UI_BROWSER_LONG(0x323d1, 0x46, 0x50),
    UI_BROWSER_LONG(0x32581, 0x4e, 0x51),
    UI_BROWSER_SHORT(0x3273e),
    UI_BROWSER_SHORT(0x328cd),
};

/* Both name tables end in a zero dword without a relocation, which the
 * count-based dropdowns never read.
 */
static const address_rule_t patch_titan_ui_names[] = {
    { UI, "titan: host-game menu sixth entry", 0x6d0d8, 0, 0x10000000u + UI_MENU_NAME },
    { UI, "titan: vote menu sixth entry", 0x6e3ec, 0, 0x10000000u + UI_VOTE_NAME },
    /* The loading screen's push operand, inside the dropped strcmp loop. */
    { UI, "titan: loading screen name", 0xa7e7, 0xd883c01bu, 0x10000000u + UI_VOTE_NAME },
    /* The server browser's short names, indexed by level. */
    { UI, "titan: server browser easy", UI_BROWSER_NAMES + 0, 0, 0x100644c4u },
    { UI, "titan: server browser medium", UI_BROWSER_NAMES + 4, 0, 0x100644bcu },
    { UI, "titan: server browser hard", UI_BROWSER_NAMES + 8, 0, 0x100644b4u },
    { UI, "titan: server browser brute", UI_BROWSER_NAMES + 12, 0, 0x100644acu },
    { UI, "titan: server browser god", UI_BROWSER_NAMES + 16, 0, 0x100644a8u },
    { UI, "titan: server browser titan", UI_BROWSER_NAMES + 20, 0, 0x10000000u + UI_BROWSER_TITAN },
};

/* Replace lea scratch,[edi+edi*4]; lea dst,[scratch*4+0x200b6cf0] with the
 * vote's name for level edi, then continue at back.
 */
static void vote_name(payload_t *payload, uint32_t rva, unsigned dst, unsigned scratch,
                      uint32_t name, uint32_t back)
{
    uint32_t base = emit_anchor(payload, rva, dst);
    EMIT(0x83, 0xff, 5);
    uint32_t titan = emit_skip_if(payload, 0x74);
    EMIT(0x8d, (unsigned char)(0x04 | scratch << 3), 0xbf);
    EMIT(0x8d, (unsigned char)(0x84 | dst << 3), (unsigned char)(0x80 | scratch << 3 | dst),
         B4(0xb6cf0u - base));
    emit_jump(payload, rva, back);
    emit_land(payload, titan);
    EMIT(0x81, (unsigned char)(0xc0 + dst), B4(rva + name - base));
    emit_jump(payload, rva, back);
}

static bool build_game(payload_t *payload, uint32_t rva, uint32_t next, uint32_t *entry)
{
    if (!payload_reserve(payload, RESERVE, "Titan"))
        return false;
    *entry = emit_entry(payload, next);

    uint32_t skill_cfg = emit_string(payload, "exec skill-t.cfg\n");
    uint32_t waves = emit_string(payload, "scripts/waves/%s-t.xml");
    uint32_t name = emit_string(payload, "titan");

    /* Level 5 execs its cfg; 0-4 take the jump table, the rest easy. */
    uint32_t skill = (uint32_t)payload->size;
    EMIT(0x83, 0xfe, 5);
    uint32_t titan = emit_skip_if(payload, 0x74);
    EMIT(0x83, 0xfe, 4);
    emit_jump_if(payload, rva, 0x87, 0x24c1eu);  /* ja: mov eax,"exec skill-e.cfg\n" */
    emit_jump(payload, rva, 0x24bfbu);           /* jmp [esi*4+table] */
    emit_land(payload, titan);
    emit_load_address(payload, rva, EAX, skill_cfg);
    emit_jump(payload, rva, 0x24c23u);           /* trap_SendConsoleCommand */

    /* Push the map name, then this level's format instead of easy's. */
    uint32_t wave = (uint32_t)payload->size;
    EMIT(0x8d, 0x54, 0x24, 0x08, 0x52);     /* lea edx,[esp+8]; push edx */
    EMIT(0x83, 0xfe, 5);
    titan = emit_skip_if(payload, 0x74);
    emit_jump(payload, rva, 0x5f362u);           /* push "scripts/waves/%s-e.xml" */
    emit_land(payload, titan);
    emit_load_address(payload, rva, EAX, waves);
    EMIT(0x50);                             /* push eax */
    emit_jump(payload, rva, 0x5f367u);           /* Com_sprintf */

    uint32_t unchanged = (uint32_t)payload->size;
    vote_name(payload, rva, ECX, EAX, name, 0x1f387u);
    uint32_t message = (uint32_t)payload->size;
    vote_name(payload, rva, EAX, EDX, name, 0x1f3d4u);

    const uint32_t targets[] = { skill, wave, unchanged, message };
    _Static_assert(ARRAY_SIZE(targets) == ARRAY_SIZE(patch_titan_game_hooks),
                   "every GAME hook needs a helper");
    resolve_hooks(patch_titan_game_hooks, ARRAY_SIZE(targets), targets, rva);
    return true;
}

static bool build_cgame(payload_t *payload, uint32_t rva, uint32_t next, uint32_t *entry)
{
    if (!payload_reserve(payload, RESERVE, "Titan"))
        return false;
    *entry = emit_entry(payload, next);

    uint32_t skill_cfg = emit_string(payload, "exec skill-t.cfg\n");
    uint32_t name = emit_string(payload, "Titan");

    /* Level 5 execs its cfg; 3 takes brute's, the rest god's or easy's. */
    uint32_t skill = (uint32_t)payload->size;
    EMIT(0x83, 0xf8, 5);
    uint32_t titan = emit_skip_if(payload, 0x74);
    EMIT(0x83, 0xf8, 3);
    emit_jump_if(payload, rva, 0x85, 0x27d07u);  /* jne: cmp eax,4 */
    emit_jump(payload, rva, 0x27d00u);           /* mov eax,"exec skill-b.cfg\n" */
    emit_land(payload, titan);
    emit_load_address(payload, rva, EAX, skill_cfg);
    emit_jump(payload, rva, 0x27d16u);           /* trap_SendConsoleCommand */

    /* Call atoi with the caller's argument. For level 5, load the name
     * into edx and return past mov edx,[eax*4+names].
     */
    uint32_t scoreboard = (uint32_t)payload->size;
    EMIT(0xff, 0x74, 0x24, 0x04);           /* push [esp+4] */
    emit_call(payload, rva, 0x82e32u);           /* atoi */
    EMIT(0x83, 0xc4, 0x04, 0x83, 0xf8, 5);
    uint32_t other = emit_skip_if(payload, 0x75);
    emit_load_address(payload, rva, EDX, name);
    EMIT(0x83, 0x04, 0x24, 7);              /* add dword [esp],7 */
    emit_land(payload, other);
    EMIT(0xc3);

    const uint32_t targets[] = { skill, scoreboard };
    resolve_hooks(patch_titan_cgame_hooks, ARRAY_SIZE(targets), targets, rva);
    /* The connection screen's name slot holds an absolute address. */
    put32(patch_titan_cgame_hooks[2].replacement, 0x30000000u + rva + name);
    return true;
}

static const payload_feature_t titan_game = {
    GAME, "TITAN difficulty", build_game,
};
static const payload_feature_t titan_cgame = {
    CGAME, "TITAN difficulty", build_cgame,
};

/* The payloads and their hooks must be enabled together. */
REGISTER_CODE_RULES(patch_titan_game_hooks)
REGISTER_CODE_RULES(patch_titan_cgame_hooks)
REGISTER_CODE_RULES(patch_titan_levels)
REGISTER_CODE_RULES(patch_titan_ui)
REGISTER_ADDRESS_RULES(patch_titan_ui_names)
REGISTER_PAYLOAD_FEATURE(titan_game)
REGISTER_PAYLOAD_FEATURE(titan_cgame)
