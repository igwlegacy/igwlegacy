/* musl's per-architecture fp_arch.h for its libm.h, in place of the
 * vendored generic one, which is empty: the same functions that libm.h
 * otherwise defines, forcing a value to be computed for its side
 * effects, without its warning about a variable only ever set.
 */
#pragma once

#define fp_force_evalf fp_force_evalf
static inline void fp_force_evalf(float x)
{
    volatile float y = x;
    (void)y;
}

#define fp_force_eval fp_force_eval
static inline void fp_force_eval(double x)
{
    volatile double y = x;
    (void)y;
}

#define fp_force_evall fp_force_evall
static inline void fp_force_evall(long double x)
{
    volatile long double y = x;
    (void)y;
}
