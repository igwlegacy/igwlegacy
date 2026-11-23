#ifndef IGW_EMIT_H
#define IGW_EMIT_H

#include "patcher.h"

/* Helpers for payload features that append machine code to the injected
 * section. Offsets are payload offsets; rva is the section RVA a feature's
 * build function receives, so rva + offset is the image RVA of that byte.
 * Every branch into the image is relative and every data reference goes
 * through call/pop, so the payload needs no base relocations.
 */

/* The little-endian bytes of a 32-bit value, for byte lists. */
#define B4(a) (a) & 255, ((a) >> 8) & 255, ((a) >> 16) & 255, ((a) >> 24) & 255

/* x86 register numbers as encoded in ModRM and the +r opcodes. */
enum { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI };

/* Append bytes and return the payload offset they start at. */
uint32_t payload_append(payload_t *payload, const void *bytes, size_t size);

/* Append a literal byte list to the payload variable in scope. */
#define EMIT(...) payload_append(payload, (const unsigned char[]){ __VA_ARGS__ }, \
                                 sizeof((const unsigned char[]){ __VA_ARGS__ }))

/* Fail with "<what> payload exceeds its size limit" unless at least bytes
 * remain, so that a feature can emit its fixed code without further checks.
 */
bool payload_reserve(const payload_t *payload, size_t bytes, const char *what);

/* Start a feature's entry with a jump to the next dispatcher, so that the
 * syscalls it does not handle pass through, and return the entry's payload
 * offset. next is a payload offset, not an image RVA.
 */
uint32_t emit_entry(payload_t *payload, uint32_t next);

/* jmp, jcc (the second opcode byte, 0x80..0x8f) and call rel32 to an
 * image RVA.
 */
void emit_jump(payload_t *payload, uint32_t rva, uint32_t target);
void emit_jump_if(payload_t *payload, uint32_t rva, unsigned char condition, uint32_t target);
void emit_call(payload_t *payload, uint32_t rva, uint32_t target);

/* A short forward branch with the opcode condition (0x70..0x7f, 0xeb),
 * resolved by emit_land() once the destination is reached.
 */
uint32_t emit_skip_if(payload_t *payload, unsigned char condition);
void emit_land(payload_t *payload, uint32_t branch);

/* call $+5; pop reg: reg holds the runtime address of the returned RVA. */
uint32_t emit_anchor(payload_t *payload, uint32_t rva, unsigned reg);

/* reg = the runtime address of the payload offset at. */
void emit_load_address(payload_t *payload, uint32_t rva, unsigned reg, uint32_t at);

/* Append a NUL-terminated string and return its payload offset. */
uint32_t emit_string(payload_t *payload, const char *text);

/* Point the rel32 of each hook's jmp or call replacement at the payload
 * offset targets[i].
 */
void resolve_hooks(code_rule_t *hooks, size_t count, const uint32_t *targets, uint32_t rva);

#endif
