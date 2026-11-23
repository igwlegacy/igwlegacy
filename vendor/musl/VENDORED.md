# Vendored: musl libm subset

This directory holds an unmodified copy of the part of musl's math
library that the Lua runtime in the C blobs needs: the generic C
implementations of the functions Lua's `math` library and number
formatting call, their helpers and data tables, and the internal
header they share. The paths are the same as upstream's.

- Upstream: https://git.musl-libc.org/git/musl
- Commit: 7813e9119eb5fe681b6750b8a033a0490bca0941
- License: MIT (see `COPYRIGHT`)

The generic C versions are used on purpose instead of musl's i386
assembly ones: compiled with SSE2 they only do plain IEEE double
arithmetic, so every CPU computes the same bits, which the x87
transcendental instructions do not guarantee.

Files:

- `arch/generic/fp_arch.h`
- `COPYRIGHT`
- `src/internal/libm.h`
- `src/math/acos.c`
- `src/math/asin.c`
- `src/math/atan2.c`
- `src/math/atan.c`
- `src/math/ceil.c`
- `src/math/__cos.c`
- `src/math/cos.c`
- `src/math/exp.c`
- `src/math/exp_data.c`
- `src/math/exp_data.h`
- `src/math/fabs.c`
- `src/math/floor.c`
- `src/math/fmod.c`
- `src/math/frexp.c`
- `src/math/ldexp.c`
- `src/math/log10.c`
- `src/math/log2.c`
- `src/math/log2_data.c`
- `src/math/log2_data.h`
- `src/math/log.c`
- `src/math/log_data.c`
- `src/math/log_data.h`
- `src/math/__math_divzero.c`
- `src/math/__math_invalid.c`
- `src/math/__math_oflow.c`
- `src/math/__math_uflow.c`
- `src/math/__math_xflow.c`
- `src/math/pow.c`
- `src/math/pow_data.c`
- `src/math/pow_data.h`
- `src/math/__rem_pio2.c`
- `src/math/__rem_pio2_large.c`
- `src/math/scalbn.c`
- `src/math/__sin.c`
- `src/math/sin.c`
- `src/math/sqrt.c`
- `src/math/sqrt_data.c`
- `src/math/sqrt_data.h`
- `src/math/__tan.c`
- `src/math/tan.c`

To update, copy the same files from the desired upstream commit over
these and update the commit reference above.
