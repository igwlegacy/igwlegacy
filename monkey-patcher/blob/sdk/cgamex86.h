/* What blobs know about the retail cgamex86.dll they run in. Addresses are
 * the retail virtual addresses at the preferred base 0x30000000; the DLL
 * may load elsewhere, so they are only used through CGAME_ADDRESS().
 */
#pragma once

#include "blob.h"

#include <stdint.h>

#pragma GCC visibility push(hidden)

#define CGAME_PREFERRED_BASE 0x30000000u

/* cg.frametime and cg.time, in milliseconds. */
#define CGAME_FRAMETIME 0x30d9df10u
#define CGAME_TIME 0x30d9df14u

/* The level CG_ParseServerinfo stores from sv_difficulty, which the
 * patched DLL sets to 6 in RESISTANCE (patches/game_mode_resistance.c).
 */
#define CGAME_LEVEL 0x30d749ecu

/* The airships that fly through the sky portal along the closed route of
 * the map's four ig_zeppelin_point entities. CGAME_AIRSHIPS_FLYING is
 * set once the route is built from them (cgamex86 RVA 0x47a20), which
 * also fills the three retail airships of CGAME_AIRSHIP_SIZE bytes each
 * at CGAME_AIRSHIPS (RVA 0x478e0).
 */
#define CGAME_AIRSHIPS_FLYING 0x30b07740u
#define CGAME_AIRSHIPS 0x30b07760u
#define CGAME_AIRSHIP_SIZE 0xb00u
#define CGAME_AIRSHIP_COUNT 3
/* An airship's fields: its speed (0.4 to 1.1), its direction along the
 * route (1 or -1), its route parameter (the segment in the integer part,
 * the progress through it in the fraction) and its distance travelled
 * into the current segment.
 */
#define CGAME_AIRSHIP_SPEED 0x1cu
#define CGAME_AIRSHIP_DIRECTION 0x68u
#define CGAME_AIRSHIP_ROUTE 0x7cu
#define CGAME_AIRSHIP_DISTANCE 0x80u
/* Retail moves airship i with an outward offset of i times this from the
 * route.
 */
#define CGAME_AIRSHIP_SPACING 140.0f

/* The route's CGAME_ROUTE_SEGMENTS segments of CGAME_ROUTE_SEGMENT_SIZE
 * bytes each; a segment's arc length is the double at
 * CGAME_ROUTE_LENGTH within it.
 */
#define CGAME_ROUTE 0x30af7c40u
#define CGAME_ROUTE_SEGMENTS 4
#define CGAME_ROUTE_SEGMENT_SIZE 0x3eb0u
#define CGAME_ROUTE_LENGTH 0x3ea8u

/* Moves the airship in ebx along the route and adds it to the scene, with
 * its outward offset from the route as the float argument, which the
 * caller pops. It keeps ebx, esi, edi and ebp.
 */
#define CGAME_AIRSHIP_UPDATE 0x30017a90u

#define CGAME_ADDRESS(va) ((void *)(blob_image_base + ((va) - CGAME_PREFERRED_BASE)))
#define CGAME(type, va) (*(type *)CGAME_ADDRESS(va))

#pragma GCC visibility pop
