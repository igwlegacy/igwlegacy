/* The freestanding C library subset declared by include/libc.h. Only what
 * tomlc17 and waves.c need is here, for the "C" locale. Nothing divides
 * 64-bit values, which on i386 would need compiler-rt's helpers.
 */
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

/* The loader never reads files through stdio. */
FILE *fopen(const char *name, const char *mode)
{
    (void)name, (void)mode;
    errno = EINVAL;
    return NULL;
}

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

/* The powers 10^(2^i), for scaling by any decimal exponent below 512. */
static const double powers[] = { 1e1, 1e2, 1e4, 1e8, 1e16, 1e32, 1e64, 1e128, 1e256 };

/* Decimal floats with up to 18 significant digits, plus inf and nan. That
 * is not always correctly rounded, which wave scripts never notice.
 */
double strtod(const char *text, char **end)
{
    const char *p = text;
    while (isspace((unsigned char)*p))
        ++p;
    bool negative = *p == '-';
    if (*p == '+' || *p == '-')
        ++p;
    if ((p[0] | 32) == 'i' && (p[1] | 32) == 'n' && (p[2] | 32) == 'f') {
        p += 3;
        if (!strncmp(p, "inity", 5))
            p += 5;
        if (end)
            *end = (char *)p;
        return negative ? -INFINITY : INFINITY;
    }
    if ((p[0] | 32) == 'n' && (p[1] | 32) == 'a' && (p[2] | 32) == 'n') {
        if (end)
            *end = (char *)p + 3;
        return negative ? -NAN : NAN;
    }

    long long mantissa = 0;
    int digits = 0, exponent = 0;
    bool any = false;
    for (; isdigit((unsigned char)*p); ++p, any = true) {
        if (digits < 18 && (mantissa || *p != '0'))
            mantissa = mantissa * 10 + (*p - '0'), ++digits;
        else if (mantissa)
            ++exponent;
    }
    if (*p == '.') {
        for (++p; isdigit((unsigned char)*p); ++p, any = true) {
            if (digits < 18 && (mantissa || *p != '0'))
                mantissa = mantissa * 10 + (*p - '0'), ++digits, --exponent;
            else if (!mantissa)
                --exponent;
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
            int n = 0;
            for (; isdigit((unsigned char)*e); ++e)
                if (n < 10000)
                    n = n * 10 + (*e - '0');
            exponent += down ? -n : n;
            p = e;
        }
    }
    if (end)
        *end = (char *)p;

    double value = (double)mantissa;
    if (value != 0) {
        bool down = exponent < 0;
        unsigned n = (unsigned)(down ? -exponent : exponent);
        if (n >= 512) {
            value = down ? 0 : INFINITY;
        } else {
            for (unsigned i = 0; n; ++i, n >>= 1)
                if (n & 1)
                    value = down ? value / powers[i] : value * powers[i];
        }
        if (value == 0 || isinf(value))
            errno = ERANGE;
    }
    return negative ? -value : value;
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

static void put_padded(sink_t *sink, const char *text, size_t length, int width,
                       bool left, char pad)
{
    int fill = width > (int)length ? width - (int)length : 0;
    if (!left)
        for (; fill; --fill)
            put(sink, pad);
    for (size_t i = 0; i < length; ++i)
        put(sink, text[i]);
    for (; fill; --fill)
        put(sink, ' ');
}

/* Digits of a 64-bit value by repeated subtraction of powers, which needs
 * no 64-bit division. Returns the start of the digits within buffer.
 */
static char *format_unsigned(char *buffer, unsigned long long value, unsigned base, bool upper)
{
    char *p = buffer + 24;
    *p = 0;
    if (base == 16) {
        const char *hex = upper ? "0123456789ABCDEF" : "0123456789abcdef";
        do
            *--p = hex[value & 15];
        while (value >>= 4);
        return p;
    }
    if (value <= UINT32_MAX) {
        uint32_t small = (uint32_t)value;
        do
            *--p = (char)('0' + small % 10);
        while (small /= 10);
        return p;
    }
    /* Leading digits first: count how often each power of ten fits. */
    static const unsigned long long tens[] = {
        10000000000000000000ULL, 1000000000000000000ULL, 100000000000000000ULL,
        10000000000000000ULL, 1000000000000000ULL, 100000000000000ULL,
        10000000000000ULL, 1000000000000ULL, 100000000000ULL, 10000000000ULL,
        1000000000ULL, 100000000ULL, 10000000ULL, 1000000ULL, 100000ULL,
        10000ULL, 1000ULL, 100ULL, 10ULL, 1ULL,
    };
    p = buffer;
    bool started = false;
    for (size_t i = 0; i < sizeof(tens) / sizeof(tens[0]); ++i) {
        char digit = '0';
        while (value >= tens[i])
            value -= tens[i], ++digit;
        if (digit != '0' || started || tens[i] == 1)
            *p++ = digit, started = true;
    }
    *p = 0;
    return buffer;
}

/* The conversions %s %c %d %i %u %x %X %p %%, with the flags '-' and '0',
 * a width, a precision for %s, and the length modifiers l, ll and z.
 */
int vsnprintf(char *buffer, size_t size, const char *format, va_list args)
{
    sink_t sink = { buffer, size, 0 };
    for (const char *f = format; *f; ++f) {
        if (*f != '%') {
            put(&sink, *f);
            continue;
        }
        bool left = false;
        char pad = ' ';
        for (++f; *f == '-' || *f == '0'; ++f) {
            if (*f == '-')
                left = true;
            else
                pad = '0';
        }
        int width = 0, precision = -1;
        if (*f == '*') {
            width = va_arg(args, int), ++f;
        } else {
            for (; isdigit((unsigned char)*f); ++f)
                width = width * 10 + (*f - '0');
        }
        if (*f == '.') {
            precision = 0;
            if (*++f == '*') {
                precision = va_arg(args, int), ++f;
            } else {
                for (; isdigit((unsigned char)*f); ++f)
                    precision = precision * 10 + (*f - '0');
            }
        }
        int longs = 0;
        for (; *f == 'l' || *f == 'z'; ++f)
            ++longs;

        char digits[32];
        switch (*f) {
        case 's': {
            const char *text = va_arg(args, const char *);
            if (!text)
                text = "(null)";
            size_t length = 0;
            while (text[length] && (precision < 0 || (int)length < precision))
                ++length;
            put_padded(&sink, text, length, width, left, ' ');
            break;
        }
        case 'c':
            digits[0] = (char)va_arg(args, int);
            put_padded(&sink, digits, 1, width, left, ' ');
            break;
        case 'd':
        case 'i': {
            long long value = longs >= 2 ? va_arg(args, long long) : va_arg(args, int);
            unsigned long long magnitude = value < 0 ? 0 - (unsigned long long)value
                                                     : (unsigned long long)value;
            char *text = format_unsigned(digits + 1, magnitude, 10, false);
            if (value < 0)
                *--text = '-';
            put_padded(&sink, text, strlen(text), width, left, left ? ' ' : pad);
            break;
        }
        case 'u':
        case 'x':
        case 'X':
        case 'p': {
            unsigned long long value = *f == 'p' ? (uintptr_t)va_arg(args, void *) :
                longs >= 2 ? va_arg(args, unsigned long long) : va_arg(args, unsigned);
            char *text = format_unsigned(digits, value, *f == 'u' ? 10 : 16, *f == 'X');
            put_padded(&sink, text, strlen(text), width, left, left ? ' ' : pad);
            break;
        }
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
