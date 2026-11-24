/* The C library subset that blobs use: what tomlc17, Lua, musl's math
 * functions and the wave loaders need. A blob runs inside a retail module
 * without a C library of its own: libc.c implements these, decimal.c the
 * number conversions, heap.c the heap, int64.c the 64-bit divisions and
 * setjmp.c setjmp.h's functions; the math functions are musl's, which a
 * blob that uses them builds itself (see patches/meson.build). Every
 * standard header in this directory includes this one, so all of them
 * build unmodified.
 */
#pragma once

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Hidden symbols need no GOT entries. */
#pragma GCC visibility push(hidden)

/* assert.h: the game has no place to report a failed assertion. */
#define assert(x) ((void)0)
#define static_assert _Static_assert

/* ctype.h, for the "C" locale. */
int isalnum(int c);
int isalpha(int c);
int iscntrl(int c);
int isdigit(int c);
int isgraph(int c);
int islower(int c);
int isprint(int c);
int ispunct(int c);
int isspace(int c);
int isupper(int c);
int isxdigit(int c);
int tolower(int c);
int toupper(int c);

/* errno.h */
#define EINVAL 22
#define ERANGE 34
extern int errno;

/* locale.h: the "C" locale only. */
struct lconv {
    char *decimal_point;
};
struct lconv *localeconv(void);

/* math.h: double precision only. */
#define INFINITY __builtin_inff()
#define NAN __builtin_nanf("")
#define HUGE_VAL __builtin_huge_val()
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x) __builtin_signbit(x)
/* What expressions evaluate in: x87 code computes in long double, SSE2
 * code in the operands' types.
 */
#if __FLT_EVAL_METHOD__ == 0
typedef float float_t;
typedef double double_t;
#else
typedef long double float_t;
typedef long double double_t;
#endif
double acos(double x);
double asin(double x);
double atan(double x);
double atan2(double y, double x);
double ceil(double x);
double cos(double x);
double exp(double x);
double fabs(double x);
double floor(double x);
double fmod(double x, double y);
double frexp(double x, int *exponent);
double ldexp(double x, int exponent);
double log(double x);
double log10(double x);
double log2(double x);
double pow(double x, double y);
double scalbn(double x, int exponent);
double sin(double x);
double sqrt(double x);
double tan(double x);

/* stdio.h: formatting only. The FILE functions exist for tomlc17's
 * toml_parse_file() and Lua's luaL_loadfilex(), which nothing calls; no
 * file opens, and the standard streams are null.
 */
typedef struct FILE FILE;
#define EOF (-1)
extern FILE *const stdin;
extern FILE *const stdout;
extern FILE *const stderr;
int snprintf(char *buffer, size_t size, const char *format, ...);
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);
FILE *fopen(const char *name, const char *mode);
FILE *freopen(const char *name, const char *mode, FILE *file);
size_t fread(void *buffer, size_t size, size_t count, FILE *file);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *file);
int getc(FILE *file);
int fflush(FILE *file);
int fclose(FILE *file);
int feof(FILE *file);
int ferror(FILE *file);

/* stdlib.h */
_Noreturn void abort(void);
void *malloc(size_t size);
void *realloc(void *pointer, size_t size);
void free(void *pointer);
int atoi(const char *text);
double strtod(const char *text, char **end);
long strtol(const char *text, char **end, int base);
long long strtoll(const char *text, char **end, int base);

/* string.h */
void *memchr(const void *memory, int c, size_t size);
int memcmp(const void *a, const void *b, size_t size);
void *memcpy(void *destination, const void *source, size_t size);
void *memmove(void *destination, const void *source, size_t size);
void *memset(void *destination, int c, size_t size);
char *strchr(const char *text, int c);
int strcmp(const char *a, const char *b);
int strcoll(const char *a, const char *b);
char *strcpy(char *destination, const char *source);
char *strerror(int error);
size_t strlen(const char *text);
int strncmp(const char *a, const char *b, size_t size);
char *strpbrk(const char *text, const char *accept);
size_t strspn(const char *text, const char *accept);
char *strstr(const char *text, const char *part);

/* time.h: blobs have no clock, and must not depend on one; time() and
 * clock() report that with -1.
 */
typedef long long time_t;
typedef long clock_t;
#define CLOCKS_PER_SEC 1000
time_t time(time_t *now);
clock_t clock(void);

#pragma GCC visibility pop
