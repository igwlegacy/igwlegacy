#include "patcher.h"

#include <stdio.h>
#include <string.h>

/* Emit i386 instructions as data, so a native C compiler on any host can
 * produce the Windows DLL payload. Every address is relative to EBX, the
 * section's runtime base from call/pop. No new PE relocations are needed.
 */
enum { DISPATCH, REGISTER, UPDATE, SET, PASS, EQUALS, LABEL_COUNT };
typedef struct { size_t at; unsigned label; } fixup_t;
typedef struct {
    payload_t *payload;
    bool ok;
    size_t labels[LABEL_COUNT];
    fixup_t fixups[2048];
    size_t fixup_count;
} emitter_t;
typedef struct { uint32_t name, value, handle; } record_t;

static size_t emit(emitter_t *e, const void *data, size_t size)
{
    size_t at = e->payload->size;
    if (!e->ok || size > PAYLOAD_MAX - at) {
        e->ok = false;
        return 0;
    }
    memcpy(e->payload->data + at, data, size);
    e->payload->size += size;
    return at;
}

#define BYTES(e, ...) emit(e, (const unsigned char[]){ __VA_ARGS__ }, \
                          sizeof((const unsigned char[]){ __VA_ARGS__ }))

static size_t word(emitter_t *e, uint32_t value)
{
    unsigned char data[4];
    put32(data, value);
    return emit(e, data, sizeof(data));
}

/* A fixup points to the displacement following a branch or call opcode. */
static void link_to(emitter_t *e, size_t at, size_t target)
{
    if (e->ok && at + 4 <= e->payload->size)
        put32(e->payload->data + at, (uint32_t)target - (uint32_t)(at + 4));
    else
        e->ok = false;
}

static void reference(emitter_t *e, unsigned label)
{
    size_t at = word(e, 0);
    if (e->fixup_count == ARRAY_SIZE(e->fixups)) {
        e->ok = false;
        return;
    }
    e->fixups[e->fixup_count++] = (fixup_t){ at, label };
}

static void jump(emitter_t *e, unsigned label)
{
    BYTES(e, 0xe9); /* jmp rel32 */
    reference(e, label);
}

static void equal_jump(emitter_t *e, unsigned label)
{
    BYTES(e, 0x0f, 0x84); /* je rel32 */
    reference(e, label);
}

/* call next; pop ebx; sub ebx, offset-of-next -> section runtime base. */
static void section_base(emitter_t *e)
{
    BYTES(e, 0xe8, 0, 0, 0, 0);
    uint32_t next = (uint32_t)e->payload->size;
    BYTES(e, 0x5b, 0x81, 0xeb);
    word(e, next);
}

static void address_eax(emitter_t *e, uint32_t offset)
{
    BYTES(e, 0x8d, 0x83); /* lea eax, [ebx+offset] */
    word(e, offset);
}

static void restore(emitter_t *e)
{
    BYTES(e, 0x5f, 0x5e, 0x5b, 0xc9); /* pop edi, esi, ebx; leave */
}

/* Bypass our dispatcher to avoid recursion. The engine's SET syscall forces
 * the value even when the CVAR was archived, latched, cheat protected or ROM.
 */
static void force_value(emitter_t *e, const profile_t *p, const record_t *r)
{
    address_eax(e, r->value);
    BYTES(e, 0x50); /* push value */
    address_eax(e, r->name);
    BYTES(e, 0x50, 0x68); /* push name; push SET */
    word(e, p->cvar_set);
    BYTES(e, 0xff, 0x93, 8, 0, 0, 0, 0x83, 0xc4, 12); /* call [ebx+8]; add esp,12 */
}

/* Compare a caller's name to a rule. The helper preserves nonvolatile regs. */
static size_t match_name(emitter_t *e, uint8_t stack_offset, uint32_t name)
{
    BYTES(e, 0x8b, 0x45, stack_offset, 0x8d, 0x93); /* eax=arg; lea edx,[ebx+name] */
    word(e, name);
    BYTES(e, 0xe8);
    reference(e, EQUALS);
    BYTES(e, 0x85, 0xc0, 0x0f, 0x84); /* test eax,eax; jz next rule */
    return word(e, 0);
}

static void emit_equals(emitter_t *e)
{
    e->labels[EQUALS] = e->payload->size;
    BYTES(e, 0x56, 0x57, 0x89, 0xc6, 0x89, 0xd7); /* save esi/edi; esi=eax; edi=edx */
    BYTES(e, 0x85, 0xc0, 0x0f, 0x84);
    size_t null_a = word(e, 0);
    BYTES(e, 0x85, 0xd2, 0x0f, 0x84);
    size_t null_b = word(e, 0);
    size_t loop = e->payload->size;
    BYTES(e, 0x8a, 0x06, 0x8a, 0x17); /* al=[esi]; dl=[edi] */
    /* Fold only ASCII A..Z; punctuation must still compare exactly. */
    BYTES(e, 0x3c, 'A', 0x72, 6, 0x3c, 'Z', 0x77, 2, 0x04, 32);
    BYTES(e, 0x80, 0xfa, 'A', 0x72, 8, 0x80, 0xfa, 'Z', 0x77, 3, 0x80, 0xc2, 32);
    BYTES(e, 0x38, 0xd0, 0x0f, 0x85); /* cmp al,dl; jne unequal */
    size_t unequal = word(e, 0);
    BYTES(e, 0x46, 0x47, 0x84, 0xc0, 0x0f, 0x85); /* inc esi/edi; test al,al; jne loop */
    size_t again = word(e, 0);
    link_to(e, again, loop);
    BYTES(e, 0xb8, 1, 0, 0, 0, 0x5f, 0x5e, 0xc3); /* return 1 */
    link_to(e, null_a, e->payload->size);
    link_to(e, null_b, e->payload->size);
    link_to(e, unequal, e->payload->size);
    BYTES(e, 0x31, 0xc0, 0x5f, 0x5e, 0xc3); /* return 0 */
}

bool payload_build(payload_t *payload, const profile_t *p, uint32_t rva)
{
    record_t records[128] = {0};
    emitter_t e = { .payload = payload, .ok = true };
    memset(payload, 0, sizeof(*payload));
    if (cvar_rule_count > ARRAY_SIZE(records)) {
        fprintf(stderr, "At most %zu CVAR rules are supported\n", ARRAY_SIZE(records));
        return false;
    }
    emit(&e, "IGWMONK1", 8);
    word(&e, 0); /* The engine syscall pointer, filled by dllEntry. */
    for (size_t i = 0; i < cvar_rule_count; ++i) {
        const cvar_rule_t *rule = cvar_rules[i];
        if (rule->module != p->module)
            continue;
        records[i].name = (uint32_t)emit(&e, rule->name, strlen(rule->name) + 1);
        records[i].value = (uint32_t)emit(&e, rule->value, strlen(rule->value) + 1);
        records[i].handle = (uint32_t)word(&e, UINT32_MAX);
    }

    /* Redirect only the exported dllEntry RVA. The original instructions and
     * all their relocations stay intact. Store our dispatcher in the original
     * syscall global, which every trap wrapper already calls through.
     */
    payload->entry = (uint32_t)payload->size;
    BYTES(&e, 0x53); /* push ebx */
    section_base(&e);
    BYTES(&e, 0x8b, 0x44, 0x24, 8, 0x89, 0x83, 8, 0, 0, 0); /* save engine pointer */
    BYTES(&e, 0x8d, 0x83); /* lea eax,[ebx+dispatcher] */
    size_t dispatcher_address = word(&e, 0);
    BYTES(&e, 0x89, 0x83); /* mov [ebx+original syscall global],eax */
    word(&e, p->syscall_rva - rva);
    BYTES(&e, 0x5b, 0xc3); /* pop ebx; ret */

    e.labels[DISPATCH] = payload->size;
    put32(payload->data + dispatcher_address, (uint32_t)payload->size);
    BYTES(&e, 0x55, 0x89, 0xe5, 0x53, 0x56, 0x57); /* frame; save ebx,esi,edi */
    section_base(&e);
    BYTES(&e, 0x8b, 0x45, 8); /* eax=syscall number */
    const uint32_t calls[] = { p->cvar_register, p->cvar_update, p->cvar_set };
    const unsigned destinations[] = { REGISTER, UPDATE, SET };
    for (size_t i = 0; i < ARRAY_SIZE(calls); ++i) {
        BYTES(&e, 0x3d); /* cmp eax, syscall */
        word(&e, calls[i]);
        equal_jump(&e, destinations[i]);
    }
    jump(&e, PASS);

    e.labels[REGISTER] = payload->size;
    for (size_t i = 0; i < cvar_rule_count; ++i) {
        const cvar_rule_t *rule = cvar_rules[i];
        const record_t *r = &records[i];
        if (rule->module != p->module)
            continue;
        size_t next = match_name(&e, 16, r->name);
        address_eax(&e, r->value);
        BYTES(&e, 0x89, 0x45, 20); /* replace registration default argument */
        if (rule->mode == CVAR_DEFAULT) {
            jump(&e, PASS);
        } else {
            force_value(&e, p, r);
            BYTES(&e, 0x83, 0x4d, 24, 64); /* flags |= CVAR_ROM */
            /* Forward registration, then remember its engine handle. Matching
             * handles covers multiple vmCvar_t copies of the same CVAR.
             */
            BYTES(&e, 0xff, 0x75, 24, 0xff, 0x75, 20, 0xff, 0x75, 16,
                      0xff, 0x75, 12, 0xff, 0x75, 8);
            BYTES(&e, 0xff, 0x93, 8, 0, 0, 0, 0x83, 0xc4, 20);
            BYTES(&e, 0x8b, 0x55, 12, 0x85, 0xd2, 0x74, 8); /* skip NULL vmCvar */
            BYTES(&e, 0x8b, 0x12, 0x89, 0x93); /* edx=[vmCvar]; [handle]=edx */
            word(&e, r->handle);
            restore(&e);
            BYTES(&e, 0xc3); /* keep engine registration's EAX result */
        }
        link_to(&e, next, payload->size);
    }
    jump(&e, PASS);

    e.labels[UPDATE] = payload->size;
    BYTES(&e, 0x8b, 0x7d, 12, 0x85, 0xff); /* edi=vmCvar; test edi,edi */
    equal_jump(&e, PASS);
    for (size_t i = 0; i < cvar_rule_count; ++i) {
        if (cvar_rules[i]->module != p->module || cvar_rules[i]->mode != CVAR_FORCE)
            continue;
        BYTES(&e, 0x8b, 0x07, 0x3b, 0x83); /* eax=[vmCvar]; cmp eax,[saved handle] */
        word(&e, records[i].handle);
        BYTES(&e, 0x0f, 0x85);
        size_t next = word(&e, 0);
        force_value(&e, p, &records[i]);
        jump(&e, PASS);
        link_to(&e, next, payload->size);
    }
    jump(&e, PASS);

    e.labels[SET] = payload->size;
    for (size_t i = 0; i < cvar_rule_count; ++i) {
        if (cvar_rules[i]->module != p->module || cvar_rules[i]->mode != CVAR_FORCE)
            continue;
        size_t next = match_name(&e, 12, records[i].name);
        address_eax(&e, records[i].value);
        BYTES(&e, 0x89, 0x45, 16); /* replace SET value argument */
        jump(&e, PASS);
        link_to(&e, next, payload->size);
    }

    /* Tail call preserves every original argument, including unknown syscalls'
     * variadic arguments, and returns directly to the original trap wrapper.
     */
    e.labels[PASS] = payload->size;
    BYTES(&e, 0x8b, 0x83, 8, 0, 0, 0); /* eax=engine syscall */
    restore(&e);
    BYTES(&e, 0xff, 0xe0); /* jmp eax */
    emit_equals(&e);
    for (size_t i = 0; i < e.fixup_count; ++i)
        link_to(&e, e.fixups[i].at, e.labels[e.fixups[i].label]);
    if (!e.ok)
        fprintf(stderr, "CVAR payload exceeds its size or fixup limit\n");
    return e.ok;
}
