/* The freestanding C library subset declared by include/libc.h, for the
 * "C" locale. strtod() and the digits of %e, %f and %g are in decimal.c,
 * the 64-bit division helpers in int64.c.
 */
#include "decimal.h"

#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int errno;

int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalpha(int c) { return (c | 32) >= 'a' && (c | 32) <= 'z'; }
int isalnum(int c) { return isdigit(c) || isalpha(c); }
int isprint(int c) { return c >= 0x20 && c < 0x7f; }
int isspace(int c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
int iscntrl(int c) { return (c >= 0 && c < 0x20) || c == 0x7f; }
int isgraph(int c) { return c > 0x20 && c < 0x7f; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int ispunct(int c) { return isgraph(c) && !isalnum(c); }
int isxdigit(int c) { return isdigit(c) || ((c | 32) >= 'a' && (c | 32) <= 'f'); }
int tolower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
int toupper(int c) { return c >= 'a' && c <= 'z' ? c - 32 : c; }

struct lconv *localeconv(void)
{
    static char point[] = ".";
    static struct lconv c = { point };
    return &c;
}

char *strerror(int error)
{
    return error == ERANGE ? "result out of range" : "error";
}

_Noreturn void abort(void)
{
    __builtin_trap();
}

time_t time(time_t *now)
{
    if (now)
        *now = -1;
    return -1;
}

clock_t clock(void)
{
    return -1;
}

/* No blob reads or writes files through stdio. */
FILE *const stdin = NULL;
FILE *const stdout = NULL;
FILE *const stderr = NULL;

FILE *fopen(const char *name, const char *mode)
{
    (void)name, (void)mode;
    errno = EINVAL;
    return NULL;
}

FILE *freopen(const char *name, const char *mode, FILE *file)
{
    (void)name, (void)mode, (void)file;
    errno = EINVAL;
    return NULL;
}

size_t fwrite(const void *buffer, size_t size, size_t count, FILE *file)
{
    (void)buffer, (void)size, (void)count, (void)file;
    return 0;
}

int getc(FILE *file) { (void)file; return EOF; }
int fflush(FILE *file) { (void)file; return EOF; }

size_t fread(void *buffer, size_t size, size_t count, FILE *file)
{
    (void)buffer, (void)size, (void)count, (void)file;
    return 0;
}

int fclose(FILE *file) { (void)file; return EOF; }
int feof(FILE *file) { (void)file; return 1; }
int ferror(FILE *file) { (void)file; return 1; }

void *memchr(const void *memory, int c, size_t size)
{
    const unsigned char *p = memory;
    for (; size; --size, ++p)
        if (*p == (unsigned char)c)
            return (void *)p;
    return NULL;
}

int memcmp(const void *a, const void *b, size_t size)
{
    const unsigned char *x = a, *y = b;
    for (; size; --size, ++x, ++y)
        if (*x != *y)
            return *x - *y;
    return 0;
}

/* The compiler turns loops like these back into memcpy/memset calls, which
 * would recurse; the inline string instructions avoid that.
 */
void *memcpy(void *destination, const void *source, size_t size)
{
    void *d = destination;
    __asm__ volatile("rep movsb" : "+D"(d), "+S"(source), "+c"(size) : : "memory");
    return destination;
}

void *memmove(void *destination, const void *source, size_t size)
{
    if ((uintptr_t)destination - (uintptr_t)source >= size)
        return memcpy(destination, source, size);
    /* Overlapping with the destination above: copy backwards. */
    void *d = (char *)destination + size - 1;
    const void *s = (const char *)source + size - 1;
    __asm__ volatile("std\n\trep movsb\n\tcld" : "+D"(d), "+S"(s), "+c"(size) : : "memory");
    return destination;
}

void *memset(void *destination, int c, size_t size)
{
    void *d = destination;
    __asm__ volatile("rep stosb" : "+D"(d), "+c"(size) : "a"(c) : "memory");
    return destination;
}

size_t strlen(const char *text)
{
    const char *p = text;
    while (*p)
        ++p;
    return (size_t)(p - text);
}

char *strchr(const char *text, int c)
{
    for (;; ++text) {
        if (*text == (char)c)
            return (char *)text;
        if (!*text)
            return NULL;
    }
}

int strcmp(const char *a, const char *b)
{
    for (; *a && *a == *b; ++a, ++b)
        ;
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t size)
{
    for (; size; --size, ++a, ++b) {
        if (*a != *b)
            return (unsigned char)*a - (unsigned char)*b;
        if (!*a)
            break;
    }
    return 0;
}

size_t strspn(const char *text, const char *accept)
{
    size_t n = 0;
    while (text[n] && strchr(accept, text[n]))
        ++n;
    return n;
}

char *strpbrk(const char *text, const char *accept)
{
    for (; *text; ++text)
        if (strchr(accept, *text))
            return (char *)text;
    return NULL;
}

char *strstr(const char *text, const char *part)
{
    size_t length = strlen(part);
    for (; *text; ++text)
        if (!strncmp(text, part, length))
            return (char *)text;
    return length ? NULL : (char *)text;
}

char *strcpy(char *destination, const char *source)
{
    return memcpy(destination, source, strlen(source) + 1);
}

/* The "C" locale collates by byte value. */
int strcoll(const char *a, const char *b)
{
    return strcmp(a, b);
}

static int digit_value(int c)
{
    if (isdigit(c))
        return c - '0';
    if (isalpha(c))
        return (c | 32) - 'a' + 10;
    return 99;
}

/* Bases 2, 8, 10 and 16 only, which keeps every cutoff a constant. */
static unsigned long long parse_unsigned(const char *text, char **end, int base,
                                         bool *negative, bool *overflow)
{
    const char *p = text;
    while (isspace((unsigned char)*p))
        ++p;
    *negative = *p == '-';
    if (*p == '+' || *p == '-')
        ++p;
    if ((base == 0 || base == 16) && p[0] == '0' && (p[1] | 32) == 'x' &&
        isxdigit((unsigned char)p[2])) {
        p += 2;
        base = 16;
    } else if (base == 0) {
        base = *p == '0' ? 8 : 10;
    }
    const unsigned long long cutoff =
        base == 16 ? ULLONG_MAX / 16 : base == 8 ? ULLONG_MAX / 8 :
        base == 2 ? ULLONG_MAX / 2 : ULLONG_MAX / 10;
    unsigned long long value = 0;
    const char *digits = p;
    *overflow = false;
    for (int d; (d = digit_value((unsigned char)*p)) < base; ++p) {
        if (value > cutoff || value * (unsigned)base > ULLONG_MAX - (unsigned)d)
            *overflow = true;
        value = value * (unsigned)base + (unsigned)d;
    }
    if (end)
        *end = (char *)(p == digits ? text : p);
    return value;
}

long long strtoll(const char *text, char **end, int base)
{
    bool negative, overflow;
    unsigned long long value = parse_unsigned(text, end, base, &negative, &overflow);
    if (overflow || value > (unsigned long long)LLONG_MAX + negative) {
        errno = ERANGE;
        return negative ? LLONG_MIN : LLONG_MAX;
    }
    return negative ? (long long)(0 - value) : (long long)value;
}

long strtol(const char *text, char **end, int base)
{
    long long value = strtoll(text, end, base);
    if (value > LONG_MAX || value < LONG_MIN) {
        errno = ERANGE;
        return value > 0 ? LONG_MAX : LONG_MIN;
    }
    return (long)value;
}

int atoi(const char *text)
{
    return (int)strtol(text, NULL, 10);
}

/* A bounded writer that keeps counting past the end, as vsnprintf must. */
typedef struct {
    char *at;
    size_t left;
    int count;
} sink_t;

static void put(sink_t *sink, char c)
{
    if (sink->left > 1) {
        *sink->at++ = c;
        --sink->left;
    }
    ++sink->count;
}

/* Digits of a 64-bit value, written backwards ending at end; returns the
 * first digit. Bases 8 and 16 shift; base 10 divides 32 bits at a time
 * by 10^9 so that only 32-bit hardware divisions are needed.
 */
static char *format_unsigned(char *end, unsigned long long value, unsigned base, bool upper)
{
    char *p = end;
    if (base != 10) {
        const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
        unsigned bits = base == 16 ? 4 : 3;
        do
            *--p = digits[value & (base - 1)];
        while (value >>= bits);
        return p;
    }
    while (value > UINT32_MAX) {
        /* value / 10^9 by long division over 32-bit halves. */
        uint32_t high = (uint32_t)(value >> 32), low = (uint32_t)value;
        uint32_t quotient_high = high / 1000000000u, rest = high % 1000000000u;
        uint32_t quotient_low, chunk;
        __asm__("divl %4" : "=a"(quotient_low), "=d"(chunk)
                : "a"(low), "d"(rest), "rm"(1000000000u));
        value = (unsigned long long)quotient_high << 32 | quotient_low;
        for (int i = 0; i < 9; ++i, chunk /= 10)
            *--p = (char)('0' + chunk % 10);
    }
    uint32_t small = (uint32_t)value;
    do
        *--p = (char)('0' + small % 10);
    while (small /= 10);
    return p;
}

/* How one conversion is to be laid out. */
typedef struct {
    bool left;      /* '-' */
    bool plus;      /* '+' */
    bool space;     /* ' ' */
    bool alternate; /* '#' */
    bool zero;      /* '0' */
    int width;
    int precision;  /* -1 without one */
} spec_t;

/* Pads the field of prefix and length more characters to the width:
 * spaces before or after it, or zeros between the prefix and the rest.
 * Call it once before writing the rest with before set and once after.
 */
static void pad(sink_t *sink, const spec_t *spec, const char *prefix, int length,
                bool zeros, bool before)
{
    int fill = spec->width - (int)strlen(prefix) - length;
    if (before) {
        if (!spec->left && !zeros)
            for (int i = 0; i < fill; ++i)
                put(sink, ' ');
        for (; *prefix; ++prefix)
            put(sink, *prefix);
        if (!spec->left && zeros)
            for (int i = 0; i < fill; ++i)
                put(sink, '0');
    } else if (spec->left) {
        for (int i = 0; i < fill; ++i)
            put(sink, ' ');
    }
}

static void format_integer(sink_t *sink, const spec_t *spec, char conversion,
                           unsigned long long magnitude, bool negative)
{
    char buffer[32];
    char *end = buffer + sizeof(buffer);
    unsigned base = conversion == 'o' ? 8 : conversion == 'x' || conversion == 'X' ||
                    conversion == 'p' ? 16 : 10;
    char *digits = format_unsigned(end, magnitude, base, conversion == 'X');
    int length = (int)(end - digits);
    /* A precision is the minimum digit count; 0 prints nothing for 0. */
    if (!spec->precision && !magnitude)
        length = 0;
    int zeros = spec->precision > length ? spec->precision - length : 0;
    char prefix[3] = "";
    if (negative)
        prefix[0] = '-';
    else if (spec->plus && (conversion == 'd' || conversion == 'i'))
        prefix[0] = '+';
    else if (spec->space && (conversion == 'd' || conversion == 'i'))
        prefix[0] = ' ';
    if (conversion == 'p' || (spec->alternate && magnitude &&
                              (conversion == 'x' || conversion == 'X')))
        prefix[0] = '0', prefix[1] = conversion == 'X' ? 'X' : 'x';
    if (spec->alternate && conversion == 'o' && !zeros && (!length || *digits != '0'))
        zeros = 1;
    bool zero_pad = spec->zero && spec->precision < 0;
    pad(sink, spec, prefix, zeros + length, zero_pad, true);
    for (int i = 0; i < zeros; ++i)
        put(sink, '0');
    for (int i = 0; i < length; ++i)
        put(sink, digits[i]);
    pad(sink, spec, prefix, zeros + length, zero_pad, false);
}

/* Writes or, without a sink, only counts the characters of a float body. */
typedef struct {
    sink_t *sink;
    int count;
} body_t;

static void emit(body_t *body, char c)
{
    if (body->sink)
        put(body->sink, c);
    ++body->count;
}

/* The digit for the place 10^place of a rounded value. */
static char digit_at(const decimal_t *value, int place)
{
    int index = value->exponent - place;
    return (char)('0' + (index >= 0 && index < value->n ? value->d[index] : 0));
}

/* The digits of %f with fraction digits after the point, or of %e with
 * fraction digits after the first one, without the sign.
 */
static void float_body(body_t *body, const decimal_t *value, bool exponential,
                       int fraction, bool point, bool upper)
{
    if (exponential) {
        emit(body, digit_at(value, value->exponent));
        if (point)
            emit(body, '.');
        for (int i = 1; i <= fraction; ++i)
            emit(body, digit_at(value, value->exponent - i));
        emit(body, upper ? 'E' : 'e');
        int exponent = value->exponent;
        emit(body, exponent < 0 ? '-' : '+');
        if (exponent < 0)
            exponent = -exponent;
        char buffer[8];
        char *end = buffer + sizeof(buffer);
        char *digits = format_unsigned(end, (unsigned)exponent, 10, false);
        if (end - digits < 2)
            emit(body, '0');
        for (; digits < end; ++digits)
            emit(body, *digits);
        return;
    }
    for (int place = value->exponent > 0 ? value->exponent : 0; place >= 0; --place)
        emit(body, digit_at(value, place));
    if (point)
        emit(body, '.');
    for (int i = 1; i <= fraction; ++i)
        emit(body, digit_at(value, -i));
}

/* %e, %f and %g with exact digits, rounded half to even like glibc's and
 * musl's printf in the default rounding mode, see decimal.c.
 */
static void format_float(sink_t *sink, const spec_t *spec, char conversion, double number)
{
    union {
        double value;
        uint64_t bits;
    } in = { number };
    bool negative = in.bits >> 63;
    bool upper = conversion >= 'A' && conversion <= 'Z';
    char lower = (char)(conversion | 32);
    char prefix[2] = "";
    if (negative)
        prefix[0] = '-';
    else if (spec->plus)
        prefix[0] = '+';
    else if (spec->space)
        prefix[0] = ' ';

    if ((in.bits >> 52 & 0x7ff) == 0x7ff) {
        const char *text = in.bits << 12 ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
        pad(sink, spec, prefix, 3, false, true);
        for (; *text; ++text)
            put(sink, *text);
        pad(sink, spec, prefix, 3, false, false);
        return;
    }

    decimal_t value;
    decimal_from_double(&value, in.bits);
    int precision = spec->precision < 0 ? 6 : spec->precision;
    bool exponential = lower == 'e';
    int fraction = precision;
    if (lower == 'f') {
        decimal_round(&value, value.exponent + 1 + precision);
    } else if (lower == 'e') {
        decimal_round(&value, precision + 1);
    } else {
        /* %g: %e's exponent X after rounding to P significant digits picks
         * %f with P - 1 - X fraction digits if P > X >= -4, else %e with
         * P - 1. Without '#', trailing zeros and a bare point go.
         */
        int significant = precision ? precision : 1;
        decimal_round(&value, significant);
        int x = value.exponent;
        exponential = !(significant > x && x >= -4);
        fraction = exponential ? significant - 1 : significant - 1 - x;
        if (!spec->alternate) {
            /* The rounded value has no trailing zeros. */
            int needed = exponential ? value.n - 1 : value.n - 1 - x;
            if (needed < 0)
                needed = 0;
            if (fraction > needed)
                fraction = needed;
        }
    }
    bool point = fraction > 0 || spec->alternate;
    body_t count = { NULL, 0 };
    float_body(&count, &value, exponential, fraction, point, upper);
    pad(sink, spec, prefix, count.count, spec->zero, true);
    body_t out = { sink, 0 };
    float_body(&out, &value, exponential, fraction, point, upper);
    pad(sink, spec, prefix, count.count, spec->zero, false);
}

/* printf() formatting with the flags '-', '+', ' ', '#' and '0', a width
 * and a precision (both may be '*'), the length modifiers hh, h, l, ll, j,
 * z, t and L, and the conversions d i u o x X c s p e E f F g G and %%.
 * %p prints 0x and hex digits. An unknown conversion ends the output.
 */
int vsnprintf(char *buffer, size_t size, const char *format, va_list args)
{
    sink_t sink = { buffer, size, 0 };
    for (const char *f = format; *f; ++f) {
        if (*f != '%') {
            put(&sink, *f);
            continue;
        }
        spec_t spec = { false, false, false, false, false, 0, -1 };
        for (++f;; ++f) {
            if (*f == '-')
                spec.left = true;
            else if (*f == '+')
                spec.plus = true;
            else if (*f == ' ')
                spec.space = true;
            else if (*f == '#')
                spec.alternate = true;
            else if (*f == '0')
                spec.zero = true;
            else
                break;
        }
        if (*f == '*') {
            spec.width = va_arg(args, int), ++f;
            if (spec.width < 0)
                spec.left = true, spec.width = -spec.width;
        } else {
            for (; isdigit((unsigned char)*f); ++f)
                spec.width = spec.width * 10 + (*f - '0');
        }
        if (*f == '.') {
            spec.precision = 0;
            if (*++f == '*') {
                spec.precision = va_arg(args, int), ++f;
                if (spec.precision < 0)
                    spec.precision = -1;
            } else {
                for (; isdigit((unsigned char)*f); ++f)
                    spec.precision = spec.precision * 10 + (*f - '0');
            }
        }
        /* char and short arrive promoted to int; long, size_t and
         * ptrdiff_t are 32 bits wide here.
         */
        int longs = 0, shorts = 0;
        for (;; ++f) {
            if (*f == 'l')
                ++longs;
            else if (*f == 'j')
                longs = 2;
            else if (*f == 'h')
                ++shorts;
            else if (*f != 'z' && *f != 't' && *f != 'L')
                break;
        }

        switch (*f) {
        case 's': {
            const char *text = va_arg(args, const char *);
            if (!text)
                text = "(null)";
            int length = 0;
            while (text[length] && (spec.precision < 0 || length < spec.precision))
                ++length;
            pad(&sink, &spec, "", length, false, true);
            for (int i = 0; i < length; ++i)
                put(&sink, text[i]);
            pad(&sink, &spec, "", length, false, false);
            break;
        }
        case 'c': {
            char c = (char)va_arg(args, int);
            pad(&sink, &spec, "", 1, false, true);
            put(&sink, c);
            pad(&sink, &spec, "", 1, false, false);
            break;
        }
        case 'd':
        case 'i': {
            long long value = longs >= 2 ? va_arg(args, long long) : va_arg(args, int);
            if (shorts == 1)
                value = (short)value;
            else if (shorts >= 2)
                value = (signed char)value;
            format_integer(&sink, &spec, *f,
                           value < 0 ? 0 - (unsigned long long)value : (unsigned long long)value,
                           value < 0);
            break;
        }
        case 'u':
        case 'o':
        case 'x':
        case 'X': {
            unsigned long long value = longs >= 2 ? va_arg(args, unsigned long long)
                                                  : va_arg(args, unsigned);
            if (shorts == 1)
                value = (unsigned short)value;
            else if (shorts >= 2)
                value = (unsigned char)value;
            format_integer(&sink, &spec, *f, value, false);
            break;
        }
        case 'p':
            spec.precision = -1;
            format_integer(&sink, &spec, 'p', (uintptr_t)va_arg(args, void *), false);
            break;
        case 'e':
        case 'E':
        case 'f':
        case 'F':
        case 'g':
        case 'G':
            format_float(&sink, &spec, *f, va_arg(args, double));
            break;
        case '%':
            put(&sink, '%');
            break;
        default:
            /* Unknown conversion or a truncated format: stop formatting. */
            goto done;
        }
    }
done:
    if (size)
        *sink.at = 0;
    return sink.count;
}

int snprintf(char *buffer, size_t size, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    int count = vsnprintf(buffer, size, format, args);
    va_end(args);
    return count;
}
