/* Exact conversions between doubles and decimal digits, for strtod() and
 * vsnprintf()'s %e, %f and %g. See decimal.c.
 */
#pragma once

#include <stdint.h>

#pragma GCC visibility push(hidden)

/* Enough for every digit of any double: a subnormal's exact expansion
 * has at most 767 significant digits, the largest double 309.
 */
#define DECIMAL_MAX_DIGITS 800

/* A decimal value d[0].d[1]d[2]...d[n-1] * 10^exponent with digit values
 * 0 to 9, d[0] nonzero unless the value is zero, which is n 1, d[0] 0 and
 * exponent 0.
 */
typedef struct {
    unsigned char d[DECIMAL_MAX_DIGITS];
    int n;
    int exponent;
} decimal_t;

/* Every digit of the magnitude of the finite double with these bits, with
 * no trailing zeros.
 */
void decimal_from_double(decimal_t *out, uint64_t bits);

/* Round to the first keep significant digits, half to even, as printf in
 * the default rounding mode does. keep may be 0 or less: the value then
 * rounds to zero or up to 10^(exponent + 1 - keep). Trailing zeros are
 * dropped afterwards.
 */
void decimal_round(decimal_t *value, int keep);

#pragma GCC visibility pop
