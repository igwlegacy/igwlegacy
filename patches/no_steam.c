#include "patches.h"

/* Remove the Steamworks dependency from both executables, so that neither
 * steam_api.dll nor what it loads (steamclient.dll, Steam.dll, tier0_s.dll,
 * vstdlib_s.dll) nor steam_appid.txt has to ship. The game modules do not
 * import Steam at all; only the client's and the dedicated server's
 * igwarlord.exe import steam_api.dll, 21 and 20 cdecl functions.
 *
 * Each import resolves to one stub, xor eax,eax; ret, which is the result
 * every function gives when Steam is not running:
 *   - SteamAPI_Init and SteamGameServer_Init return false. The client
 *     layer (client 0x5a950, server 0x598b0) then prints "SteamInit error:
 *     SteamAPI_Init() failed", leaves its steam ids 0 and never sets its
 *     initialized flag or creates the achievement and leaderboard objects.
 *     The game server init (client 0x725e0, server 0x71500) prints
 *     "SteamGameServer_Init call failed" and leaves its server object NULL
 *     (client 0x10cbd68, server 0xa64794), which its per-frame P2P and
 *     auth handling, "steamserverstatus" and shutdown all check first.
 *   - The interface accessors (SteamUser, SteamApps, SteamGameServer, ...)
 *     return NULL, as steam_api.dll's own do before a successful init:
 *     each just returns a global that only init fills. Callers outside
 *     those gates test the pointer, e.g. push-to-talk voice and shutdown.
 *   - Callback registration, SteamAPI_RunCallbacks, SteamAPI_Shutdown,
 *     SteamGameServer_RunCallbacks and SteamGameServer_Shutdown do nothing.
 * That is exactly the path the client already takes whenever Steam is not
 * running. The dedicated server, which ships steamclient.dll, so far got a
 * running Steam game server; without it, servers only lose the Steam
 * master server listing and the Steam P2P auth exchange. Connecting does
 * not depend on either: the server takes the steamId1/steamId2 userinfo
 * keys as sent (0 without Steam). cl_expansionSE no longer gets set from
 * Steam's DLC check; default.cfg sets it already.
 *
 * The stubs take int3 padding behind each executable's Steam game server
 * frame function, which only Steam code surrounds.
 */
static const import_rule_t patch_no_steam[] = {
    { ENGINE, "no Steam in the client", "steam_api.dll", 21, 0x72c17u },
    { SERVER, "no Steam in the dedicated server", "steam_api.dll", 20, 0x71b4au },
};

REGISTER_IMPORT_RULES(patch_no_steam)
