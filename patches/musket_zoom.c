#include "patches.h"

#include <float.h>
#include <string.h>

/* Right-click musket zoom timings, in milliseconds (stock: 200, delay: 0).
 * Edit, rebuild and install the patched cgamex86.dll on the client.
 * Use 1 for a virtually immediate zoom; zoom-out still takes 150 ms.
 * The delay holds the normal FOV before the transition starts. Both times
 * begin when aiming is accepted; existing weapon cooldowns still apply.
 * These control the view's FOV transition, not the weapon animation.
 */
#define MUSKET_ZOOM_IN_MS 400
#define MUSKET_ZOOM_DELAY_MS 0

_Static_assert(MUSKET_ZOOM_IN_MS >= 1 && MUSKET_ZOOM_IN_MS <= INT32_MAX,
               "Musket zoom-in time must be a positive signed 32-bit value");
_Static_assert(MUSKET_ZOOM_DELAY_MS >= 0 && MUSKET_ZOOM_DELAY_MS <= INT32_MAX,
               "Musket zoom delay must be a nonnegative signed 32-bit value");
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
               FLT_MAX_EXP == 128, "Musket zoom requires IEEE binary32 floats");

/* CG_CalcFov divides elapsed zoom-in time by a pooled 200.0 double at
 * 0x34df4. First subtract the delay, clamp to zero, then scale the elapsed
 * time by 200 / MUSKET_ZOOM_IN_MS, only when cg.deployZoom == 1
 * (PM_ToggleDeploy's musket aiming mode).
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
        0x0f, 0x84, B4(0x33801u - 0x3482fu),
        0xc3 } },
    { CGAME, true, "musket zoom: subtract start delay", 0x33801, 14,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0x68, B4(MUSKET_ZOOM_DELAY_MS), /* push delay */
        0xda, 0x24, 0x24,              /* fisub dword [esp] */
        0x58,                          /* pop eax */
        0xe9, B4(0x33841u - 0x3380fu) } },

    /* A negative elapsed time would widen the FOV during the delay.
     * Keep max(elapsed - delay, 0) on the x87 stack instead.
     */
    { CGAME, true, "musket zoom: hold view during delay", 0x33841, 15,
      { 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc,
        0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc, 0xcc },
      { 0xd9, 0xee,                    /* fldz */
        0xd9, 0xc9,                    /* fxch st(1) */
        0xdb, 0xf1,                    /* fcomi st,st(1) */
        0xda, 0xc1,                    /* fcmovb st,st(1) */
        0xdd, 0xd9,                    /* fstp st(1) */
        0xe9, B4(0x35083u - 0x33850u) } },
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
