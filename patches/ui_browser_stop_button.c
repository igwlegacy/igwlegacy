#include "patches.h"

/* The server browser's refresh button turns into STOP (item id 0x2d) while
 * a refresh runs. Its case at RVA 0x345c5 of the browser's event handler
 * calls ArenaServers_StopRefresh at RVA 0x32e70, which clears the refresh
 * flag at VA 0x100de528 and tail-calls ArenaServers_UpdateMenu at RVA
 * 0x32160. When no server answered yet, UpdateMenu's IGW-added master
 * server fallback (RVA 0x32a9b) sees an idle browser with an empty list
 * and, as long as ui_useMasterServer is 1 and the master server number at
 * VA 0x100d8294 is below 2, switches to master server 2 and starts a new
 * refresh at once. Clicking STOP before any server answered, which is
 * every refresh of an empty LAN, therefore only restarts the scan. The
 * fallback is meant for refreshes that time out on their own (RVA
 * 0x3315c), which go through StopRefresh as well.
 *
 * Make STOP set the master server number to 2 before stopping, so the
 * fallback is skipped and the browser shows "No Servers Found." right
 * away. The REFRESH case at RVA 0x34585 already resets the number to 1
 * (and ui_useMasterServer to 1) whenever it is 2 or more, so the next
 * refresh behaves as before, fallback included. Only a refresh started by
 * switching sources right after a stop keeps the 2, which affects nothing
 * but the master number shown in its Internet scanning label.
 *
 * The STOP case's two stores after the call only repeat what StopRefresh
 * itself sets (id 0x1f and "#refresh"), so they make room. The new store
 * starts at RVA 0x345ca, where its address operand reuses the relocation
 * slot of the old id store's address. The handler's jump table entry for
 * id 0x2d (RVA 0x34950, itself relocated) moves to that store, and a short
 * jump after it goes back to RVA 0x345c5, whose call becomes a tail jump
 * to StopRefresh with the same displacement. The "#refresh" store's
 * remaining bytes stay in place and are no longer reached, so all its
 * relocation slots stay intact.
 */
static const code_rule_t patch_browser_stop_button[] = {
    { UI, true, "browser stop button: enter at master server store", 0x34950, 4,
      { B4(0x100345c5u) },
      { B4(0x100345cau) } },
    { UI, true, "browser stop button: tail jump to StopRefresh", 0x345c5, 5,
      { 0xe8, B4(0xffffe8a6u) },           /* call ArenaServers_StopRefresh */
      { 0xe9, B4(0xffffe8a6u) } },         /* jmp ArenaServers_StopRefresh */
    { UI, true, "browser stop button: skip master server fallback", 0x345ca, 10,
      { 0xc7, 0x05, B4(0x100d7b58u), B4(0x1fu) },  /* mov [button id],0x1f */
      { 0xc7, 0x05, B4(0x100d8294u), B4(2u) } },   /* mov [master server],2 */
    { UI, true, "browser stop button: jump back to StopRefresh", 0x345d4, 2,
      { 0xc7, 0x05 },                      /* mov [button text],"#refresh" */
      { 0xeb, 0xef } },                    /* jmp 0x345c5 */
};

REGISTER_CODE_RULES(patch_browser_stop_button)
