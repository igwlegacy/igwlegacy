## Configuration

Each functionality has a separate file in `patches/` at the repository root:

| File | Functionality |
| --- | --- |
| [run_pitch.c](../../patches/run_pitch.c) | Pitch from forward and backward speed |
| [run_roll.c](../../patches/run_roll.c) | Roll from sideways speed |
| [bob_pitch.c](../../patches/bob_pitch.c) | Walking pitch bob |
| [bob_roll.c](../../patches/bob_roll.c) | Walking roll bob |
| [bob_up.c](../../patches/bob_up.c) | Walking eye-height bob |
| [screen_shake.c](../../patches/screen_shake.c) | Explosion and death shake |
| [damage_kick.c](../../patches/damage_kick.c) | Damage pitch and roll, including the return phase |
| [landing_dip.c](../../patches/landing_dip.c) | Eye-height dip on landing |
| [main_menu_ad.c](../../patches/main_menu_ad.c) | Main-menu promotional panel, ticker, and update request |

Edit the rule definitions in the corresponding file and rebuild. `src/rules.c` registers the rules by reference and validates them. Comment out a registry reference to omit that rule. For a new functionality, add its `.c` file to `patches/meson.build`, declare its rules in `patches/patches.h`, and reference them in `src/rules.c`. One functionality can supply several CVAR or instruction rules.

| Setting                                 | Implementation                                                            |
| ---                                     | ---                                                                       |
| `cg_runpitch`, `cg_runroll`             | Force `"0"`                                                               |
| `cg_bobpitch`, `cg_bobroll`, `cg_bobup` | Force `"0"`                                                               |
| Explosion and death shake               | Return immediately from the shake starter                                 |
| Damage pitch and roll                   | Read an existing zero constant; HUD damage indicators retain their values |
| Landing dip                             | Read the zero constant                                                    |
| Main-menu promotion                     | Omit the panel's menu items, ticker, and update request                    |

Weapon recoil and weapon model sway retain their original behavior. Recoil changes the actual aim in shared player movement code and would require corresponding server and client patches. The preset has no active `gamex86` rules, so its output is an unchanged copy until you enable a rule for that module.

### CVAR mapping

Edit the `cvar_rule_t` definition in the corresponding functionality file, then rebuild. Each entry specifies the module that registers the CVAR, its name, a string value, and the mode:

```c
{ CGAME, "cg_bobup", "0", CVAR_FORCE },
{ CGAME, "cg_fov", "100", CVAR_DEFAULT },
{ GAME, "g_knockback", "600", CVAR_DEFAULT },
{ GAME, "g_bodytime", "0", CVAR_FORCE },
```

The last three entries above are examples, not active preset rules. The registry references the feature definitions; its null sentinel permits an empty mapping. Remove individual registry references to leave those CVARs alone. String values can be longer than the original defaults because the patcher appends new storage instead of modifying shared string literals. Names and values have a 255-byte limit, and there can be at most 128 rules. Duplicate names within one module are rejected without regard to ASCII case.

`CVAR_DEFAULT` replaces the fallback string passed to the engine during registration. Existing saved settings and subsequent changes still take precedence. It does not force a value.

`CVAR_FORCE` sets the engine CVAR before registration, substitutes its default, adds `CVAR_ROM` to prevent ordinary console changes, and records the registered engine handle. Before a matching `CVAR_UPDATE`, it sets the engine value again so the engine fills the `vmCvar_t` integer, float, and string consistently. It also substitutes the configured value in matching `CVAR_SET` calls from that DLL. Multiple `vmCvar_t` registrations of one CVAR are recognized by their common engine handle. A null `vmCvar_t` registration still gets the default, forced set, and ROM flag.

Rules activate when the selected DLL registers the named CVAR. They cover the main tables and individual registrations such as `G_RegisterSkillCvars`; a misspelled name, an unexecuted registration, or a CVAR registered only by the engine or another module will not activate a registration rule. Rules do not override arbitrary direct writes to memory or engine changes made by another module between updates. Apply consistent rules to both modules if they register the same CVAR.

### Instruction and function patches

Each `code_rule_t` definition in a functionality file holds an enable switch, an RVA, expected bytes, and replacement bytes for an instruction patch. Set `enabled` to `false` to skip a patch. Each rule can cover up to 16 bytes; enabled overlapping ranges are rejected. The expected bytes must match before output is written.

The supplied shake patch replaces the first opcode with `ret` and checks the rest of the original instructions. This is valid because reverse engineering established that this function returns void and its callers clean up the stack. A function that returns a value, uses a different calling convention, or has required side effects needs a different patch. Address operands in replacement code must keep the existing relocation positions valid; arbitrary new absolute addresses need relocation support that the generic byte patch table does not provide.

### Main-menu promotion

In the retail UI DLL, the menu initializer at RVA `0x1e390` assigns `textures/ui/motdsmall_IGM` to the bitmap items at VAs `0x10100ea8` and `0x10100dc4`. The generic bitmap renderer at RVA `0x2c8a0` loads their shader names through the registration wrapper at RVA `0x3a2f0`. The engine resolves the extensionless shader name to the texture asset. The initializer also supplies the default Marauders promotional text and `http://igmarauders.isotx.com` link. The response parser can select other images through the `igm`, `igl`, `igw`, and `isotx` topic table at VA `0x1006d484`.

The patch omits the nine promotional menu-item registration calls while retaining their argument pushes and stack cleanup. It returns from the menu draw callback at RVA `0x1e1fc` after the normal menu/error draw, releasing its reserved stack word before returning, to skip response handling and the separately rendered ticker. The request starter at RVA `0x1dde0` returns before calling engine syscall 121 with request type 5. Original absolute operands and their relocation slots remain intact.

## DLL support and implementation

Only the retail files are accepted:

| DLL            | Bytes  | PE timestamp | SHA-256                                                            |
| ---            | ---    | ---          | ---                                                                |
| `gamex86.dll`  | 930304 | `0x4fbdcafd` | `61db8f1ca15bd6a8cf68ee98ce4af4420c7347fecdc327b2e557d7502c0931fe` |
| `cgamex86.dll` | 911360 | `0x4fbdcb0d` | `d095cc681067f88827e7229953bcc9d8f66129d3fd680a034bb4266382f60985` |
| `uix86.dll`    | 499712 | `0x4fbdcaee` | `90ab043eb8299c5852594a92bc5e6f958b97f6af55d9d0bcbbe8b81584f43dae` |

The program checks file size and a whole-file FNV-1a 64-bit fingerprint, then verifies PE32/i386 headers and instruction guards. The SHA-256 values above are provided for independent identification. The fingerprint guards against accidental version differences, not malicious collisions. Supporting another release requires verifying its layout and patch sites and adding a profile in `src/pe.c`.

For a module with CVAR rules, the patcher adds a writable and executable `.igwmonk` section with strings, handle storage, and an i386 syscall dispatcher. It redirects the `dllEntry` export to a position independent initializer that saves the real engine syscall pointer and installs the dispatcher in the DLL's original syscall global. The original `dllEntry` instructions and their relocation entries stay untouched. The PE loader entry point, `vmMain` export, and unrelated syscall arguments remain intact.

The dispatcher intercepts the standard C ABI at the engine boundary, avoiding the custom internal register conventions introduced by MSVC 9 whole program optimization. It uses `call`/`pop` and section-relative offsets, so its code and data need no additional base relocations. Existing relocations still apply to the camera instruction operands. The patcher converts RVAs through PE section headers, updates section sizes and `SizeOfImage`, and recomputes the PE checksum.
