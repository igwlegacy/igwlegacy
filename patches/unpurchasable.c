#include "patches.h"

#include <string.h>

/* Anything priced at UNPURCHASABLE or more by the skill config cannot be
 * bought: weapons (sk_weapcost_*), player upgrades (sk_upgrade_*), RTS
 * structures (sk_cost_*) and structure upgrades. The client greys those
 * entries out in its menus, and the server refuses them.
 *
 * G_AddMoney (gamex86:0x44070) keeps money within 0..9999, so 10000 is the
 * lowest price nobody could ever pay. Structures and their upgrades are
 * already out of reach on the server: Cmd_Build and the structure upgrade
 * compare the full price with the money. Weapons and upgrades are not:
 * G_CannotBuyWeapons and G_CannotBuyUpgrades credit three quarters of the
 * price of everything owned but not asked for, which "menu_weap" and
 * "menu_upgrade" with a single bit, or "pickinfo" and "clienttalents"
 * with a mask, can raise above 9999.
 *
 * Retail buttons have four states, each one a frame of their texture:
 * 0 normal (green on the buy buttons), 1 disabled (red, clicks do nothing),
 * 2 hovered (light grey) and 3 pressed or owned (dark grey). The weapon and
 * upgrade buttons get a fifth one, UNPURCHASABLE_STATE, which every retail
 * input handler ignores like the disabled one and which the button draw
 * hooks render in black; their price labels lose their text
 * meanwhile. The RTS build and structure upgrade buttons get a sixth
 * state, UNPURCHASABLE_ICON_STATE, for the same, as their icons order
 * their frames differently, and lose their price text as well.
 *
 * Every hook is a relative jump or call into the injected section, whose
 * helpers address retail data with call/pop. No hook overwrites bytes that
 * a base relocation covers, so the DLLs still rebase correctly.
 */
#define UNPURCHASABLE 10000u

_Static_assert(UNPURCHASABLE > 9999u, "money reaches up to 9999");

/* Both modules keep a vmCvar_t's integer at +0xc, its stride 0x110. */
#define CVAR_SIZE 0x110u

/* The cgame helpers take about 6 KiB, the game ones much less. */
#define RESERVE 16384u

/* Retail cgame buttons: 0x54c bytes, their text at +0x8c, their state at
 * +0x510; disabled is 1, pressed or owned 3.
 */
#define BUTTON_SIZE 0x54cu
#define BUTTON_TEXT 0x8cu
#define BUTTON_STATE 0x510u
#define DISABLED_STATE 1u
#define OWNED_STATE 3u
#define UNPURCHASABLE_STATE 4u
/* The same for the RTS build and structure upgrade buttons, whose icons
 * have a frame order of their own.
 */
#define UNPURCHASABLE_ICON_STATE 5u
/* Retail cgame labels, the icon and price beside each buy button: 0x4e4
 * bytes, their text at the same +0x8c.
 */
#define LABEL_SIZE 0x4e4u
/* A widget's image, a shader handle the image draw skips while 0. */
#define WIDGET_SHADER 0x34u
/* How much darker than their frame unpurchasable buttons are drawn, in
 * percent: 0 leaves the frame as it is, 100 makes the button completely
 * black.
 */
#define UNPURCHASABLE_DIMMING 100u
_Static_assert(UNPURCHASABLE_DIMMING <= 100u, "a percentage");
/* Completely black weapon and upgrade buttons also hide the icon beside
 * them.
 */
#define HIDE_ICONS (UNPURCHASABLE_DIMMING == 100u)

/* UNPURCHASABLE_STATE is drawn with the hovered frame of the buy button
 * texture, light grey with a mean brightness of BUTTON_GREY, and
 * UNPURCHASABLE_ICON_STATE with the disabled frame of the RTS icons
 * (textures/ui/hud/buyicons/), whose frames are normal, disabled grey,
 * hovered and pressed, all in color but the disabled one. Those grey
 * frames are about ICON_GREY bright, so the icons' tint is raised by
 * BUTTON_GREY / ICON_GREY for both to come out alike. The frame is 6
 * minus the state, the tint the state's entry of the tints from
 * UNPURCHASABLE_STATE on.
 */
#define BUTTON_GREY 105.0f
#define ICON_GREY 44.0f
_Static_assert(6u - UNPURCHASABLE_STATE == 2u && 6u - UNPURCHASABLE_ICON_STATE == 1u,
               "the hovered and the disabled frame");

/* Hook replacements receive their rel32 once the payload has a known RVA. */
static code_rule_t patch_unpurchasable_game_hooks[] = {
    /* G_CannotBuyUpgrades and G_CannotBuyWeapons (ent in ecx, the mask on
     * the stack, true refuses): sub esp,frame; mov ecx,[ecx+client].
     */
    { GAME, true, "unpurchasable: refuse upgrades", 0x3bd60, 9,
      { 0x83, 0xec, 0x38, 0x8b, 0x89, B4(0x28cu) },
      { 0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 } },
    { GAME, true, "unpurchasable: refuse weapons", 0x3be60, 9,
      { 0x83, 0xec, 0x34, 0x8b, 0x89, B4(0x28cu) },
      { 0xe9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90 } },
};

static code_rule_t patch_unpurchasable_cgame_hooks[] = {
    /* The in-game "Weapons & Upgrades" menu that "changeweapons" (M)
     * toggles; its refresh takes the menu on the stack:
     * push ecx; mov edx,[esp+8].
     */
    { CGAME, true, "unpurchasable: grey out in the in-game shop", 0x56a80, 5,
      { 0x51, 0x8b, 0x54, 0x24, 0x08 }, { 0xe9, 0, 0, 0, 0 } },
    /* The class selection's upgrade page: its refresh's epilogue,
     * mov ecx,[esp+0x24]; pop edi, with the menu in ebp.
     */
    { CGAME, true, "unpurchasable: grey out on the spawn upgrade page", 0x531fa, 5,
      { 0x8b, 0x4c, 0x24, 0x24, 0x5f }, { 0xe9, 0, 0, 0, 0 } },
    /* The class selection's weapon page: mov ecx,[esp+0x14]; pop ebx in
     * its refresh's epilogue, with the menu in ebx.
     */
    { CGAME, true, "unpurchasable: grey out on the spawn weapon page", 0x51218, 5,
      { 0x8b, 0x4c, 0x24, 0x14, 0x5b }, { 0xe9, 0, 0, 0, 0 } },
    /* The RTS build menu's frame, after its build buttons' loop, with the
     * menu in ebx: call 0x47820, which only reads globals.
     */
    { CGAME, true, "unpurchasable: grey out RTS structures", 0x742be, 5,
      { 0xe8, B4(0x47820u - 0x742c3u) }, { 0xe8, 0, 0, 0, 0 } },
    /* Both calls of the RTS structure panel (0x71490), the menu in edi. */
    { CGAME, true, "unpurchasable: grey out RTS structure upgrades", 0x733cc, 5,
      { 0xe8, B4(0x71490u - 0x733d1u) }, { 0xe8, 0, 0, 0, 0 } },
    { CGAME, true, "unpurchasable: grey out RTS structure upgrades", 0x74262, 5,
      { 0xe8, B4(0x71490u - 0x74267u) }, { 0xe8, 0, 0, 0, 0 } },
    /* The draw function of every button, the button on the stack:
     * push ecx; mov eax,[esp+8].
     */
    { CGAME, true, "unpurchasable: draw unpurchasable buttons grey", 0x59520, 5,
      { 0x51, 0x8b, 0x44, 0x24, 0x08 }, { 0xe9, 0, 0, 0, 0 } },
    /* The image draw every widget ends in (0x593b0), the widget in esi:
     * its call of trap_R_SetColor (0x33140, the color in eax) with the
     * white the image is drawn in.
     */
    { CGAME, true, "unpurchasable: draw unpurchasable buttons dark", 0x59447, 5,
      { 0xe8, B4(0x33140u - 0x5944cu) }, { 0xe8, 0, 0, 0, 0 } },
};

/* Replaces the prologue of G_CannotBuy*: true when the mask asks for an
 * item the client does not own yet whose price is UNPURCHASABLE or more,
 * else the retail check. owned is the gclient_t offset of the owned items'
 * mask, costs the RVA of the first price's integer, frame the prologue's
 * stack frame and back the RVA after the replaced instructions.
 */
static uint32_t refuse(payload_t *payload, uint32_t rva, uint32_t owned, uint32_t costs,
                       unsigned count, unsigned char frame, uint32_t back)
{
    uint32_t at = (uint32_t)payload->size;
    EMIT(0x8b, 0x81, B4(0x28cu));           /* mov eax,[ecx+client] */
    EMIT(0x8b, 0x80, B4(owned));            /* mov eax,[eax+owned] */
    EMIT(0xf7, 0xd0,                        /* not eax */
         0x23, 0x44, 0x24, 0x04);           /* and eax,[esp+4]: to buy */
    EMIT(0x56);                             /* push esi */
    uint32_t base = emit_anchor(payload, rva, ESI);
    EMIT(0x8d, 0xb6, B4(costs - base));     /* lea esi,[esi+costs] */
    EMIT(0xba, B4(count));                  /* mov edx,count */
    uint32_t loop = (uint32_t)payload->size;
    EMIT(0xd1, 0xe8);                       /* shr eax,1: the next bit */
    uint32_t kept = emit_skip_if(payload, 0x73); /* jnc next */
    EMIT(0x81, 0x3e, B4(UNPURCHASABLE));    /* cmp dword [esi],limit */
    uint32_t refused = emit_skip_if(payload, 0x7d); /* jge refused */
    emit_land(payload, kept);
    EMIT(0x81, 0xc6, B4(CVAR_SIZE),         /* next: add esi,stride */
         0x4a);                             /* dec edx */
    EMIT(0x75, (unsigned char)(loop - (payload->size + 2u))); /* jnz loop */
    EMIT(0x5e);                             /* pop esi */
    EMIT(0x83, 0xec, frame,                 /* the replaced prologue */
         0x8b, 0x89, B4(0x28cu));
    emit_jump(payload, rva, back);
    emit_land(payload, refused);
    EMIT(0x5e,                              /* pop esi */
         0xb8, B4(1u),                      /* mov eax,1 */
         0xc3);                             /* ret */
    return at;
}

static bool build_game(payload_t *payload, uint32_t rva, uint32_t next, uint32_t *entry)
{
    if (!payload_reserve(payload, RESERVE, "Unpurchasable"))
        return false;
    *entry = emit_entry(payload, next);

    /* gclient_t's pers.upgrades and pers.weapons; sk_upgrade[12] and
     * sk_weapcost[9], as G_CannotBuy* walk them.
     */
    const uint32_t targets[] = {
        refuse(payload, rva, 0x4d8u, 0x148c6ecu, 12, 0x38, 0x3bd69u),
        refuse(payload, rva, 0x4dcu, 0x1482aecu, 9, 0x34, 0x3be69u),
    };
    _Static_assert(ARRAY_SIZE(targets) == ARRAY_SIZE(patch_unpurchasable_game_hooks),
                   "every GAME hook needs a helper");
    resolve_hooks(patch_unpurchasable_game_hooks, ARRAY_SIZE(targets), targets, rva);
    return true;
}

/* cgamex86 RVAs of the price arrays the menus read: sk_weapcost_* (lmg,
 * mg, shotgun, sniper, 40rte, flamer, hmg, landmine, satchel),
 * sk_upgrade_* (health 1-4, firepower 1-4, brute, spawning, adrenaline,
 * medkit) and sk_cost_* (mgturret, atturret, fuelbomb, poison, tripmine,
 * supportstation, stronghold); the "$%i" the menus format prices with,
 * Com_sprintf (size in ecx, destination in edx, the rest on the stack) and
 * the function that centers a widget's text (the widget in esi).
 */
enum {
    WEAPON_COSTS = 0xaad1ccu, UPGRADE_COSTS = 0x9664ccu, STRUCTURE_COSTS = 0x96290cu,
    PRICE_FORMAT = 0xb2118u, COM_SPRINTF = 0x3d8e0u, CENTER_TEXT = 0x5c2b0u,
};

/* Emits a subroutine that formats the price in eax as the text of the
 * widget in edi and centers it, as the retail menu setup does once;
 * every register is preserved. Returns its payload offset.
 */
static uint32_t emit_show_price(payload_t *payload, uint32_t rva)
{
    uint32_t at = (uint32_t)payload->size;
    EMIT(0x60,                              /* pushad */
         0x50);                             /* push eax: price */
    uint32_t base = emit_anchor(payload, rva, EBX);
    EMIT(0x8d, 0x83, B4(PRICE_FORMAT - base), /* lea eax,[ebx+format] */
         0x50);                             /* push eax */
    EMIT(0x8d, 0x97, B4(BUTTON_TEXT));      /* lea edx,[edi+text] */
    EMIT(0xb9, B4(0x400u));                 /* mov ecx,sizeof(text) */
    emit_call(payload, rva, COM_SPRINTF);
    EMIT(0x83, 0xc4, 0x08);                 /* add esp,8 */
    EMIT(0x89, 0xfe);                       /* mov esi,edi */
    emit_call(payload, rva, CENTER_TEXT);
    EMIT(0x61, 0xc3);                       /* popad; ret */
    return at;
}

/* Hides or shows a widget's price text, whose first byte is at offset
 * text from reg: emptied when blocked, which reaches here as zero flag
 * clear, else formatted again by show_price from the price at cost (an
 * anchor-relative offset from esi) if it is empty. Clobbers eax and edi.
 */
static void price_text(payload_t *payload, unsigned reg, uint32_t text, uint32_t cost,
                       uint32_t show_price)
{
    unsigned char mod = (unsigned char)(0x80 | reg);
    uint32_t buyable = emit_skip_if(payload, 0x74); /* jz */
    EMIT(0xc6, mod, B4(text), 0);           /* mov byte [reg+text],0 */
    uint32_t done = emit_skip_if(payload, 0xeb); /* jmp */
    emit_land(payload, buyable);
    EMIT(0x80, (unsigned char)(0xb8 | reg), B4(text), 0); /* cmp byte [reg+text],0 */
    uint32_t shown = emit_skip_if(payload, 0x75); /* jne */
    EMIT(0x8d, (unsigned char)(0xb8 | reg), B4(text - BUTTON_TEXT)); /* lea edi,[reg+widget] */
    EMIT(0x8b, 0x86, B4(cost));             /* mov eax,[esi+cost] */
    uint32_t at = (uint32_t)payload->size;
    EMIT(0xe8, B4(show_price - (at + 5u))); /* call show_price */
    emit_land(payload, shown);
    emit_land(payload, done);
}

/* Switches the state of the button at offset state from reg by whether
 * eax is nonzero (blocked): to blocked when it is, and when it is not from
 * blocked back to disabled, from which the retail refresh picks the state
 * again. Leaves eax as it was.
 */
static void set_state(payload_t *payload, unsigned reg, uint32_t state, uint32_t blocked)
{
    unsigned char mov = (unsigned char)(0x80 | reg), cmp = (unsigned char)(0xb8 | reg);
    EMIT(0x85, 0xc0);                       /* test eax,eax */
    uint32_t buyable = emit_skip_if(payload, 0x74); /* jz */
    EMIT(0xc7, mov, B4(state), B4(blocked)); /* mov dword [reg+state],blocked */
    uint32_t done = emit_skip_if(payload, 0xeb); /* jmp */
    emit_land(payload, buyable);
    EMIT(0x83, cmp, B4(state), (unsigned char)blocked); /* cmp dword [reg+state],blocked */
    uint32_t kept = emit_skip_if(payload, 0x75); /* jne */
    EMIT(0xc7, mov, B4(state), B4(DISABLED_STATE)); /* mov dword [reg+state],1 */
    emit_land(payload, kept);
    emit_land(payload, done);
}

/* Hides or shows the icon of the label at offset label from edx, whose
 * shader handle is kept at the RVA slot while hidden: cleared when eax is
 * nonzero (blocked), else put back if it is cleared. The menus set their
 * icons once, when they are built. The anchor at base is in esi; clobbers
 * edi.
 */
static void label_icon(payload_t *payload, uint32_t base, uint32_t label, uint32_t slot)
{
    uint32_t icon = label + WIDGET_SHADER;
    EMIT(0x85, 0xc0);                       /* test eax,eax */
    uint32_t buyable = emit_skip_if(payload, 0x74); /* jz */
    EMIT(0x8b, 0xba, B4(icon));             /* mov edi,[edx+icon] */
    EMIT(0x85, 0xff);                       /* test edi,edi */
    uint32_t hidden = emit_skip_if(payload, 0x74); /* jz: already hidden */
    EMIT(0x89, 0xbe, B4(slot - base));      /* mov [esi+slot],edi */
    EMIT(0xc7, 0x82, B4(icon), B4(0u));     /* mov dword [edx+icon],0 */
    uint32_t done = emit_skip_if(payload, 0xeb); /* jmp */
    emit_land(payload, buyable);
    EMIT(0x83, 0xba, B4(icon), 0);          /* cmp dword [edx+icon],0 */
    uint32_t shown = emit_skip_if(payload, 0x75); /* jne: still shown */
    EMIT(0x8b, 0xbe, B4(slot - base));      /* mov edi,[esi+slot] */
    EMIT(0x89, 0xba, B4(icon));             /* mov [edx+icon],edi */
    emit_land(payload, shown);
    emit_land(payload, hidden);
    emit_land(payload, done);
}

/* One menu page's weapons or upgrades. Prices are vmCvar_t integers from
 * the RVA costs on, and the buttons' states and the price labels menu
 * offsets from state and label on, BUTTON_SIZE and LABEL_SIZE apart; the
 * labels' hidden icons are kept from the RVA icons on, a dword each. With
 * chains, health 1-4 (items 0-3) and firepower 1-4 (4-7) each block every
 * tier after an unpurchasable one that is not owned, since those need it
 * first.
 */
typedef struct {
    uint32_t costs;
    unsigned count;
    uint32_t state, label;
    bool chains;
    uint32_t icons;
} items_t;

/* Runs after the retail refresh, which leaves owned items pressed and
 * every other one disabled: an item that is not owned and is priced
 * UNPURCHASABLE or more, or blocked by its chain, becomes
 * UNPURCHASABLE_STATE and loses its price text. One that no longer is goes
 * back to disabled, from which the next retail refresh picks its state
 * again, and gets its price back. With HIDE_ICONS the label's icon goes
 * and comes back with the price. The menu is in edx and the anchor at
 * base in esi; eax, ecx and edi are scratch.
 */
static void grey_items(payload_t *payload, uint32_t base, const items_t *items,
                       uint32_t show_price)
{
    for (unsigned i = 0; i < items->count; ++i) {
        bool chained = items->chains && i < 8;
        uint32_t state = items->state + i * BUTTON_SIZE;
        uint32_t cost = items->costs + i * CVAR_SIZE - base;
        EMIT(0x31, 0xc0);                   /* xor eax,eax */
        EMIT(0x81, 0xbe, B4(cost), B4(UNPURCHASABLE)); /* cmp dword [esi+cost],limit */
        EMIT(0x0f, 0x9d, 0xc0);             /* setge al */
        if (chained && i % 4)
            EMIT(0x09, 0xc8);               /* or eax,ecx: tier below */
        EMIT(0x83, 0xba, B4(state), OWNED_STATE); /* cmp dword [edx+state],owned */
        uint32_t not_owned = emit_skip_if(payload, 0x75); /* jne */
        EMIT(0x31, 0xc0);                   /* xor eax,eax: owned stays */
        emit_land(payload, not_owned);
        if (chained)
            EMIT(0x89, 0xc1);               /* mov ecx,eax */
        set_state(payload, EDX, state, UNPURCHASABLE_STATE);
        uint32_t label = items->label + i * LABEL_SIZE;
        if (HIDE_ICONS)
            label_icon(payload, base, label, items->icons + i * 4u);
        EMIT(0x85, 0xc0);                   /* test eax,eax */
        price_text(payload, EDX, label + BUTTON_TEXT, cost, show_price);
    }
}

/* pushad, esi = anchor, edx = menu from reg; returns the anchor. */
static uint32_t begin_menu(payload_t *payload, uint32_t rva, unsigned reg)
{
    EMIT(0x60);                             /* pushad */
    uint32_t base = emit_anchor(payload, rva, ESI);
    EMIT(0x89, (unsigned char)(0xc0 | reg << 3 | EDX)); /* mov edx,reg */
    return base;
}

/* The RTS build buttons, whose text is their price. Each frame retail
 * disables a button while its structure costs more than the money and,
 * once it does not, turns only a disabled one normal or hovered. While
 * the structure is priced UNPURCHASABLE or more the button becomes
 * UNPURCHASABLE_ICON_STATE and its text empty; once it is not, it goes back
 * to disabled for retail to resolve and shows its price again. The menu
 * is in ebx and the anchor at base in esi.
 */
static void grey_structures(payload_t *payload, uint32_t base, uint32_t show_price)
{
    for (unsigned i = 0; i < 7; ++i) {
        uint32_t button = 0x1e8u + i * BUTTON_SIZE;
        uint32_t cost = STRUCTURE_COSTS + i * CVAR_SIZE - base;
        EMIT(0x31, 0xc0);                   /* xor eax,eax */
        EMIT(0x81, 0xbe, B4(cost), B4(UNPURCHASABLE)); /* cmp dword [esi+cost],limit */
        EMIT(0x0f, 0x9d, 0xc0);             /* setge al */
        set_state(payload, EBX, button + BUTTON_STATE, UNPURCHASABLE_ICON_STATE);
        EMIT(0x85, 0xc0);                   /* test eax,eax */
        price_text(payload, EBX, button + BUTTON_TEXT, cost, show_price);
    }
}

static bool build_cgame(payload_t *payload, uint32_t rva, uint32_t next, uint32_t *entry)
{
    if (!payload_reserve(payload, RESERVE, "Unpurchasable"))
        return false;
    *entry = emit_entry(payload, next);

    /* Data after the entry's jump: the runtime address of the tint for
     * the unpurchasable button being drawn, else 0, and the tints, one
     * vec4_t per state from UNPURCHASABLE_STATE on.
     */
    uint32_t dimming = EMIT(B4(0u));
    float tint = 1.0f - (float)UNPURCHASABLE_DIMMING / 100.0f;
    float icon_tint = tint * BUTTON_GREY / ICON_GREY;
    if (icon_tint > 1.0f)
        icon_tint = 1.0f;
    const float tints_rgba[] = { tint, tint, tint, 1.0f, icon_tint, icon_tint, icon_tint, 1.0f };
    uint32_t tints = (uint32_t)payload->size;
    for (size_t i = 0; i < ARRAY_SIZE(tints_rgba); ++i) {
        uint32_t bits;
        memcpy(&bits, &tints_rgba[i], sizeof(bits)); /* the IEEE single bits */
        EMIT(B4(bits));
    }
    /* Then the hidden icons of the shop's weapons and upgrades and of
     * the spawn upgrade and weapon pages, one zeroed dword per item.
     */
    uint32_t icons[4];
    const unsigned icon_counts[4] = { 9, 12, 12, 9 };
    for (size_t page = 0; page < 4; ++page) {
        icons[page] = rva + (uint32_t)payload->size;
        for (unsigned i = 0; i < icon_counts[page]; ++i)
            EMIT(B4(0u));
    }
    uint32_t show_price = emit_show_price(payload, rva);

    /* In-game shop: call the retail refresh with the same argument
     * through a copy of its first instructions, then grey out.
     */
    uint32_t shop = (uint32_t)payload->size;
    EMIT(0xff, 0x74, 0x24, 0x04);           /* push [esp+4] */
    uint32_t refresh = EMIT(0xe8, 0, 0, 0, 0); /* call retail */
    EMIT(0x83, 0xc4, 0x04);                 /* add esp,4 */
    EMIT(0x60);                             /* pushad */
    uint32_t base = emit_anchor(payload, rva, ESI);
    EMIT(0x8b, 0x54, 0x24, 0x24);           /* mov edx,[esp+0x24]: menu */
    const items_t shop_weapons = { WEAPON_COSTS, 9, 0x570u + BUTTON_STATE, 0x351cu, false, icons[0] };
    const items_t shop_upgrades = { UPGRADE_COSTS, 12, 0x6120u + BUTTON_STATE, 0xa0b0u, true, icons[1] };
    grey_items(payload, base, &shop_weapons, show_price);
    grey_items(payload, base, &shop_upgrades, show_price);
    EMIT(0x61, 0xc3);                       /* popad; ret */
    put32(payload->data + refresh + 1, (uint32_t)payload->size - (refresh + 5u));
    EMIT(0x51, 0x8b, 0x54, 0x24, 0x08);     /* retail: push ecx; mov edx,[esp+8] */
    emit_jump(payload, rva, 0x56a85u);

    /* Spawn upgrade page, at its refresh's epilogue. */
    uint32_t upgrade_page = (uint32_t)payload->size;
    base = begin_menu(payload, rva, EBP);
    const items_t page_upgrades = { UPGRADE_COSTS, 12, 0x15bcu + BUTTON_STATE, 0x554cu, true, icons[2] };
    grey_items(payload, base, &page_upgrades, show_price);
    EMIT(0x61);                             /* popad */
    EMIT(0x8b, 0x4c, 0x24, 0x24, 0x5f);     /* retail: mov ecx,[esp+0x24]; pop edi */
    emit_jump(payload, rva, 0x531ffu);

    /* Spawn weapon page. Its 12 slots read the 9 weapon prices and the 3
     * cvars after them, while the server only sells bits 0-8 at those 9
     * prices; the last 3 slots stay as they are.
     */
    uint32_t weapon_page = (uint32_t)payload->size;
    base = begin_menu(payload, rva, EBX);
    const items_t page_weapons = { WEAPON_COSTS, 9, 0x1f84u + BUTTON_STATE, 0x5f14u, false, icons[3] };
    grey_items(payload, base, &page_weapons, show_price);
    EMIT(0x61);                             /* popad */
    EMIT(0x8b, 0x4c, 0x24, 0x14, 0x5b);     /* retail: mov ecx,[esp+0x14]; pop ebx */
    emit_jump(payload, rva, 0x5121du);

    /* RTS build buttons build1-7. */
    uint32_t structures = (uint32_t)payload->size;
    EMIT(0x60);                             /* pushad */
    base = emit_anchor(payload, rva, ESI);
    grey_structures(payload, base, show_price);
    EMIT(0x61);                             /* popad */
    emit_jump(payload, rva, 0x47820u);           /* the replaced call's target */

    /* RTS structure panel: it sets its upgrade button's state and price
     * text again every frame, so make the button UNPURCHASABLE_ICON_STATE
     * and empty the text after it while the selected structure's next
     * level is priced UNPURCHASABLE or more. Like the panel, read type (bits 4-7)
     * and level (bits 0-3) from +0xf0 of the selected cg_entities[] entry
     * (0x420 bytes each); 0x71460 returns the price for type in eax and
     * the next level in ecx, or -1 when there is none.
     */
    uint32_t panel = (uint32_t)payload->size;
    emit_call(payload, rva, 0x71490u);
    EMIT(0x60);                             /* pushad */
    base = emit_anchor(payload, rva, ESI);
    EMIT(0x8b, 0x86, B4(0xda02f4u - base)); /* mov eax,[esi+selected] */
    EMIT(0x85, 0xc0);                       /* test eax,eax */
    uint32_t none = emit_skip_if(payload, 0x74); /* jz done */
    EMIT(0x69, 0xc0, B4(0x420u));           /* imul eax,eax,sizeof(centity_t) */
    EMIT(0x8b, 0x84, 0x06,                  /* mov eax,[esi+eax+state] */
         B4(0xda14c0u + 0xf0u - base));
    EMIT(0x89, 0xc1,                        /* mov ecx,eax */
         0x83, 0xe1, 0x0f,                  /* and ecx,0xf: level */
         0x41,                              /* inc ecx */
         0xc1, 0xe8, 0x04,                  /* shr eax,4 */
         0x83, 0xe0, 0x0f);                 /* and eax,0xf: type */
    emit_call(payload, rva, 0x71460u);
    EMIT(0x3d, B4(UNPURCHASABLE));          /* cmp eax,limit */
    uint32_t affordable = emit_skip_if(payload, 0x7c); /* jl done */
    EMIT(0xc6, 0x87, B4(0x3194u + BUTTON_TEXT), 0); /* mov byte [edi+text],0 */
    EMIT(0xc7, 0x87, B4(0x3194u + BUTTON_STATE), B4(UNPURCHASABLE_ICON_STATE));
    emit_land(payload, none);
    emit_land(payload, affordable);
    EMIT(0x61, 0xc3);                       /* popad; ret */

    /* Button draw: a button in one of the unpurchasable states is drawn
     * in the frame 6 - state with dimming pointing at the state's tint,
     * and gets its state back afterwards; any other draws as retail
     * does. The retail draw ends in a tail call that reuses its argument,
     * so the button is kept in a slot of its own.
     */
    uint32_t draw = (uint32_t)payload->size;
    EMIT(0x8b, 0x44, 0x24, 0x04);           /* mov eax,[esp+4] */
    EMIT(0x8b, 0x88, B4(BUTTON_STATE));     /* mov ecx,[eax+state] */
    EMIT(0x83, 0xf9, UNPURCHASABLE_STATE);  /* cmp ecx,4 */
    uint32_t greyed = emit_skip_if(payload, 0x74); /* je */
    EMIT(0x83, 0xf9, UNPURCHASABLE_ICON_STATE); /* cmp ecx,5 */
    uint32_t plain = emit_skip_if(payload, 0x75); /* jne retail */
    emit_land(payload, greyed);
    EMIT(0x51);                             /* push ecx: the state */
    EMIT(0xba, B4(6u),                      /* mov edx,6 */
         0x29, 0xca);                       /* sub edx,ecx */
    EMIT(0x89, 0x90, B4(BUTTON_STATE));     /* mov [eax+state],edx: frame */
    EMIT(0xc1, 0xe1, 0x04);                 /* shl ecx,4: sizeof(vec4_t) */
    base = emit_anchor(payload, rva, EDX);
    EMIT(0x8d, 0x8c, 0x0a,                  /* lea ecx,[edx+ecx+tints-4*16] */
         B4(tints + rva - UNPURCHASABLE_STATE * 16u - base));
    EMIT(0x89, 0x8a, B4(dimming + rva - base)); /* mov [edx+dimming],ecx */
    EMIT(0x50, 0x50);                       /* push eax; push eax */
    uint32_t drawn = EMIT(0xe8, 0, 0, 0, 0); /* call retail */
    EMIT(0x83, 0xc4, 0x04);                 /* add esp,4 */
    base = emit_anchor(payload, rva, ECX);
    EMIT(0xc7, 0x81, B4(dimming + rva - base), B4(0u)); /* mov dword [ecx+dimming],0 */
    EMIT(0x58,                              /* pop eax */
         0x59);                             /* pop ecx */
    EMIT(0x89, 0x88, B4(BUTTON_STATE));     /* mov [eax+state],ecx */
    EMIT(0xc3);                             /* ret */
    emit_land(payload, plain);
    put32(payload->data + drawn + 1, (uint32_t)payload->size - (drawn + 5u));
    EMIT(0x51, 0x8b, 0x44, 0x24, 0x08);     /* retail: push ecx; mov eax,[esp+8] */
    emit_jump(payload, rva, 0x59525u);

    /* Image color: the tint dimming points at instead of white, else
     * unchanged. The color is only read by trap_R_SetColor, so pointing
     * eax at the tint is enough; ecx is free like in any call.
     */
    uint32_t color = (uint32_t)payload->size;
    base = emit_anchor(payload, rva, ECX);
    EMIT(0x8b, 0x89, B4(dimming + rva - base)); /* mov ecx,[ecx+dimming] */
    EMIT(0x85, 0xc9);                       /* test ecx,ecx */
    uint32_t white = emit_skip_if(payload, 0x74); /* je */
    EMIT(0x89, 0xc8);                       /* mov eax,ecx */
    emit_land(payload, white);
    emit_jump(payload, rva, 0x33140u);

    const uint32_t targets[] = {
        shop, upgrade_page, weapon_page, structures, panel, panel, draw, color,
    };
    _Static_assert(ARRAY_SIZE(targets) == ARRAY_SIZE(patch_unpurchasable_cgame_hooks),
                   "every CGAME hook needs a helper");
    resolve_hooks(patch_unpurchasable_cgame_hooks, ARRAY_SIZE(targets), targets, rva);
    return true;
}

static const payload_feature_t unpurchasable_game = {
    GAME, "unpurchasable items", build_game,
};
static const payload_feature_t unpurchasable_cgame = {
    CGAME, "unpurchasable items", build_cgame,
};

/* The payloads and their hooks must be enabled together. */
REGISTER_CODE_RULES(patch_unpurchasable_game_hooks)
REGISTER_CODE_RULES(patch_unpurchasable_cgame_hooks)
REGISTER_PAYLOAD_FEATURE(unpurchasable_game)
REGISTER_PAYLOAD_FEATURE(unpurchasable_cgame)
