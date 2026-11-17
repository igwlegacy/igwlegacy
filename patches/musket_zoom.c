#include "patches.h"

#include <float.h>
#include <string.h>

/* Right-click musket zoom-in duration, in milliseconds (stock: 200).
 * Edit, rebuild and install the patched cgamex86.dll on the client.
 * Use 1 for a virtually immediate zoom; zoom-out still takes 150 ms.
 * This controls the view's FOV transition, not the weapon animation.
 */
#define MUSKET_ZOOM_IN_MS 200

_Static_assert(MUSKET_ZOOM_IN_MS >= 1 && MUSKET_ZOOM_IN_MS <= INT32_MAX,
               "Musket zoom-in time must be a positive signed 32-bit value");
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
               FLT_MAX_EXP == 128, "Musket zoom requires IEEE binary32 floats");

/* CG_CalcFov divides elapsed zoom-in time by a pooled 200.0 double at
 * 0x34df4. Scale the elapsed time by 200 / MUSKET_ZOOM_IN_MS first, only
 * when cg.deployZoom == 1 (PM_ToggleDeploy's musket aiming mode).
 * Sniper zoom uses mode 2; zoom-out has its own unmodified division.
 *
 * Replace the preceding mov/fild pair rather than the division, keeping
 * its absolute operand and HIGHLOW relocation intact. The helper loads
 * elapsed EAX through the stack; the omitted local store is overwritten
 * by the original fstp at 0x34dfa before it is read again. EAX is scratch
 * here; other registers and the original x87 stack effect are preserved.
 *
 * Fragments occupy guarded int3 padding after returning functions. The
 * inner call's return address is consumed to locate cg.deployZoom without
 * adding relocations; both helper paths return to the original call site.
 */
static const code_rule_t patch_musket_zoom[] = {
    { CGAME, true, "musket zoom: elapsed zoom-in time", 0x34dec, 8,
      { 0x89, 0x44, 0x24, 0x10, 0xdb, 0x44, 0x24, 0x10 },
      { 0xe8, B4(0x348c4u - 0x34df1u), 0x90, 0x90, 0x90 } },
    { CGAME, true, "musket zoom: load elapsed time", 0x348c4, 10,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0x50,                           /* push eax */
        0xdb, 0x04, 0x24,               /* fild dword [esp] */
        0x58,                           /* pop eax */
        0xe8, B4(0x34821u - 0x348ceu) } },
    { CGAME, true, "musket zoom: select musket aiming", 0x34821, 15,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0x58,                           /* pop eax: image base + 0x348ce */
        0x83, 0xb8, B4(0xda0ee0u - 0x348ceu), 0x01,
        0x0f, 0x84, B4(0x35083u - 0x3482fu),
        0xc3 } },
};

static code_rule_t patch_musket_zoom_scale = {
    CGAME, true, "musket zoom: scale elapsed time", 0x35083, 12,
    { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
      0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
    { 0x68, 0, 0, 0, 0,                /* push 200.0f / duration */
      0xd8, 0x0c, 0x24,                /* fmul dword [esp] */
      0x83, 0xc4, 0x04,                /* add esp,4 */
      0xc3 }
};

PATCH_CONSTRUCTOR(configure_musket_zoom)
{
    const float scale = 200.0f / MUSKET_ZOOM_IN_MS;
    uint32_t bits;

    /* Encode the host's float bits as a little-endian x86 immediate. */
    memcpy(&bits, &scale, sizeof(bits));
    put32(patch_musket_zoom_scale.replacement + 1, bits);
}

/* The hook, selection fragments and scale helper must be enabled together. */
REGISTER_CODE_RULES(patch_musket_zoom)
REGISTER_CODE_RULE(patch_musket_zoom_scale)
