#include "emit.h"

#include <stdio.h>
#include <string.h>

uint32_t payload_append(payload_t *payload, const void *bytes, size_t size)
{
    uint32_t at = (uint32_t)payload->size;
    memcpy(payload->data + at, bytes, size);
    payload->size += size;
    return at;
}

bool payload_reserve(const payload_t *payload, size_t bytes, const char *what)
{
    if (payload->size > PAYLOAD_MAX - bytes) {
        fprintf(stderr, "%s payload exceeds its size limit\n", what);
        return false;
    }
    return true;
}

uint32_t emit_entry(payload_t *payload, uint32_t next)
{
    uint32_t at = (uint32_t)payload->size;
    EMIT(0xe9, B4(next - (at + 5u)));
    return at;
}

void emit_jump(payload_t *payload, uint32_t rva, uint32_t target)
{
    uint32_t at = (uint32_t)payload->size;
    EMIT(0xe9, B4(target - (rva + at + 5u)));
}

void emit_jump_if(payload_t *payload, uint32_t rva, unsigned char condition, uint32_t target)
{
    uint32_t at = (uint32_t)payload->size;
    EMIT(0x0f, condition, B4(target - (rva + at + 6u)));
}

void emit_call(payload_t *payload, uint32_t rva, uint32_t target)
{
    uint32_t at = (uint32_t)payload->size;
    EMIT(0xe8, B4(target - (rva + at + 5u)));
}

uint32_t emit_skip_if(payload_t *payload, unsigned char condition)
{
    return EMIT(condition, 0);
}

void emit_land(payload_t *payload, uint32_t branch)
{
    /* The displacement counts from the end of the two-byte branch. */
    payload->data[branch + 1] = (unsigned char)(payload->size - (branch + 2u));
}

uint32_t emit_anchor(payload_t *payload, uint32_t rva, unsigned reg)
{
    EMIT(0xe8, 0, 0, 0, 0, (unsigned char)(0x58 + reg));
    /* The pushed return address is that of the pop. */
    return rva + (uint32_t)payload->size - 1u;
}

void emit_load_address(payload_t *payload, uint32_t rva, unsigned reg, uint32_t at)
{
    uint32_t base = emit_anchor(payload, rva, reg);
    EMIT(0x81, (unsigned char)(0xc0 + reg), B4(rva + at - base)); /* add reg,imm32 */
}

uint32_t emit_string(payload_t *payload, const char *text)
{
    return payload_append(payload, text, strlen(text) + 1);
}

void resolve_hooks(code_rule_t *hooks, size_t count, const uint32_t *targets, uint32_t rva)
{
    for (size_t i = 0; i < count; ++i)
        put32(hooks[i].replacement + 1, rva + targets[i] - (hooks[i].rva + 5u));
}
