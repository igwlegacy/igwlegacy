/* Correctly rounded strtod() and the exact digits behind vsnprintf()'s
 * %e, %f and %g, with integer arithmetic only.
 *
 * Both directions work on the exact value: a double is m * 2^e with an
 * integer m, so its decimal expansion is m * 2^e or m * 5^-e * 10^e,
 * computed as big integers. A decimal string is turned into big integers
 * as well, divided where its exponent is negative, and rounded once to
 * the nearest double, ties to even. No result depends on the x87's or
 * SSE's precision or rounding mode, so every machine produces the same
 * bits and digits, which Lua wave scripts rely on to be deterministic.
 */
#include "decimal.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/* 4096 bits: the largest value either direction builds is 10^1124 times
 * 2^63 in strtod() (3797 bits), see the limits there.
 */
#define WORDS 128

/* A nonnegative big integer, least significant word first; the words from
 * n on are not part of it.
 */
typedef struct {
    uint32_t w[WORDS];
    int n;
} big_t;

static void big_trim(big_t *b)
{
    while (b->n && !b->w[b->n - 1])
        --b->n;
}

static void big_set(big_t *b, uint64_t value)
{
    b->w[0] = (uint32_t)value;
    b->w[1] = (uint32_t)(value >> 32);
    b->n = 2;
    big_trim(b);
}

/* b = b * factor + add. */
static void big_mul_add(big_t *b, uint32_t factor, uint32_t add)
{
    uint64_t carry = add;
    for (int i = 0; i < b->n; ++i) {
        uint64_t t = (uint64_t)b->w[i] * factor + carry;
        b->w[i] = (uint32_t)t;
        carry = t >> 32;
    }
    if (carry)
        b->w[b->n++] = (uint32_t)carry;
}

static void big_mul_pow5(big_t *b, int power)
{
    /* 5^13 is the largest power of 5 in 32 bits. */
    static const uint32_t small[] = {
        1, 5, 25, 125, 625, 3125, 15625, 78125, 390625, 1953125, 9765625,
        48828125, 244140625, 1220703125,
    };
    for (; power >= 13; power -= 13)
        big_mul_add(b, small[13], 0);
    if (power)
        big_mul_add(b, small[power], 0);
}

static void big_shl(big_t *b, int bits)
{
    if (!b->n || !bits)
        return;
    int words = bits / 32, shift = bits % 32;
    if (shift) {
        b->w[b->n] = 0;
        for (int i = b->n; i > 0; --i)
            b->w[i] = b->w[i] << shift | b->w[i - 1] >> (32 - shift);
        b->w[0] <<= shift;
        ++b->n;
    }
    memmove(b->w + words, b->w, (size_t)b->n * sizeof(b->w[0]));
    memset(b->w, 0, (size_t)words * sizeof(b->w[0]));
    b->n += words;
    big_trim(b);
}

static void big_mul_pow10(big_t *b, int power)
{
    big_mul_pow5(b, power);
    big_shl(b, power);
}

static int big_bits(const big_t *b)
{
    return b->n ? 32 * b->n - __builtin_clz(b->w[b->n - 1]) : 0;
}

static bool big_bit(const big_t *b, int bit)
{
    return bit / 32 < b->n && (b->w[bit / 32] >> (bit % 32) & 1);
}

static int big_cmp(const big_t *a, const big_t *b)
{
    if (a->n != b->n)
        return a->n < b->n ? -1 : 1;
    for (int i = a->n - 1; i >= 0; --i)
        if (a->w[i] != b->w[i])
            return a->w[i] < b->w[i] ? -1 : 1;
    return 0;
}

/* a -= b, for a >= b. */
static void big_sub(big_t *a, const big_t *b)
{
    uint32_t borrow = 0;
    for (int i = 0; i < a->n; ++i) {
        uint32_t x = a->w[i], y = i < b->n ? b->w[i] : 0;
        uint32_t d = x - y - borrow;
        borrow = x < y || (x == y && borrow);
        a->w[i] = d;
    }
    big_trim(a);
}

/* b /= divisor, returning the remainder. The hardware division takes
 * edx:eax, which avoids a 64-bit division helper.
 */
static uint32_t big_div_small(big_t *b, uint32_t divisor)
{
    uint32_t remainder = 0;
    for (int i = b->n - 1; i >= 0; --i) {
        uint32_t quotient;
        __asm__("divl %4" : "=a"(quotient), "=d"(remainder)
                : "a"(b->w[i]), "d"(remainder), "rm"(divisor));
        b->w[i] = quotient;
    }
    big_trim(b);
    return remainder;
}

void decimal_from_double(decimal_t *out, uint64_t bits)
{
    int field = (int)(bits >> 52 & 0x7ff);
    uint64_t mantissa = bits & ((1ULL << 52) - 1);
    if (!field && !mantissa) {
        out->n = 1, out->d[0] = 0, out->exponent = 0;
        return;
    }
    /* value = mantissa * 2^power exactly. */
    int power = field ? field - 1075 : -1074;
    if (field)
        mantissa |= 1ULL << 52;
    big_t b;
    big_set(&b, mantissa);
    /* value = b * 10^power10 */
    int power10 = 0;
    if (power >= 0) {
        big_shl(&b, power);
    } else {
        big_mul_pow5(&b, -power);
        power10 = power;
    }

    /* Chunks of nine digits, least significant first. */
    uint32_t chunks[DECIMAL_MAX_DIGITS / 9 + 1];
    int count = 0;
    while (b.n)
        chunks[count++] = big_div_small(&b, 1000000000u);
    char text[DECIMAL_MAX_DIGITS + 9];
    int n = 0;
    for (int i = count - 1; i >= 0; --i) {
        for (int k = 8; k >= 0; --k) {
            text[n + k] = (char)(chunks[i] % 10);
            chunks[i] /= 10;
        }
        n += 9;
    }
    int first = 0;
    while (!text[first])
        ++first;
    int last = n - 1;
    while (!text[last])
        --last;
    out->n = last - first + 1;
    memcpy(out->d, text + first, (size_t)out->n);
    /* The last of the n digits stands for 10^power10. */
    out->exponent = power10 + (n - 1 - first);
}

void decimal_round(decimal_t *value, int keep)
{
    if (keep >= value->n)
        return;
    bool up;
    if (keep < 0) {
        up = false;
    } else {
        /* Ties go to the even digit; the digit before d[0] is 0. */
        int next = value->d[keep];
        bool beyond = false;
        for (int i = keep + 1; i < value->n && !beyond; ++i)
            beyond = value->d[i] != 0;
        bool odd = keep > 0 && (value->d[keep - 1] & 1);
        up = next > 5 || (next == 5 && (beyond || odd));
    }
    if (keep <= 0) {
        /* Zero, or one unit of the place before the first digit. */
        if (up) {
            value->exponent += 1 - keep;
            value->d[0] = 1;
        } else {
            value->exponent = 0;
            value->d[0] = 0;
        }
        value->n = 1;
        return;
    }
    value->n = keep;
    if (up) {
        int i = keep - 1;
        while (i >= 0 && value->d[i] == 9)
            value->d[i--] = 0;
        if (i < 0) {
            /* All nines: 10^(exponent + 1). */
            value->d[0] = 1;
            value->n = 1;
            ++value->exponent;
        } else {
            ++value->d[i];
        }
    }
    while (value->n > 1 && !value->d[value->n - 1])
        --value->n;
}

/* The double nearest to q * 2^power, where sticky tells whether the exact
 * value is a little above that, ties to even; overflows to infinity and
 * underflows to subnormals or zero, which set errno.
 */
static uint64_t round_to_double(uint64_t q, int power, bool sticky)
{
    int length = 64 - __builtin_clzll(q);
    int lead = power + length - 1;
    if (lead > 1023) {
        errno = ERANGE;
        return 0x7ffULL << 52;
    }
    /* Subnormals keep fewer bits; the last one always stands for 2^-1074. */
    int precision = lead < -1022 ? lead + 1075 : 53;
    if (precision < 0) {
        errno = ERANGE;
        return 0;
    }
    int drop = length - precision;
    uint64_t m;
    if (drop <= 0) {
        m = q << -drop;
    } else {
        uint64_t rest = drop == 64 ? q : q & ((1ULL << drop) - 1);
        uint64_t half = 1ULL << (drop - 1);
        m = drop == 64 ? 0 : q >> drop;
        if (rest > half || (rest == half && (sticky || (m & 1))))
            ++m;
    }
    int exponent = power + drop;
    if (precision < 53) {
        /* exponent is -1074 here, and the bits are the subnormal's; one
         * that rounded up to 2^52 is the smallest normal, as it should.
         */
        if (!m)
            errno = ERANGE;
        return m;
    }
    if (m >> 53) {
        m >>= 1;
        ++exponent;
    }
    int field = exponent + 52 + 1023;
    if (field >= 0x7ff) {
        errno = ERANGE;
        return 0x7ffULL << 52;
    }
    return (uint64_t)field << 52 | (m & ((1ULL << 52) - 1));
}

/* Significant digits kept from the input; anything nonzero after them
 * becomes one more digit 1. 767 digits are enough to tell any two halfway
 * points apart, so rounding comes out the same.
 */
#define KEEP_DIGITS 800

/* Decimal floats, infinity and nan; hexadecimal floats are not supported
 * (Lua converts those itself).
 */
double strtod(const char *text, char **end)
{
    const char *p = text;
    while (isspace((unsigned char)*p))
        ++p;
    bool negative = *p == '-';
    if (*p == '+' || *p == '-')
        ++p;
    uint64_t bits;
    if ((p[0] | 32) == 'i' && (p[1] | 32) == 'n' && (p[2] | 32) == 'f') {
        p += 3;
        if ((p[0] | 32) == 'i' && (p[1] | 32) == 'n' && (p[2] | 32) == 'i' &&
            (p[3] | 32) == 't' && (p[4] | 32) == 'y')
            p += 5;
        bits = 0x7ffULL << 52;
        goto done;
    }
    if ((p[0] | 32) == 'n' && (p[1] | 32) == 'a' && (p[2] | 32) == 'n') {
        p += 3;
        bits = 0x7ff8ULL << 48;
        goto done;
    }

    /* value = digits * 10^power, digits without leading zeros. */
    unsigned char digits[KEEP_DIGITS + 1];
    int n = 0, power = 0;
    bool any = false, sticky = false;
    for (; isdigit((unsigned char)*p); ++p, any = true) {
        if (!n && *p == '0')
            continue;
        if (n < KEEP_DIGITS)
            digits[n++] = (unsigned char)(*p - '0');
        else
            ++power, sticky |= *p != '0';
    }
    if (*p == '.' && (any || isdigit((unsigned char)p[1]))) {
        for (++p; isdigit((unsigned char)*p); ++p, any = true) {
            if (!n && *p == '0')
                --power;
            else if (n < KEEP_DIGITS)
                digits[n++] = (unsigned char)(*p - '0'), --power;
            else
                sticky |= *p != '0';
        }
    }
    if (!any) {
        if (end)
            *end = (char *)text;
        return 0;
    }
    if ((*p | 32) == 'e') {
        const char *e = p + 1;
        bool down = *e == '-';
        if (*e == '+' || *e == '-')
            ++e;
        if (isdigit((unsigned char)*e)) {
            int exponent = 0;
            for (; isdigit((unsigned char)*e); ++e)
                if (exponent < 100000)
                    exponent = exponent * 10 + (*e - '0');
            power += down ? -exponent : exponent;
            p = e;
        }
    }
    if (sticky)
        digits[n++] = 1, --power;
    while (n && !digits[n - 1])
        --n, ++power;

    /* The place of the leading digit decides the far ends at once:
     * 10^310 overflows, and below 10^-324 everything rounds to zero.
     */
    int lead = n - 1 + power;
    if (!n) {
        bits = 0;
    } else if (lead > 309) {
        errno = ERANGE;
        bits = 0x7ffULL << 52;
    } else if (lead < -324) {
        errno = ERANGE;
        bits = 0;
    } else {
        big_t number;
        number.n = 0;
        for (int i = 0; i < n;) {
            uint32_t chunk = 0, scale = 1;
            for (int k = 0; k < 9 && i < n; ++k, ++i)
                chunk = chunk * 10 + digits[i], scale *= 10;
            big_mul_add(&number, scale, chunk);
        }
        if (power >= 0) {
            /* An integer of at most 310 digits: its top 64 bits. */
            big_mul_pow10(&number, power);
            int length = big_bits(&number);
            int shift = length > 64 ? length - 64 : 0;
            uint64_t q = 0;
            bool below = false;
            for (int i = 0; i < length; ++i) {
                if (i < shift)
                    below |= big_bit(&number, i);
                else if (big_bit(&number, i))
                    q |= 1ULL << (i - shift);
            }
            bits = round_to_double(q, shift, below);
        } else {
            /* number / 10^-power: scale the numerator by 2^s so that the
             * quotient q has 63 or 64 bits, and divide one bit at a time
             * from the remainder of its top part on.
             */
            big_t divisor;
            big_set(&divisor, 1);
            big_mul_pow10(&divisor, -power);
            int s = big_bits(&divisor) - big_bits(&number) + 63;
            if (s >= 0)
                big_shl(&number, s);
            else
                big_shl(&divisor, -s);
            /* number >> 64, which is below the divisor. */
            big_t rest;
            rest.n = number.n > 2 ? number.n - 2 : 0;
            memcpy(rest.w, number.w + 2, (size_t)rest.n * sizeof(rest.w[0]));
            uint64_t q = 0;
            for (int i = 63; i >= 0; --i) {
                big_shl(&rest, 1);
                big_mul_add(&rest, 1, big_bit(&number, i));
                if (big_cmp(&rest, &divisor) >= 0) {
                    big_sub(&rest, &divisor);
                    q |= 1ULL << i;
                }
            }
            bits = round_to_double(q, -s, rest.n != 0);
        }
    }
done:
    if (end)
        *end = (char *)p;
    if (negative)
        bits |= 1ULL << 63;
    union {
        uint64_t bits;
        double value;
    } result = { bits };
    return result.value;
}
