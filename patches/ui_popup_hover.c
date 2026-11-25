#include "patches.h"

#include <stdio.h>

/* An opened dropdown's popup list, menu item type 12, only changes its
 * selection by clicking an entry or by keys and the mouse wheel. Make it
 * follow the mouse as well, the way the wheel does.
 *
 * UI_MouseEvent (uix86 RVA 0x8710) calls RVA 0x8600 at RVA 0x8835 after
 * moving the cursor. That check returns nonzero while the cursor is still
 * on the focused item, for an open list on its whole drop-down area, and
 * UI_MouseEvent then keeps the focus. The hook calls it in the same way
 * and, while it returns nonzero for an open list, does what a click does
 * in the list's key handler (RVA 0x2cfd5) short of closing it: it takes
 * the row under the cursor, (cursor y / y scale - item y) / 16 rows below
 * the first shown entry, and, if that is an entry other than the current
 * one, remembers the old index, selects the new one and calls the item's
 * callback with event 1, as the keys and the wheel do. A click on the
 * already selected entry then just closes the list.
 */
enum {
    UI_FOCUS_CHECK = 0x8600u,   /* cursor still on the focused item? */
    UI_FTOL = 0x404d0u,         /* st(0) -> eax, clobbers edx */
    UI_ACTIVE_MENU = 0x107234u, /* uis.activemenu */
    UI_CURSOR_Y = 0x10722cu,    /* uis.cursory, in pixels */
    UI_Y_SCALE = 0x109ee4u,     /* pixels per 480-unit row, a float */

    MENU_CURSOR = 0x00,         /* menuframework: focused item index */
    MENU_ITEMS = 0x10,          /* menuframework: item pointers */

    ITEM_TYPE = 0x00,
    ITEM_Y = 0x10,              /* float, in 480-unit coordinates */
    ITEM_CALLBACK = 0x3c,       /* void (*)(void *item, int event) */
    ITEM_TYPE_LIST = 12,
    LIST_OLD_VALUE = 0x130,
    LIST_CUR_VALUE = 0x134,
    LIST_NUM_ITEMS = 0x138,
    LIST_TOP = 0x13c,
    LIST_OPEN = 0x554,

    ROW_SHIFT = 4,              /* rows are 16 units high */
    RESERVE = 256u,
};

/* The call of the focus check in UI_MouseEvent; the replacement's rel32 is
 * resolved once the payload has a known RVA.
 */
static code_rule_t patch_popup_hover_hooks[] = {
    { UI, true, "popup hover: focus check", 0x8835, 5,
      { 0xe8, B4(UI_FOCUS_CHECK - 0x883au) }, { 0xe8 } },
};

/* op reg,[ebx+disp32], ebx holding the runtime address of the RVA base,
 * so that the operand addresses the image RVA target.
 */
static void ebx_op(payload_t *payload, unsigned char opcode, unsigned reg,
                   uint32_t base, uint32_t target)
{
    EMIT(opcode, (unsigned char)(0x83 | reg << 3), B4(target - base));
}

static bool build_ui(payload_t *payload, uint32_t rva, uint32_t next, uint32_t *entry)
{
    if (!payload_reserve(payload, RESERVE, "Popup hover"))
        return false;
    *entry = emit_entry(payload, next);

    uint32_t hover = (uint32_t)payload->size;
    emit_call(payload, rva, UI_FOCUS_CHECK);
    EMIT(0x85, 0xc0);                       /* test eax,eax */
    uint32_t left = emit_skip_if(payload, 0x75); /* jnz: still on the item */
    EMIT(0xc3);                             /* ret */
    emit_land(payload, left);

    /* Keep the check's result and UI_MouseEvent's registers. */
    EMIT(0x50, 0x56, 0x53);                 /* push eax; push esi; push ebx */
    uint32_t base = emit_anchor(payload, rva, EBX);

    /* esi = the focused item, which the check found valid. */
    ebx_op(payload, 0x8b, ECX, base, UI_ACTIVE_MENU); /* mov ecx,[ebx+d] */
    EMIT(0x8b, 0x41, MENU_CURSOR,           /* mov eax,[ecx] */
         0x8b, 0x74, 0x81, MENU_ITEMS);     /* mov esi,[ecx+eax*4+0x10] */

    /* Only an open popup list. */
    uint32_t done[6];
    size_t done_count = 0;
    EMIT(0x83, 0x7e, ITEM_TYPE, ITEM_TYPE_LIST); /* cmp dword [esi],12 */
    done[done_count++] = emit_skip_if(payload, 0x75); /* jne */
    EMIT(0x83, 0xbe, B4(LIST_OPEN), 0x00);  /* cmp dword [esi+0x554],0 */
    done[done_count++] = emit_skip_if(payload, 0x74); /* je */

    /* ecx = the row under the cursor, in 480-unit coordinates as the
     * click computes it.
     */
    ebx_op(payload, 0xdb, 0, base, UI_CURSOR_Y);   /* fild dword [ebx+d] */
    ebx_op(payload, 0xd8, 6, base, UI_Y_SCALE);    /* fdiv dword [ebx+d] */
    emit_call(payload, rva, UI_FTOL);
    EMIT(0x50,                              /* push eax */
         0xd9, 0x46, ITEM_Y);               /* fld dword [esi+0x10] */
    emit_call(payload, rva, UI_FTOL);
    EMIT(0x59,                              /* pop ecx */
         0x29, 0xc1);                       /* sub ecx,eax */
    done[done_count++] = emit_skip_if(payload, 0x78); /* js: above the list */
    EMIT(0xc1, 0xf9, ROW_SHIFT,             /* sar ecx,4 */
         0x03, 0x8e, B4(LIST_TOP));         /* add ecx,[esi+0x13c] */

    /* Past the last entry, or already selected: nothing to do. */
    EMIT(0x3b, 0x8e, B4(LIST_NUM_ITEMS));   /* cmp ecx,[esi+0x138] */
    done[done_count++] = emit_skip_if(payload, 0x7d); /* jge */
    EMIT(0x8b, 0x86, B4(LIST_CUR_VALUE),    /* mov eax,[esi+0x134] */
         0x39, 0xc8);                       /* cmp eax,ecx */
    done[done_count++] = emit_skip_if(payload, 0x74); /* je */

    /* Select it and tell the menu, like the wheel. */
    EMIT(0x89, 0x86, B4(LIST_OLD_VALUE),    /* mov [esi+0x130],eax */
         0x89, 0x8e, B4(LIST_CUR_VALUE),    /* mov [esi+0x134],ecx */
         0x8b, 0x46, ITEM_CALLBACK,         /* mov eax,[esi+0x3c] */
         0x85, 0xc0);                       /* test eax,eax */
    done[done_count++] = emit_skip_if(payload, 0x74); /* jz */
    EMIT(0x6a, 0x01, 0x56,                  /* push 1; push esi */
         0xff, 0xd0,                        /* call eax */
         0x83, 0xc4, 0x08);                 /* add esp,8 */

    for (size_t i = 0; i < done_count; ++i) {
        if (payload->size - (done[i] + 2u) > 127u) {
            fputs("Popup hover payload: short branch out of range\n", stderr);
            payload->failed = true;
        }
        emit_land(payload, done[i]);
    }
    EMIT(0x5b, 0x5e, 0x58, 0xc3);           /* pop ebx; pop esi; pop eax; ret */

    uint32_t targets[ARRAY_SIZE(patch_popup_hover_hooks)] = { hover };
    resolve_hooks(patch_popup_hover_hooks, ARRAY_SIZE(targets), targets, rva);
    return true;
}

static const payload_feature_t popup_hover = { UI, "Popup hover", build_ui };

/* The payload and its hook must be enabled together. */
REGISTER_CODE_RULES(patch_popup_hover_hooks)
REGISTER_PAYLOAD_FEATURE(popup_hover)
