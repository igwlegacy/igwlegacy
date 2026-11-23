/* The C library subset that blobs use, so far what tomlc17 and the wave
 * loader need. A blob runs inside a retail module without a C library of
 * its own: libc.c implements these, heap.c the heap. Every standard header
 * in this directory includes this one, so tomlc17 builds unmodified.
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
int isdigit(int c);
int isprint(int c);
int isspace(int c);
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

/* math.h */
#define INFINITY __builtin_inff()
#define NAN __builtin_nanf("")
#define isnan(x) __builtin_isnan(x)
#define isinf(x) __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)

/* stdio.h: formatting only. The FILE functions exist for tomlc17's
 * toml_parse_file(), which the loader never calls.
 */
typedef struct FILE FILE;
#define EOF (-1)
int snprintf(char *buffer, size_t size, const char *format, ...);
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);
FILE *fopen(const char *name, const char *mode);
size_t fread(void *buffer, size_t size, size_t count, FILE *file);
int fclose(FILE *file);
int feof(FILE *file);
int ferror(FILE *file);

/* stdlib.h */
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
char *strerror(int error);
size_t strlen(const char *text);
int strncmp(const char *a, const char *b, size_t size);
size_t strspn(const char *text, const char *accept);

#pragma GCC visibility pop
