#include "patches.h"

#include <stdio.h>
#include <string.h>

/* The host-game menu's help box, at its bottom left, describes the
 * focused control. For the "Game mode:" dropdown it only said "Choose the
 * game mode.", whatever the mode; it now shows the text of the selected
 * one, which changes as the selection does, also while the opened list
 * follows the mouse (popup_hover.c).
 *
 * Edit the texts below. Each entry belongs to the dropdown entry at the
 * same index: VANILLA, RESISTANCE and DEATHMATCH, in the order that
 * game_mode_resistance.c gives the dropdown. A "\n" starts a new line;
 * the box also wraps long lines at word boundaries by itself. The box does
 * not grow, so only about five lines fit in it. Like retail's help texts,
 * a text starting with a single '#' is a key into the language files
 * in scripts/languages/ instead, and '|' breaks lines as well.
 */
static const char *const game_mode_hints[] = {
    /* VANILLA */
    "Vanilla - The original cooperative game against the computer.\n"
    "Difficulty, diff multiplier and friendly fire can be chosen freely.",

    /* RESISTANCE */
    "Resistance - A cooperative game with fixed rules.\n"
    "God difficulty, no diff multiplier and no friendly fire.\n"
    "Each map needs its own Resistance wave script.",

    /* DEATHMATCH */
    "Deathmatch - Players fight each other.",
};

/* The draw copies the text into a buffer of this many bytes. */
#define HINT_MAX 1023u

/* The help box's text draw breaks lines at '|' instead of '\n'. */
#define LINE_BREAK '|'

/* -- uix86.dll --------------------------------------------------------- */

/* The menu's status callback (RVA 0x11e20) switches over the focused
 * item's id through a jump table at RVA 0x1205c. The "Game mode:"
 * dropdown's case, the table's last slot, pushes "Choose the game mode."
 * at RVA 0x11f80 and Com_sprintf()s it into a 256-byte buffer, which the
 * callback draws after the box from RVA 0x11f9c on, its text pointer
 * pushed at RVA 0x12030.
 */
#define UI_HINT_CASE 0x11f80u
#define UI_HINT_DRAWN 0x11f9cu
#define UI_HINT_PUSH 0x12030u
/* The "Game mode:" dropdown's selected index. */
#define UI_GAME_MODE 0x93184u

/* The jump table's slot of the "Game mode:" case, relocated: it points to
 * the payload's case instead, at an address of the same image base, so
 * the relocation stays valid. Its replacement is set once the payload has
 * a known RVA.
 */
static code_rule_t patch_game_mode_hint_case[] = {
    { UI, true, "game mode hint: status case", 0x12088, 4,
      { B4(0x10000000u + UI_HINT_CASE) }, { 0 } },
};

/* lea edx,[esp+0x20]; push edx: the draw's text argument, the buffer. */
static code_rule_t patch_game_mode_hint_hooks[] = {
    { UI, true, "game mode hint: status text", UI_HINT_PUSH, 5,
      { 0x8d, 0x54, 0x24, 0x20, 0x52 }, { 0xe9 } },
};

static bool build_ui(payload_t *payload, uint32_t rva, uint32_t next, uint32_t *entry)
{
    /* The texts, their table and the pending text, then about 100 bytes
     * of code.
     */
    size_t reserve = 128u + 4u * ARRAY_SIZE(game_mode_hints);
    for (size_t i = 0; i < ARRAY_SIZE(game_mode_hints); ++i)
        reserve += strlen(game_mode_hints[i]) + 1u;
    if (!payload_reserve(payload, reserve, "Game mode hint"))
        return false;
    *entry = emit_entry(payload, next);

    /* Each text with its line breaks as the draw expects them. */
    uint32_t texts[ARRAY_SIZE(game_mode_hints)];
    for (size_t i = 0; i < ARRAY_SIZE(game_mode_hints); ++i) {
        char text[HINT_MAX + 1u];
        size_t length = strlen(game_mode_hints[i]);
        if (length > HINT_MAX) {
            fprintf(stderr, "Game mode hint %zu: longer than %u bytes\n", i, HINT_MAX);
            payload->failed = true;
            return false;
        }
        for (size_t j = 0; j <= length; ++j)
            text[j] = game_mode_hints[i][j] == '\n' ? LINE_BREAK : game_mode_hints[i][j];
        texts[i] = rva + emit_string(payload, text);
    }

    /* The texts' RVAs by dropdown index, and the runtime address of the
     * text the next draw takes instead of the buffer, 0 for the buffer.
     */
    while (payload->size & 3)
        EMIT(0);
    uint32_t table = rva + (uint32_t)payload->size;
    for (size_t i = 0; i < ARRAY_SIZE(texts); ++i)
        EMIT(B4(texts[i]));
    uint32_t pending = rva + EMIT(B4(0u));

    /* The "Game mode:" case: make the selected mode's text pending and
     * skip the Com_sprintf(), on to the box and the draw. An index without
     * a text keeps the retail case. eax, ecx and edx are free here, esi and
     * edi hold the box's place.
     */
    uint32_t pick = (uint32_t)payload->size;
    uint32_t base = emit_anchor(payload, rva, EAX);
    EMIT(0x8b, 0x88, B4(UI_GAME_MODE - base));      /* mov ecx,[eax+d] */
    EMIT(0x83, 0xf9, (unsigned char)(ARRAY_SIZE(texts) - 1u)); /* cmp ecx,last */
    emit_jump_if(payload, rva, 0x87, UI_HINT_CASE); /* ja: retail */
    EMIT(0x8b, 0x8c, 0x88, B4(table - base));       /* mov ecx,[eax+ecx*4+d]: RVA */
    EMIT(0x8d, 0x8c, 0x08, B4(0u - base));          /* lea ecx,[eax+ecx-base] */
    EMIT(0x89, 0x88, B4(pending - base));           /* mov [eax+d],ecx */
    emit_jump(payload, rva, UI_HINT_DRAWN);

    /* The draw's text argument: the pending text once, else the buffer as
     * retail pushes it. eax and edx are free here, the anchor's call and
     * pop leave esp as it was.
     */
    uint32_t push = (uint32_t)payload->size;
    base = emit_anchor(payload, rva, EDX);
    EMIT(0x8b, 0x82, B4(pending - base));           /* mov eax,[edx+d] */
    EMIT(0x85, 0xc0);                               /* test eax,eax */
    uint32_t buffer = emit_skip_if(payload, 0x74);  /* jz */
    EMIT(0xc7, 0x82, B4(pending - base), B4(0u));   /* mov dword [edx+d],0 */
    EMIT(0x50);                                     /* push eax */
    emit_jump(payload, rva, UI_HINT_PUSH + 5u);
    emit_land(payload, buffer);
    EMIT(0x8d, 0x54, 0x24, 0x20, 0x52);             /* replayed */
    emit_jump(payload, rva, UI_HINT_PUSH + 5u);

    const uint32_t targets[] = { push };
    _Static_assert(ARRAY_SIZE(targets) == ARRAY_SIZE(patch_game_mode_hint_hooks),
                   "every hook needs a helper");
    resolve_hooks(patch_game_mode_hint_hooks, ARRAY_SIZE(targets), targets, rva);
    put32(patch_game_mode_hint_case[0].replacement, 0x10000000u + rva + pick);
    return true;
}

static const payload_feature_t game_mode_hint_ui = { UI, "Game mode hint", build_ui };

/* The payload and its hooks must be enabled together. */
REGISTER_CODE_RULES(patch_game_mode_hint_case)
REGISTER_CODE_RULES(patch_game_mode_hint_hooks)
REGISTER_PAYLOAD_FEATURE(game_mode_hint_ui)
