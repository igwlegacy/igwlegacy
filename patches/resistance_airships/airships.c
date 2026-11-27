/* More airships in RESISTANCE, see patches/resistance_airships.c.
 *
 * Retail flies three airships along the sky portal's route, one after the
 * other from where it starts, each moved and drawn by CGAME_AIRSHIP_UPDATE
 * with an outward offset from the route of 0, 140 and 280. In RESISTANCE
 * (or every mode, see AIRSHIPS_RESISTANCE_ONLY) more join over time, up to
 * AIRSHIPS_MAX in all, each at a random place on the route, with a random
 * direction, speed and outward offset, and grows to its full size. The
 * same retail update moves the new ones, so they follow the route exactly
 * as the retail three do.
 *
 * The extra airships keep flying as long as the retail ones do, that is
 * until the cgame restarts the sky (CGAME_AIRSHIPS_FLYING goes back to 0)
 * or the DLL is loaded again for the next map. All times count from when
 * the retail airships start flying, shortly after the map has loaded.
 */
#include "cgamex86.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* -- Tweakables -------------------------------------------------------- */

/* Add airships in RESISTANCE only (1) or in every mode (0). */
#define AIRSHIPS_RESISTANCE_ONLY 1

/* The most airships in the sky at once, retail's three included. */
#define AIRSHIPS_MAX 20

/* When the first extra airship joins, and the time between each one and
 * the next, in seconds.
 */
#define AIRSHIP_FIRST_SECONDS (4 * 60)
#define AIRSHIP_INTERVAL_SECONDS (4 * 60)

/* How long a new airship takes to grow to its full size, in seconds (0:
 * at once), and its sizes at the start and the end of that, relative to
 * retail's airships.
 */
#define AIRSHIP_GROWTH_SECONDS 60
#define AIRSHIP_START_SIZE 0.01f
#define AIRSHIP_FULL_SIZE 1.0f
/* Grow slowly at the start and the end (1) or at an even pace (0). */
#define AIRSHIP_GROWTH_EASED 1

/* The speed range of the new airships; retail's are 0.4 to 1.1 (cgamex86
 * RVA 0x478e0).
 */
#define AIRSHIP_SPEED_MIN 0.4f
#define AIRSHIP_SPEED_MAX 1.1f

/* How many of the new airships fly the route one way (direction 1), in
 * percent; the others fly it the other way. Retail picks either half the
 * time.
 */
#define AIRSHIP_FORWARD_PERCENT 50

/* The range of the new airships' outward offsets from the route, which
 * keeps them apart from each other; retail's three are at 0, 140 and
 * 280. Negative offsets move them inward.
 */
#define AIRSHIP_OFFSET_MIN 0.0f
#define AIRSHIP_OFFSET_MAX 420.0f

/* ---------------------------------------------------------------------- */

_Static_assert(AIRSHIP_FIRST_SECONDS >= 0 && AIRSHIP_INTERVAL_SECONDS > 0,
               "airships need a time to join at");
_Static_assert(AIRSHIP_GROWTH_SECONDS >= 0, "a growth time can't be negative");
_Static_assert(AIRSHIP_FORWARD_PERCENT >= 0 && AIRSHIP_FORWARD_PERCENT <= 100, "a percentage");

#define RESISTANCE_LEVEL 6

#define FIRST_MS (AIRSHIP_FIRST_SECONDS * 1000)
#define INTERVAL_MS (AIRSHIP_INTERVAL_SECONDS * 1000)
#define GROWTH_MS (AIRSHIP_GROWTH_SECONDS * 1000)

#define EXTRA_MAX (AIRSHIPS_MAX - CGAME_AIRSHIP_COUNT)
_Static_assert(EXTRA_MAX > 0, "AIRSHIPS_MAX leaves no room beyond the retail airships");

/* The extra airships' retail records, which the update reads and writes
 * like the retail ones, their outward offsets and the flight time they
 * joined at.
 */
static unsigned char extra[EXTRA_MAX][CGAME_AIRSHIP_SIZE] __attribute__((aligned(16)));
static float extra_offset[EXTRA_MAX];
static int extra_joined[EXTRA_MAX];
static int extra_count;

/* Milliseconds of flight in RESISTANCE, the clock the new airships join
 * and grow by.
 */
static int flight_time;

/* What the scale hook multiplies the next airship's axes with. */
static float scale = 1.0f;

static uint32_t random_state;

/* xorshift32, seeded from cg.time on first use: the airships need not be
 * the same on every client, only somewhat different on every map.
 */
static uint32_t random_next(void)
{
    if (!random_state)
        random_state = (uint32_t)CGAME(int, CGAME_TIME) * 2654435761u | 1u;
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

/* A float in [0, 1). */
static float random_unit(void)
{
    return (float)(random_next() >> 8) * (1.0f / 16777216.0f);
}

static float *field(void *airship, uint32_t offset)
{
    return (float *)((unsigned char *)airship + offset);
}

/* Call the retail update with the airship in ebx and the offset as its
 * stack argument. It clobbers eax, ecx and edx and leaves the x87 stack
 * empty, as MSVC's convention requires on entry too, so no x87 value of
 * ours may stay live across it.
 */
static void update(void *airship, float offset)
{
    union {
        float f;
        uint32_t u;
    } argument = { offset };
    uint32_t eax, ecx, edx;
    __asm__ volatile(
        "pushl %%ebx\n\t"
        "movl %[airship], %%ebx\n\t"
        "pushl %%eax\n\t"
        "call *%[function]\n\t"
        "addl $4, %%esp\n\t"
        "popl %%ebx"
        : "=a"(eax), "=c"(ecx), "=d"(edx)
        : "0"(argument.u), [airship] "S"(airship),
          [function] "D"(CGAME_ADDRESS(CGAME_AIRSHIP_UPDATE))
        : "memory", "cc", "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)",
          "st(6)", "st(7)");
}

/* The flight time in milliseconds extra airship i joins at. */
static int joins_at(int i)
{
    return FIRST_MS + i * INTERVAL_MS;
}

/* Place extra airship i at a random distance into a random segment of
 * the route, where the update continues from as from any other place.
 */
static void join(int i)
{
    void *airship = extra[i];
    memset(airship, 0, CGAME_AIRSHIP_SIZE);
    int segment = (int)(random_next() % CGAME_ROUTE_SEGMENTS);
    double length = CGAME(double, CGAME_ROUTE + (uint32_t)segment * CGAME_ROUTE_SEGMENT_SIZE +
                                      CGAME_ROUTE_LENGTH);
    *field(airship, CGAME_AIRSHIP_ROUTE) = (float)segment;
    *field(airship, CGAME_AIRSHIP_DISTANCE) = (float)(length * random_unit());
    *field(airship, CGAME_AIRSHIP_DIRECTION) =
        random_next() % 100u < AIRSHIP_FORWARD_PERCENT ? 1.0f : -1.0f;
    *field(airship, CGAME_AIRSHIP_SPEED) =
        AIRSHIP_SPEED_MIN + (AIRSHIP_SPEED_MAX - AIRSHIP_SPEED_MIN) * random_unit();
    extra_offset[i] = AIRSHIP_OFFSET_MIN + (AIRSHIP_OFFSET_MAX - AIRSHIP_OFFSET_MIN) * random_unit();
    extra_joined[i] = joins_at(i);
}

/* The size of an airship age milliseconds after it joined, relative to
 * retail's.
 */
static float growth(int age)
{
    if (age >= GROWTH_MS)
        return AIRSHIP_FULL_SIZE;
    float x = age > 0 ? (float)age / (float)GROWTH_MS : 0.0f;
    if (AIRSHIP_GROWTH_EASED)
        x = x * x * (3.0f - 2.0f * x);    /* smoothstep */
    return AIRSHIP_START_SIZE + (AIRSHIP_FULL_SIZE - AIRSHIP_START_SIZE) * x;
}

/* In place of the retail loop over the airships (cgamex86 RVA 0x17e30),
 * once per frame from CG_AddPacketEntities.
 */
BLOB_EXPORT void airships_frame(void)
{
    if (!CGAME(int, CGAME_AIRSHIPS_FLYING)) {
        extra_count = 0;
        flight_time = 0;
        return;
    }

    scale = 1.0f;
    for (int i = 0; i < CGAME_AIRSHIP_COUNT; ++i)
        update((unsigned char *)CGAME_ADDRESS(CGAME_AIRSHIPS) + i * CGAME_AIRSHIP_SIZE,
               (float)i * CGAME_AIRSHIP_SPACING);

    if (AIRSHIPS_RESISTANCE_ONLY && CGAME(int, CGAME_LEVEL) != RESISTANCE_LEVEL)
        return;

    int frametime = CGAME(int, CGAME_FRAMETIME);
    if (frametime > 0)
        flight_time += frametime;
    while (extra_count < EXTRA_MAX && flight_time >= joins_at(extra_count))
        join(extra_count++);

    for (int i = 0; i < extra_count; ++i) {
        scale = growth(flight_time - extra_joined[i]);
        update(extra[i], extra_offset[i]);
    }
    scale = 1.0f;
}

/* In place of the update's trap_R_AddRefEntityToScene call, with the
 * refEntity_t, whose axes the update already scaled to the sky portal:
 * scale them by the airship's growth too and return the refEntity_t for
 * the trap. The update sets nonNormalizedAxes itself.
 */
BLOB_EXPORT float *airships_scale(float *entity)
{
    if (scale != 1.0f)
        for (int i = 0; i < 9; ++i)
            entity[7 + i] *= scale;    /* axis[3][3] at byte offset 0x1c */
    return entity;
}
