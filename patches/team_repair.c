#include "patches.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Edit these settings and rebuild. Every toolkit hit on a living teammate
 * adds up to TEAM_REPAIR_AMMO_PER_HIT rounds to the clip of the weapon the
 * teammate is holding, never beyond its clip size and without touching the
 * teammate's reserve ammo. Each hit that adds at least one round costs the
 * repairing player TEAM_REPAIR_COST_PER_HIT money; a player with less
 * money than that cannot repair teammates. The toolkit hits every 150 ms
 * while the attack button stays down, about 6.7 hits per second.
 */
#define TEAM_REPAIR_AMMO_PER_HIT  1
#define TEAM_REPAIR_COST_PER_HIT  1

_Static_assert(TEAM_REPAIR_AMMO_PER_HIT >= 1 &&
               TEAM_REPAIR_AMMO_PER_HIT <= INT32_MAX,
               "Team repair must add at least one round per hit");
/* Money is kept within 0..9999 by G_AddMoney. */
_Static_assert(TEAM_REPAIR_COST_PER_HIT >= 0 &&
               TEAM_REPAIR_COST_PER_HIT <= 9999,
               "Team repair cost must be between 0 and 9999");

/* reigw: g_weapon.c Weapon_Toolkit_Fire (game 0x36f80). After its trace,
 * anything that is neither a stronghold nor a built structure (eTypes
 * 15 to 19) reaches `cmp ecx,0xf; jl 0x37551` at 0x373be and returns.
 * Players are ET_PLAYER (1), so redirect only that jl to the helper below
 * and keep the shared return at 0x37551 for everything it rejects.
 *
 * At the hook ECX is traceEnt->s.eType, ESI traceEnt and EDI the repairing
 * entity, whose client the function has already checked. EAX, ECX and EDX
 * are scratch; EBX (the upgrade bit) is restored by the epilogue's pop,
 * which, like the security-cookie check, only depends on a balanced ESP.
 *
 * gentity_t: client 0x28c, health 0x36c. gclient_t starts with its
 * playerState_t: eFlags 0x68, weapon 0x90, money persistant[9] 0x11c
 * (G_AddMoney, 0x44070), clip[] 0x1e4 (PM_FillClip, 0x13300). The
 * session team is at 0x874. weaponScript_t records are 0x2848 bytes, and
 * weapon 0's clipsize is at RVA 0x140f8b4 (PM_FillClip's 0x2140f8b4).
 * Bit 8 of the repairer's ps.eFlags is the "working" mark the function
 * clears on every hit and sets while repairing structures; set it too.
 */
enum {
    TOOLKIT_RETURN = 0x37551u,
    TOOLKIT_HOOK = 0x373beu,
    CLIPSIZE_TABLE = 0x140f8b4u,
};

/* One helper instruction, or a group that must stay together. A BRANCH's
 * rel32 is its last four bytes. ANCHOR is `call $+5; pop ebx`, leaving
 * the runtime address of its pop in EBX, so a later FROM_ANCHOR operand
 * at offset `at` can address retail data without a new PE relocation.
 */
typedef enum { PLAIN, BRANCH, ANCHOR, FROM_ANCHOR } op_kind_t;
typedef struct {
    op_kind_t kind;
    unsigned char size, at;
    unsigned char bytes[10];
    uint32_t target;
    bool terminal;
} op_t;

#define OP(...) { PLAIN, sizeof((unsigned char[]){ __VA_ARGS__ }), 0, \
                  { __VA_ARGS__ }, 0, false }
#define JCC(cc) { BRANCH, 6, 0, { 0x0f, cc }, TOOLKIT_RETURN, false }
#define JNE JCC(0x85)
#define JE  JCC(0x84)
#define JL  JCC(0x8c)
#define JLE JCC(0x8e)

static const op_t team_repair_ops[] = {
    OP(0x83, 0xf9, 0x01),                           /* cmp ecx,ET_PLAYER */
    JNE,
    OP(0x8b, 0x86, B4(0x28c)),                      /* eax=target client */
    OP(0x85, 0xc0),                                 /* test eax,eax */
    JE,
    OP(0x8b, 0x97, B4(0x28c)),                      /* edx=repairer client */
    OP(0x8b, 0x88, B4(0x874)),                      /* ecx=target team */
    OP(0x3b, 0x8a, B4(0x874)),                      /* same team? */
    JNE,
    OP(0x83, 0xbe, B4(0x36c), 0x00),                /* target alive? */
    JLE,
    OP(0x81, 0xba, B4(0x11c),                       /* money >= cost? */
       B4((uint32_t)TEAM_REPAIR_COST_PER_HIT)),
    JL,
    OP(0x8b, 0x88, B4(0x90)),                       /* ecx=target weapon */
    OP(0x8d, 0x84, 0x88, B4(0x1e4)),                /* eax=&clip[weapon] */
    OP(0x69, 0xc9, B4(0x2848)),                     /* ecx=script offset */
    { ANCHOR, 6, 0, { 0xe8, 0, 0, 0, 0, 0x5b }, 0, false },
    { FROM_ANCHOR, 7, 3, { 0x8b, 0x8c, 0x0b },      /* ecx=clipsize */
      CLIPSIZE_TABLE, false },
    OP(0x2b, 0x08),                                 /* ecx-=clip */
    JLE,                                            /* full or no clip */
    OP(0xbb, B4((uint32_t)TEAM_REPAIR_AMMO_PER_HIT)), /* ebx=ammo per hit */
    OP(0x3b, 0xcb),                                 /* cmp ecx,ebx */
    OP(0x0f, 0x4f, 0xcb),                           /* cmovg ecx,ebx */
    OP(0x01, 0x08),                                 /* clip+=ecx */
    OP(0x81, 0xaa, B4(0x11c),                       /* money-=cost */
       B4((uint32_t)TEAM_REPAIR_COST_PER_HIT)),
    OP(0x83, 0x4a, 0x68, 0x08),                     /* ps.eFlags|=8 */
    { BRANCH, 5, 0, { 0xe9 }, TOOLKIT_RETURN, true },
};

/* Unreachable 15-byte int3 runs between functions, each directly after a
 * ret and ending at the next function's 16-byte alignment. The helper is
 * packed into them in order, chained with jmp rel32; relative branches
 * and the call/pop base need no new relocations. Unused slots stay off.
 */
#define CC15 { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, \
               0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc }
#define SLOT(rva) { GAME, true, "team repair: helper fragment", rva, 15, CC15, CC15 }

static code_rule_t patch_team_repair_helper[] = {
    SLOT(0x38471u), SLOT(0x39c31u), SLOT(0x3a6b1u), SLOT(0x3be51u),
    SLOT(0x400e1u), SLOT(0x41041u), SLOT(0x41aa1u), SLOT(0x41eb1u),
    SLOT(0x42571u), SLOT(0x43071u), SLOT(0x43a61u), SLOT(0x444f1u),
    SLOT(0x44961u), SLOT(0x44af1u), SLOT(0x45541u), SLOT(0x45581u),
    SLOT(0x464b1u), SLOT(0x464e1u), SLOT(0x46941u), SLOT(0x46bb1u),
    SLOT(0x47301u),
};

/* Keep the hook and all helper fragments enabled together. */
static const code_rule_t patch_team_repair_hook[] = {
    { GAME, true, "team repair: toolkit hits players", TOOLKIT_HOOK, 9,
      { 0x83, 0xf9, 0x0f, 0x0f, 0x8c, B4(TOOLKIT_RETURN - (TOOLKIT_HOOK + 9u)) },
      { 0x83, 0xf9, 0x0f, 0x0f, 0x8c, B4(0x38471u - (TOOLKIT_HOOK + 9u)) } },
};

PATCH_CONSTRUCTOR(pack_team_repair_helper)
{
    enum { JMP_SIZE = 5 };
    size_t slot = 0, pos = 0;
    uint32_t anchor = 0;

    for (size_t i = 0; i < ARRAY_SIZE(team_repair_ops); ++i) {
        const op_t *op = &team_repair_ops[i];
        size_t need = op->size + (op->terminal ? 0 : JMP_SIZE);

        /* Every non-terminal placement leaves room for the chaining jmp. */
        if (pos + need > patch_team_repair_helper[slot].size) {
            code_rule_t *from = &patch_team_repair_helper[slot];
            if (++slot == ARRAY_SIZE(patch_team_repair_helper)) {
                fprintf(stderr, "team_repair.c: helper exceeds its padding slots\n");
                exit(EXIT_FAILURE);
            }
            from->replacement[pos] = 0xe9;
            put32(from->replacement + pos + 1,
                  patch_team_repair_helper[slot].rva - (from->rva + (uint32_t)pos + JMP_SIZE));
            pos = 0;
        }

        code_rule_t *rule = &patch_team_repair_helper[slot];
        uint32_t rva = rule->rva + (uint32_t)pos;
        unsigned char *out = rule->replacement + pos;
        memcpy(out, op->bytes, op->size);
        switch (op->kind) {
        case BRANCH:
            put32(out + op->size - 4, op->target - (rva + op->size));
            break;
        case ANCHOR:
            anchor = rva + 5;
            break;
        case FROM_ANCHOR:
            put32(out + op->at, op->target - anchor);
            break;
        case PLAIN:
            break;
        }
        pos += op->size;
    }

    /* The remaining int3 bytes of the last slot are left unchanged. */
    for (size_t i = slot + 1; i < ARRAY_SIZE(patch_team_repair_helper); ++i)
        patch_team_repair_helper[i].enabled = false;
}

REGISTER_CODE_RULES(patch_team_repair_helper)
REGISTER_CODE_RULES(patch_team_repair_hook)
