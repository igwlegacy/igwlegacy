/* setjmp() and longjmp(), which Lua raises its errors with. The buffer
 * holds the registers the cdecl convention preserves, the stack pointer
 * of the caller and the return address.
 */
#include <setjmp.h>

__attribute__((naked)) int setjmp(jmp_buf env)
{
    __asm__(
        "movl 4(%esp), %eax\n\t"
        "movl %ebx, 0(%eax)\n\t"
        "movl %esi, 4(%eax)\n\t"
        "movl %edi, 8(%eax)\n\t"
        "movl %ebp, 12(%eax)\n\t"
        "leal 4(%esp), %ecx\n\t"   /* the caller's esp after the return */
        "movl %ecx, 16(%eax)\n\t"
        "movl (%esp), %ecx\n\t"
        "movl %ecx, 20(%eax)\n\t"
        "xorl %eax, %eax\n\t"
        "ret");
}

__attribute__((naked)) void longjmp(jmp_buf env, int value)
{
    __asm__(
        "movl 4(%esp), %edx\n\t"
        "movl 8(%esp), %eax\n\t"
        "cmpl $1, %eax\n\t"        /* a value of 0 returns 1 instead */
        "adcl $0, %eax\n\t"
        "movl 0(%edx), %ebx\n\t"
        "movl 4(%edx), %esi\n\t"
        "movl 8(%edx), %edi\n\t"
        "movl 12(%edx), %ebp\n\t"
        "movl 16(%edx), %esp\n\t"
        "jmp *20(%edx)");
}
