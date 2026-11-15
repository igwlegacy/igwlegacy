#include "patches.h"

/* Com_Init (in igwarlord.exe) queues "cinematic\n" when no "+" startup
 * commands were given on the command line. The "cinematic" command handler
 * at RVA 0x6550 ignores its arguments and always plays the isotx intro
 * (isotx.roq, or isotx_wide.roq for widescreen modes).
 *
 * Turn the conditional skip after Com_AddStartupCommands (RVA 0x20b60) into
 * an unconditional one, so the Cbuf_AddText call is never reached. The
 * command itself stays registered for manual use. The skipped mov keeps its
 * absolute operand and HIGHLOW relocation untouched.
 */
static const code_rule_t patch_skip_intro =
    { ENGINE, true, "skip intro cinematic", 0x2331b, 2,
      { 0x75, 0x0a }, { 0xeb, 0x0a } };

REGISTER_CODE_RULE(patch_skip_intro)
