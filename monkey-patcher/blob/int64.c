/* The 64-bit division helpers that compilers call on i386, where the
 * hardware only divides a 64-bit value by a 32-bit one. Lua's integers
 * are 64 bits wide.
 */
#include <stdint.h>

#pragma GCC visibility push(hidden)

/* numerator / divisor and its remainder. A divisor of 0 traps, as the
 * hardware division would.
 */
static uint64_t divide(uint64_t numerator, uint64_t divisor, uint64_t *remainder)
{
    if (!divisor)
        __builtin_trap();
    if (divisor <= UINT32_MAX) {
        /* Long division by 32-bit halves with the hardware's divl, whose
         * quotient fits as the upper remainder is below the divisor.
         */
        uint32_t d = (uint32_t)divisor, high = (uint32_t)(numerator >> 32);
        uint32_t quotient_high = high / d, rest = high % d, quotient_low;
        __asm__("divl %4" : "=a"(quotient_low), "=d"(rest)
                : "a"((uint32_t)numerator), "d"(rest), "rm"(d));
        *remainder = rest;
        return (uint64_t)quotient_high << 32 | quotient_low;
    }
    /* A divisor above 32 bits leaves a quotient below 2^32: shift and
     * subtract, one quotient bit at a time. The numerator is nonzero past
     * the first test, which __builtin_clzll needs.
     */
    if (numerator < divisor) {
        *remainder = numerator;
        return 0;
    }
    uint64_t quotient = 0;
    int shift = __builtin_clzll(divisor) - __builtin_clzll(numerator);
    divisor <<= shift;
    for (; shift >= 0; --shift, divisor >>= 1) {
        quotient <<= 1;
        if (numerator >= divisor) {
            numerator -= divisor;
            quotient |= 1;
        }
    }
    *remainder = numerator;
    return quotient;
}

uint64_t __udivdi3(uint64_t a, uint64_t b)
{
    uint64_t rest;
    return divide(a, b, &rest);
}

uint64_t __umoddi3(uint64_t a, uint64_t b)
{
    uint64_t rest;
    divide(a, b, &rest);
    return rest;
}

/* Signed ones truncate toward zero; the remainder takes the dividend's
 * sign.
 */
int64_t __divdi3(int64_t a, int64_t b)
{
    uint64_t rest;
    uint64_t q = divide(a < 0 ? 0 - (uint64_t)a : (uint64_t)a,
                        b < 0 ? 0 - (uint64_t)b : (uint64_t)b, &rest);
    return (a < 0) != (b < 0) ? (int64_t)(0 - q) : (int64_t)q;
}

int64_t __moddi3(int64_t a, int64_t b)
{
    uint64_t rest;
    divide(a < 0 ? 0 - (uint64_t)a : (uint64_t)a,
           b < 0 ? 0 - (uint64_t)b : (uint64_t)b, &rest);
    return a < 0 ? (int64_t)(0 - rest) : (int64_t)rest;
}

#pragma GCC visibility pop
