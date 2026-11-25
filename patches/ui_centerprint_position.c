#include "patches.h"

/* IGW's centerprint draws its text on a dark band and treats the y argument
 * as an offset from the screen centre in 480-line units: CG_DrawCenterString
 * (0x10a70) and its text helper (0x10810) scale |y| by vidHeight / 480 and
 * subtract it for a negative y, add it otherwise. Callers passing 0 therefore
 * cover the crosshair.
 *
 * Move exactly those callers up by CENTERPRINT_RAISE, a quarter of the
 * screen height. Callers that already chose a lower position (warnings at
 * 100 and 170, "Use %s" at 144, the respawn countdown at 210) keep it.
 * Every y is pushed as a sign-extended imm8 (6a xx), so only that byte
 * changes; no operands with relocations are touched.
 */
#define CENTERPRINT_RAISE 128

/* The imm8 written by `push imm8`, sign-extended by the CPU. */
#define CENTERPRINT_Y ((unsigned char)(-CENTERPRINT_RAISE))

_Static_assert(CENTERPRINT_RAISE > 0 && CENTERPRINT_RAISE <= 128,
               "the raise must fit a negative sign-extended imm8");

static const code_rule_t patch_centerprint_position[] = {
    /* 0x121c0: the round start countdown ("ROUND STARTS IN X"), through
     * CG_CenterPrintTimed with its own 30 ms lifetime.
     */
    { CGAME, true, "centerprint position: round start countdown", 0x1227c, 2,
      { 0x6a, 0x00 },
      { 0x6a, CENTERPRINT_Y } },
    /* The round end message (#winmsg-atelia/-confed) of the function just
     * before, which also sets cg_winteam.
     */
    { CGAME, true, "centerprint position: round winner", 0x12152, 2,
      { 0x6a, 0x00 },
      { 0x6a, CENTERPRINT_Y } },
    /* CG_EntityEvent: the airstrike announcements. The incoming one's y
     * is pushed ahead of the va() arguments and left on the stack.
     */
    { CGAME, true, "centerprint position: airstrike incoming", 0x23c38, 2,
      { 0x6a, 0x00 },
      { 0x6a, CENTERPRINT_Y } },
    { CGAME, true, "centerprint position: airstrike prevented", 0x23c53, 2,
      { 0x6a, 0x00 },
      { 0x6a, CENTERPRINT_Y } },
    /* CG_ServerCommand: the server's "cp" command. */
    { CGAME, true, "centerprint position: server cp", 0x3178b, 2,
      { 0x6a, 0x00 },
      { 0x6a, CENTERPRINT_Y } },
};

/* The death screen tip ("#dead-tip3", "Kill the Officer to hurt Enemy
 * Morale", and the rest) does not go through the centerprint: with
 * cg_showhelp 1, CG_DrawDeathScreen (0xd0e0) draws a dark band itself,
 * centred on the screen by 0x7aad0 (y = vidHeight / 2 - height / 2), and
 * then the tip on it through the text helper with a y offset of 0 in EDX
 * (xor edx,edx). Raise both by CENTERPRINT_RAISE, like the centerprints
 * above. Neither fits in place, so both calls go through helpers in the
 * injected section. The overwritten operands are rel32 call targets
 * without base relocations, and neither call is a jump target.
 */
static code_rule_t patch_centerprint_death_tip_hooks[] = {
    /* push edx; push edi; call 0x7aad0, with EDX = &y and EDI = the
     * band's height, which the band is drawn with afterwards.
     */
    { CGAME, true, "centerprint position: death tip band", 0xda23, 5,
      { 0xe8, B4(0x7aad0u - 0xda28u) },
      { 0xe8, 0, 0, 0, 0 } },
    /* xor ecx,ecx; xor edx,edx; call 0x10810: no color, so the tip fades
     * with "SPAWNING IN", and no y offset.
     */
    { CGAME, true, "centerprint position: death tip text", 0xdaa5, 5,
      { 0xe8, B4(0x10810u - 0xdaaau) },
      { 0xe8, 0, 0, 0, 0 } },
};

#define CENTERPRINT_STR_(x) #x
#define CENTERPRINT_STR(x) CENTERPRINT_STR_(x)

static bool build_centerprint_death_tip(payload_t *payload, uint32_t rva,
                                        uint32_t next, uint32_t *entry)
{
    /* Both helpers and the string need well under 96 bytes. */
    if (!payload_reserve(payload, 96, "Centerprint position"))
        return false;
    *entry = emit_entry(payload, next);

    /* The y helper 0x7ac50 skips a leading non-digit, so "128" scales
     * exactly like the "-128" the text helper formats from EDX, and band
     * and tip stay aligned.
     */
    uint32_t raise = emit_string(payload, CENTERPRINT_STR(CENTERPRINT_RAISE));

    /* Called with the band's height at [esp+4] and &y at [esp+8]: centre
     * the band as before, then subtract the scaled raise from y. EAX, ECX
     * and EDX are free at the call site; 0x7ac50 keeps EBX, ESI and EDI.
     */
    uint32_t band = (uint32_t)payload->size;
    EMIT(0xff, 0x74, 0x24, 0x08,            /* push [esp+8] (&y) */
         0xff, 0x74, 0x24, 0x08);           /* push [esp+8] (height) */
    emit_call(payload, rva, 0x7aad0u);           /* y = vidHeight / 2 - height / 2 */
    EMIT(0x83, 0xc4, 0x08,                  /* add esp,8 */
         0x6a, 0x00);                       /* push 0, the scaled raise */
    emit_load_address(payload, rva, EAX, raise);
    EMIT(0x89, 0xe2);                       /* mov edx,esp */
    emit_call(payload, rva, 0x7ac50u);           /* [esp] = raise * vidHeight / 480 */
    EMIT(0x58,                              /* pop eax */
         0x8b, 0x54, 0x24, 0x08,            /* mov edx,[esp+8] (&y) */
         0x29, 0x02,                        /* sub [edx],eax */
         0xc3);

    /* Draw the tip with the y offset of the raised centerprints. The
     * stack is that of the original call, so 0x10810 returns to it.
     */
    uint32_t text = (uint32_t)payload->size;
    EMIT(0xba, B4((uint32_t)-CENTERPRINT_RAISE));  /* mov edx,-raise */
    emit_jump(payload, rva, 0x10810u);

    const uint32_t targets[] = { band, text };
    _Static_assert(ARRAY_SIZE(targets) ==
                   ARRAY_SIZE(patch_centerprint_death_tip_hooks),
                   "every hook needs a helper");
    resolve_hooks(patch_centerprint_death_tip_hooks, ARRAY_SIZE(targets),
                  targets, rva);
    return true;
}

static const payload_feature_t centerprint_death_tip = {
    CGAME, "Centerprint position: death tip", build_centerprint_death_tip,
};

REGISTER_CODE_RULES(patch_centerprint_position)
/* The payload and its hooks must be enabled together. */
REGISTER_CODE_RULES(patch_centerprint_death_tip_hooks)
REGISTER_PAYLOAD_FEATURE(centerprint_death_tip)
