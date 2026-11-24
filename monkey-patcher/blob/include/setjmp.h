/* Standard header stand-in, see setjmp.c. */
#pragma once

#pragma GCC visibility push(hidden)

/* ebx, esi, edi, ebp, esp and the return address. */
typedef unsigned long jmp_buf[6];

__attribute__((returns_twice)) int setjmp(jmp_buf env);
_Noreturn void longjmp(jmp_buf env, int value);

#pragma GCC visibility pop
