/* The part of endian.h that musl's libm.h tests, for a little endian
 * x86, and features.h, which libm.h needs and does not include.
 */
#pragma once

#include "features.h"

#define __LITTLE_ENDIAN 1234
#define __BIG_ENDIAN 4321
#define __BYTE_ORDER __LITTLE_ENDIAN
