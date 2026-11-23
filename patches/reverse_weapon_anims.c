#include "patches.h"

/* Weapon scripts play an activity backwards with a negative FPS, e.g.
 * "ACT_UNDEPLOY 140 5 0 -25" in weapon_lrifle.txt. The parser
 * (cgamex86:0x49840) stores frameLerp = initialLerp = 1000 / fps, so both
 * become negative. The view model's lerp frame runner (0x35a60) and its
 * animation setter (0x359b0) are id's CG_RunLerpFrame without the reversed
 * and flipflop cases:
 *
 *   animationTime = frameTime + initialLerp;    // setter
 *   frameTime = oldFrameTime + frameLerp;       // runner
 *   f = (frameTime - animationTime) / frameLerp;
 *   frame = firstFrame + f;
 *
 * A negative frameLerp makes frameTime go backwards, so on every rendered
 * frame it lands behind cg.time and is clamped to it. oldFrame and frame
 * then always span exactly one rendered frame and backlerp stays 0: the
 * model snaps from keyframe to keyframe instead of interpolating, and the
 * blend into the activity's first frame is skipped. The negative quotient
 * f does count frames down from firstFrame, but it never reaches
 * numFrames, so the activity also runs past its last frame.
 *
 * Time the activity with |frameLerp| and |initialLerp| instead, so lerping
 * and the end-of-activity hold work as for positive FPS, and mirror only
 * the frame index: firstFrame, firstFrame - 1, ... firstFrame - numFrames
 * + 1. That is the frame order the scripts were written for (each reverse
 * activity starts where its forward counterpart ends). The script data
 * itself is left alone; the frame event dispatcher (0x48350) and other
 * readers keep seeing the values the retail DLL stores.
 *
 * Every hook is a relative call into the injected section, and the
 * helpers use no absolute addresses. None of the overwritten bytes is
 * covered by a base relocation, and none is a jump target.
 */
static code_rule_t patch_reverse_weapon_anims_hooks[] = {
    /* Setter: mov eax,[eax+0x10]; add eax,[edi+0xc] with EAX = anim,
     * computing animationTime = frameTime + initialLerp.
     */
    { CGAME, true, "reverse weapon anims: initial lerp", 0x359f6, 6,
      { 0x8b, 0x40, 0x10, 0x03, 0x47, 0x0c },
      { 0xe8, 0, 0, 0, 0, 0x90 } },
    /* Runner: mov eax,[ecx+0xc]; test eax,eax with ECX = anim. The
     * following je skips frameLerp 0, and EAX is added to oldFrameTime.
     */
    { CGAME, true, "reverse weapon anims: frame time", 0x35ae4, 5,
      { 0x8b, 0x41, 0x0c, 0x85, 0xc0 },
      { 0xe8, 0, 0, 0, 0 } },
    /* Runner: mov eax,[edi+0xc]; sub eax,esi; cdq; idiv [ecx+0xc], the
     * frame count since animationTime (ESI), before speedScale applies.
     */
    { CGAME, true, "reverse weapon anims: frame count", 0x35b05, 9,
      { 0x8b, 0x47, 0x0c, 0x2b, 0xc6, 0x99, 0xf7, 0x79, 0x0c },
      { 0xe8, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 } },
    /* Runner: mov ecx,[ecx]; add ecx,eax; mov [edi+8],ecx stores
     * lf->frame = firstFrame + f. EAX and ECX are reloaded afterwards.
     */
    { CGAME, true, "reverse weapon anims: frame index", 0x35b45, 7,
      { 0x8b, 0x09, 0x03, 0xc8, 0x89, 0x4f, 0x08 },
      { 0xe8, 0, 0, 0, 0, 0x90, 0x90 } },
};

static bool build_reverse_weapon_anims(payload_t *payload, uint32_t rva,
                                       uint32_t next, uint32_t *entry)
{
    /* The four helpers need well under 128 bytes. */
    if (!payload_reserve(payload, 128, "Reverse weapon animation"))
        return false;
    *entry = emit_entry(payload, next);

    /* animationTime = frameTime + |initialLerp|; flags are not consumed. */
    uint32_t initial = EMIT(0x8b, 0x40, 0x10,    /* mov eax,[eax+0x10] */
                            0x85, 0xc0,          /* test eax,eax */
                            0x79, 0x02,          /* jns +2 */
                            0xf7, 0xd8,          /* neg eax */
                            0x03, 0x47, 0x0c,    /* add eax,[edi+0xc] */
                            0xc3);

    /* EAX = |frameLerp|. ZF must reflect EAX == 0 for the caller's je:
     * test sets it on the positive path, neg on the negated one.
     */
    uint32_t frame_time = EMIT(0x8b, 0x41, 0x0c, /* mov eax,[ecx+0xc] */
                               0x85, 0xc0,       /* test eax,eax */
                               0x79, 0x02,       /* jns +2 */
                               0xf7, 0xd8,       /* neg eax */
                               0xc3);

    /* EAX = (frameTime - animationTime) / |frameLerp|. ESI held
     * animationTime and is reloaded with numFrames right afterwards, so
     * it can carry the divisor. EDX is clobbered by cdq as before.
     */
    uint32_t frame_count = EMIT(0x8b, 0x47, 0x0c, /* mov eax,[edi+0xc] */
                                0x2b, 0xc6,       /* sub eax,esi */
                                0x8b, 0x71, 0x0c, /* mov esi,[ecx+0xc] */
                                0x85, 0xf6,       /* test esi,esi */
                                0x79, 0x02,       /* jns +2 */
                                0xf7, 0xde,       /* neg esi */
                                0x99,             /* cdq */
                                0xf7, 0xfe,       /* idiv esi */
                                0xc3);

    /* lf->frame = firstFrame + f, or firstFrame - f for a negative FPS. */
    uint32_t frame_index = EMIT(0x83, 0x79, 0x0c, 0x00, /* cmp [ecx+0xc],0 */
                                0x7d, 0x02,             /* jge +2 */
                                0xf7, 0xd8,             /* neg eax */
                                0x03, 0x01,             /* add eax,[ecx] */
                                0x89, 0x47, 0x08,       /* mov [edi+8],eax */
                                0xc3);

    /* image_patch builds the payload before applying guarded code rules,
     * so their relative call targets can use this section's actual RVA.
     */
    const uint32_t targets[] = { initial, frame_time, frame_count, frame_index };
    _Static_assert(ARRAY_SIZE(targets) ==
                   ARRAY_SIZE(patch_reverse_weapon_anims_hooks),
                   "every hook needs a helper");
    for (size_t i = 0; i < ARRAY_SIZE(patch_reverse_weapon_anims_hooks); ++i) {
        code_rule_t *hook = &patch_reverse_weapon_anims_hooks[i];
        put32(hook->replacement + 1, rva + targets[i] - (hook->rva + 5u));
    }
    return true;
}

static const payload_feature_t reverse_weapon_anims = {
    CGAME, "Reverse weapon animation lerping", build_reverse_weapon_anims,
};

/* The payload and its hooks must be enabled together. */
REGISTER_CODE_RULES(patch_reverse_weapon_anims_hooks)
REGISTER_PAYLOAD_FEATURE(reverse_weapon_anims)
