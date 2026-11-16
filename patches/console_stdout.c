#include "patches.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Mirror every console line of igwarlord.exe to the process's stdout.
 *
 * Com_Printf (RVA 0x204e0) passes each message to Conbuf_AppendText
 * (RVA 0x4dfc0, message in ECX), as do Sys_Error and the console window's
 * input line echo. That function strips color codes, turns line endings
 * into CRLF, writes the result into the 32 KiB stack buffer at ESP + 0x18
 * and appends it to the dedicated console window with SendMessageA. Only
 * Com_Printf's rcon redirection bypasses it.
 *
 * Hook the function's last instruction before its epilogue (RVA 0x4e147),
 * "mov ecx,[esp+0x8018]", which loads the stack cookie. At that point the
 * window has received the text, so the buffer may be modified, and the
 * epilogue restores EBX, ESI, EDI and EBP, which leaves every general
 * purpose register free. The helper:
 *   - drops each CR from the NUL-terminated buffer in place, so stdout
 *     gets plain LF line endings,
 *   - WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buffer, length, &written,
 *     NULL), unbuffered so the output appears immediately,
 *   - performs the displaced cookie load and returns.
 * Without an inherited stdout, GetStdHandle yields NULL or an invalid
 * handle and WriteFile fails without side effects.
 *
 * The imports are called through the existing IAT slots. call/pop obtains
 * the helper's runtime address, so the IAT displacements are relative and
 * no new relocations are needed even if the image is rebased.
 *
 * No int3 padding slot fits the ~60 byte helper, so whole instructions
 * are distributed over guarded 15-byte function-alignment padding slots
 * chained with jmp rel32. Keep the slots and the hook enabled together.
 */
#define CONSOLE_STDOUT_HOOK      0x4e147u
#define CONSOLE_STDOUT_ENTRY     0x4db61u
#define IAT_GET_STD_HANDLE       0x1aa208u
#define IAT_WRITE_FILE           0x1aa154u

typedef enum { PLAIN, BASE, IAT } console_fixup_t;

typedef struct {
    unsigned char bytes[10];
    size_t size;
    /* BASE marks call $+5; pop ebx, which yields the pop's runtime address.
     * IAT ends in a disp32 from that address to the IAT slot in target.
     */
    console_fixup_t fixup;
    uint32_t target;
} console_insn_t;

/* The stack offsets account for the hook's return address on the stack. */
static const console_insn_t console_stdout_insns[] = {
    { { 0x8d, 0x74, 0x24, 0x1c }, 4, PLAIN, 0 },   /* lea esi,[esp+0x1c] */
    { { 0x89, 0xf7 }, 2, PLAIN, 0 },               /* mov edi,esi */
    /* Compact in place, skipping CR; ends with EDI one past the NUL.
     * The short branches are relative to this unit, which stays whole.
     */
    { { 0xac,                                      /* lodsb */
        0x3c, 0x0d,                                /* cmp al,0x0d */
        0x74, 0xfb,                                /* je lodsb */
        0xaa,                                      /* stosb */
        0x84, 0xc0,                                /* test al,al */
        0x75, 0xf6 }, 10, PLAIN, 0 },              /* jne lodsb */
    { { 0x8d, 0x44, 0x24, 0x1c }, 4, PLAIN, 0 },   /* lea eax,[esp+0x1c] */
    { { 0x4f }, 1, PLAIN, 0 },                     /* dec edi */
    { { 0x29, 0xc7 }, 2, PLAIN, 0 },               /* sub edi,eax: length */
    { { 0x6a, 0x00 }, 2, PLAIN, 0 },               /* push 0: written */
    { { 0x89, 0xe1 }, 2, PLAIN, 0 },               /* mov ecx,esp */
    { { 0x6a, 0x00 }, 2, PLAIN, 0 },               /* push 0: lpOverlapped */
    { { 0x51 }, 1, PLAIN, 0 },                     /* push ecx: &written */
    { { 0x57 }, 1, PLAIN, 0 },                     /* push edi: length */
    { { 0x50 }, 1, PLAIN, 0 },                     /* push eax: buffer */
    { { 0x6a, 0xf5 }, 2, PLAIN, 0 },               /* push STD_OUTPUT_HANDLE */
    { { 0xe8, 0, 0, 0, 0, 0x5b }, 6, BASE, 0 },    /* call $+5; pop ebx */
    { { 0xff, 0x93 }, 6, IAT, IAT_GET_STD_HANDLE },/* call [GetStdHandle] */
    { { 0x50 }, 1, PLAIN, 0 },                     /* push eax: handle */
    { { 0xff, 0x93 }, 6, IAT, IAT_WRITE_FILE },    /* call [WriteFile] */
    { { 0x58 }, 1, PLAIN, 0 },                     /* pop eax: drop written */
    /* Displaced instruction: mov ecx,[esp+0x8018] from the hook's frame. */
    { { 0x8b, 0x8c, 0x24, B4(0x801cu) }, 7, PLAIN, 0 },
    { { 0xc3 }, 1, PLAIN, 0 },                     /* ret */
};

/* Padding between functions in the win32 and qcommon code, verified
 * against the function boundaries. The first slot is the helper's entry.
 */
static const uint32_t console_stdout_slots[] = {
    CONSOLE_STDOUT_ENTRY, 0x4ca11u, 0x4c351u, 0x4bd01u,
    0x49a81u, 0x499b1u, 0x48ec1u, 0x462c1u,
};
#define CONSOLE_SLOT_SIZE 15

static code_rule_t patch_console_stdout_helper[ARRAY_SIZE(console_stdout_slots)];

static const code_rule_t patch_console_stdout_hook = {
    ENGINE, true, "console stdout: hook Conbuf_AppendText", CONSOLE_STDOUT_HOOK, 7,
    { 0x8b, 0x8c, 0x24, B4(0x8018u) },
    { 0xe8, B4(CONSOLE_STDOUT_ENTRY - (CONSOLE_STDOUT_HOOK + 5u)), 0x90, 0x90 },
};

/* Place the instructions in order, keeping room for the chaining jmp in
 * every slot but the last. Each rule guards its complete int3 slot and
 * leaves the unused tail as int3. Every listed slot must end up used, so
 * a stale slot list fails loudly instead of silently registering rules.
 */
PATCH_CONSTRUCTOR(pack_console_stdout)
{
    const size_t slot_count = ARRAY_SIZE(console_stdout_slots);
    const size_t insn_count = ARRAY_SIZE(console_stdout_insns);
    uint32_t base = 0;
    size_t slot = 0, used = 0;

    for (size_t s = 0; s < slot_count; ++s) {
        code_rule_t *rule = &patch_console_stdout_helper[s];
        *rule = (code_rule_t){ ENGINE, true, "console stdout: helper fragment",
                               console_stdout_slots[s], CONSOLE_SLOT_SIZE, { 0 }, { 0 } };
        memset(rule->expected, 0xcc, CONSOLE_SLOT_SIZE);
        memset(rule->replacement, 0xcc, CONSOLE_SLOT_SIZE);
    }
    for (size_t i = 0; i < insn_count; ++i) {
        const console_insn_t *insn = &console_stdout_insns[i];
        size_t reserve = i + 1 < insn_count ? 5 : 0;
        unsigned char *out;
        uint32_t at;

        if (used + insn->size + reserve > CONSOLE_SLOT_SIZE) {
            at = console_stdout_slots[slot] + (uint32_t)used;
            if (slot + 1 == slot_count)
                break;
            patch_console_stdout_helper[slot].replacement[used] = 0xe9;
            put32(patch_console_stdout_helper[slot].replacement + used + 1,
                  console_stdout_slots[slot + 1] - (at + 5u));
            ++slot;
            used = 0;
        }
        at = console_stdout_slots[slot] + (uint32_t)used;
        out = patch_console_stdout_helper[slot].replacement + used;
        memcpy(out, insn->bytes, insn->size);
        used += insn->size;
        if (insn->fixup == BASE)
            base = at + 5u;
        else if (insn->fixup == IAT)
            put32(out + insn->size - 4, insn->target - base);
        if (i + 1 == insn_count && slot + 1 == slot_count)
            return;
    }
    fputs("console stdout: the padding slots do not fit the helper exactly\n", stderr);
    exit(EXIT_FAILURE);
}

REGISTER_CODE_RULES(patch_console_stdout_helper)
REGISTER_CODE_RULE(patch_console_stdout_hook)
