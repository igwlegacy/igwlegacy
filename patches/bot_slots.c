#include "patches.h"

/* Keep bots out of the human client slots by handing them the highest
 * free slot instead of the lowest one.
 *
 * SV_DirectConnect (igwarlord.exe RVA 0x38180) only lets humans into the
 * first min(sv_maxclients, sv_humanClients) slots and takes the last free
 * one of those. When none is free, it drops the first bot it finds in that
 * range (SV_DropClient) and gives its slot to the connecting player. The
 * start menu's StartServer_Start sets sv_humanClients to 1 for a local
 * game, which leaves slot 0 as the only human slot.
 *
 * SV_BotAllocateClient (RVA 0x35df0) scans upwards from slot 0, so a bot
 * that G_CheckMinimumPlayers adds before the local client has connected
 * takes slot 0. The player then replaces that bot and ends up stuck as a
 * spectator: "Hit Enter to start" is shown, but no class menu ever opens,
 * so pickinfo is never sent and the player never spawns until a map
 * restart.
 *
 * Replace the upward scan with a downward one from sv_maxclients - 1.
 * EDX holds sv_maxclients->integer and ECX svs.clients on entry. The loop
 * leaves EAX at the slot and ECX at its client_t, which is what the
 * unchanged code at RVA 0x35e1d expects (it reloads EDX). When no slot is
 * free, EAX runs down to -1 and the loop exits through the existing ret
 * at RVA 0x35e1c, which returns the same -1 as the original "or eax,-1".
 * Slot 0 is still used once every other slot is taken.
 */
#define CLIENT_T_SIZE 0x1fca0u

/* The 29 bytes exceed PATCH_MAX, so they are split at the new code's
 * instruction boundary at RVA 0x35e0e, which falls inside the original
 * "add ecx,sizeof(client_t)". Keep both rules enabled together.
 */
static const code_rule_t patch_bot_slots[] = {
    { ENGINE, true, "allocate bot slots from the top (1/2)", 0x35dff, 15,
      { 0x33, 0xc0,                         /* xor eax,eax */
        0x85, 0xd2,                         /* test edx,edx */
        0x7e, 0x10,                         /* jle 0x35e15 */
        0x83, 0x39, 0x00,                   /* cmp dword [ecx],0 */
        0x74, 0x0b,                         /* je 0x35e15 */
        0x40,                               /* inc eax */
        0x81, 0xc1, 0xa0 },                 /* add ecx,0x1fca0 (first part) */
      { 0x8d, 0x42, 0xff,                   /* lea eax,[edx-1] */
        0x69, 0xd0, B4(CLIENT_T_SIZE),      /* imul edx,eax,sizeof(client_t) */
        0x03, 0xca,                         /* add ecx,edx */
        0x85, 0xc0,                         /* loop: test eax,eax */
        0x7c, 0x0e } },                     /* jl 0x35e1c (ret, EAX = -1) */
    { ENGINE, true, "allocate bot slots from the top (2/2)", 0x35e0e, 14,
      { 0xfc, 0x01, 0x00,                   /* add ecx,0x1fca0 (rest) */
        0x3b, 0xc2,                         /* cmp eax,edx */
        0x7c, 0xf0,                         /* jl 0x35e05 */
        0x3b, 0xc2,                         /* cmp eax,edx */
        0x75, 0x04,                         /* jne 0x35e1d */
        0x83, 0xc8, 0xff },                 /* or eax,-1 */
      { 0x83, 0x39, 0x00,                   /* cmp dword [ecx],0 (CS_FREE) */
        0x74, 0x0a,                         /* je 0x35e1d (found) */
        0x48,                               /* dec eax */
        0x81, 0xe9, B4(CLIENT_T_SIZE),      /* sub ecx,sizeof(client_t) */
        0xeb, 0xee } },                     /* jmp loop (RVA 0x35e0a) */
};

REGISTER_CODE_RULES(patch_bot_slots)
