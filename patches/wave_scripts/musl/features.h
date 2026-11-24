/* What musl's own build gets from its src/include/features.h, for the
 * vendored libm sources (vendor/musl): the visibility of their internal
 * helpers. endian.h includes it, as libm.h includes nothing else that
 * could.
 */
#pragma once

#define hidden __attribute__((__visibility__("hidden")))
