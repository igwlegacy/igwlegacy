/* More airships in RESISTANCE, see patches/resistance_airships.c.
 *
 * Retail flies three airships along the sky portal's route, one after the
 * other from where it starts, each moved and drawn by CGAME_AIRSHIP_UPDATE
 * with an outward offset from the route of 0, 140 and 280. In RESISTANCE
 * another one joins every AIRSHIP_INTERVAL_MS, up to AIRSHIPS_MAX in all,
 * at a random place on the route, with a random direction, speed and
 * outward offset, and grows from AIRSHIP_MIN_SCALE to the retail size over
 * AIRSHIP_GROWTH_MS. The same retail update moves the new ones, so they
 * follow the route exactly as the retail three do.
 *
 * The extra airships keep flying as long as the retail ones do, that is
 * until the cgame restarts the sky (CGAME_AIRSHIPS_FLYING goes back to 0)
 * or the DLL is loaded again for the next map.
 */
#include "cgamex86.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define RESISTANCE_LEVEL 6

#define AIRSHIPS_MAX 20
#define AIRSHIP_INTERVAL_MS (4 * 60 * 1000)
#define AIRSHIP_GROWTH_MS (60 * 1000)
#define AIRSHIP_MIN_SCALE 0.01f
/* The new airships' outward offsets from the route, which keep them
 * apart from each other: retail's three span 0 to 280.
 */
#define AIRSHIP_OFFSET_MIN 0.0f
#define AIRSHIP_OFFSET_MAX 420.0f
/* Retail's speed range, see cgamex86 RVA 0x478e0. */
#define AIRSHIP_SPEED_MIN 0.4f
#define AIRSHIP_SPEED_RANGE 0.7f

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
    *field(airship, CGAME_AIRSHIP_DIRECTION) = random_next() & 1u ? 1.0f : -1.0f;
    *field(airship, CGAME_AIRSHIP_SPEED) = AIRSHIP_SPEED_MIN + AIRSHIP_SPEED_RANGE * random_unit();
    extra_offset[i] = AIRSHIP_OFFSET_MIN + (AIRSHIP_OFFSET_MAX - AIRSHIP_OFFSET_MIN) * random_unit();
    extra_joined[i] = (i + 1) * AIRSHIP_INTERVAL_MS;
}

/* The size of an airship age milliseconds after it joined, easing in and
 * out of its growth.
 */
static float growth(int age)
{
    if (age >= AIRSHIP_GROWTH_MS)
        return 1.0f;
    float x = age > 0 ? (float)age / (float)AIRSHIP_GROWTH_MS : 0.0f;
    return AIRSHIP_MIN_SCALE + (1.0f - AIRSHIP_MIN_SCALE) * x * x * (3.0f - 2.0f * x);
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

    if (CGAME(int, CGAME_LEVEL) != RESISTANCE_LEVEL)
        return;

    int frametime = CGAME(int, CGAME_FRAMETIME);
    if (frametime > 0)
        flight_time += frametime;
    while (extra_count < EXTRA_MAX && flight_time >= (extra_count + 1) * AIRSHIP_INTERVAL_MS)
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
