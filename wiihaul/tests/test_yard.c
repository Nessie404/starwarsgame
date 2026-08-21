/*
 * Host-side tests for WiiHaul's yard geometry, collision and scoring.
 *
 *   gcc -std=c99 -O2 -Wall -Werror -Isource \
 *       tests/test_yard.c source/truck.c source/yard.c -lm -o wiihaul-test
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "truck.h"
#include "yard.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int failures = 0;

#define CHECK(cond, ...)                                        \
    do {                                                        \
        if (!(cond)) {                                          \
            failures++;                                         \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);         \
            printf(__VA_ARGS__);                                \
            printf("\n");                                       \
        }                                                       \
    } while (0)

static void test_obb_overlap_basic(void)
{
    OBB a = { 0.0f, 0.0f, 0.0f, 2.0f, 1.0f };
    OBB b = { 3.0f, 0.0f, 0.0f, 2.0f, 1.0f };
    OBB c = { 5.0f, 0.0f, 0.0f, 2.0f, 1.0f };

    CHECK(obb_overlap(&a, &b), "adjacent-ish boxes 3m apart with 2m "
         "half-extents should overlap");
    CHECK(!obb_overlap(&a, &c), "boxes 5m apart with 2m half-extents "
         "should not overlap");
}

static void test_obb_overlap_rotated(void)
{
    OBB a = { 0.0f, 0.0f, 0.0f, 3.0f, 1.0f };            /* long, facing +X */
    OBB b = { 0.0f, 0.0f, (float)M_PI * 0.5f, 3.0f, 1.0f }; /* facing +Z */

    CHECK(obb_overlap(&a, &b), "two long boxes crossing at their "
         "centers should overlap regardless of orientation");
}

static void test_obb_circle(void)
{
    OBB a = { 0.0f, 0.0f, 0.0f, 2.0f, 1.0f };
    CHECK(obb_circle_overlap(&a, 2.3f, 0.0f, 0.5f),
         "a cone just past the box's forward edge, within its radius, "
         "should register a hit");
    CHECK(!obb_circle_overlap(&a, 5.0f, 0.0f, 0.5f),
         "a cone well clear of the box should not register a hit");
}

static void test_rig_obbs_count(void)
{
    RigSpec single = rig_specs[1];
    RigSpec doubles = rig_specs[5];
    OBB boxes[1 + MAX_TRAILERS];
    Rig r;

    rig_reset(&r, &single, 0.0f, 0.0f, 0.0f, 0.0f);
    CHECK(rig_obbs(&r, &single, boxes, 1 + MAX_TRAILERS) == 2,
         "a single-trailer rig should produce 2 boxes (tractor+trailer)");

    rig_reset(&r, &doubles, 0.0f, 0.0f, 0.0f, 0.0f);
    CHECK(rig_obbs(&r, &doubles, boxes, 1 + MAX_TRAILERS) == 3,
         "a doubles rig should produce 3 boxes");
}

static float flat_ground(float x, float z, void *ctx)
{
    (void)x; (void)z; (void)ctx;
    return 0.0f;
}

/* Driving the rig straight into a scenario's own start-adjacent obstacle
 * should fail the attempt; parking cleanly in the target should pass
 * it. This is the yard's whole scoring contract exercised end to end,
 * not just its individual geometry primitives. */
static void test_attempt_pass_and_fail(void)
{
    Yard y;
    RigSpec spec = rig_specs[1];
    HaulSettings set;
    Rig r;
    YardAttempt a;
    Input in;
    float dt = 1.0f / 60.0f;
    int i;

    yard_init(SCENARIO_STRAIGHT_BACK, &y);
    haul_settings_defaults(&set);
    rig_reset(&r, &spec, y.start_x, y.start_z, 0.0f, y.start_heading);
    yard_attempt_reset(&a);
    memset(&in, 0, sizeof(in));

    /* back straight down the lane toward the target, gently */
    in.reverse = 1;
    for (i = 0; i < 60; i++) {
        rig_step(&r, &spec, &in, &set, dt, flat_ground, NULL);
        yard_attempt_update(&a, &y, &r, &spec, dt);
    }
    CHECK(a.result == ATTEMPT_IN_PROGRESS, "should not resolve yet");

    /* keep backing until the TRAILER (what the target zone is actually
     * sized around — see yard_attempt_update) is at the target, then
     * stop and hold */
    for (i = 0; i < 60 * 20 && a.result == ATTEMPT_IN_PROGRESS; i++) {
        OBB boxes[1 + MAX_TRAILERS];
        int n;
        rig_step(&r, &spec, &in, &set, dt, flat_ground, NULL);
        yard_attempt_update(&a, &y, &r, &spec, dt);
        n = rig_obbs(&r, &spec, boxes, 1 + MAX_TRAILERS);
        if (boxes[n - 1].cx <= y.target.cx) break;
    }
    memset(&in, 0, sizeof(in));
    in.brake = 1;
    for (i = 0; i < 300 && a.result == ATTEMPT_IN_PROGRESS; i++) {
        rig_step(&r, &spec, &in, &set, dt, flat_ground, NULL);
        yard_attempt_update(&a, &y, &r, &spec, dt);
    }
    CHECK(a.result == ATTEMPT_PASS, "backing gently into the target and "
         "holding should pass the attempt (result=%d speed=%.2f x=%.2f)",
         a.result, r.speed, r.x);
    CHECK(!a.obstacle_hit, "a clean run should not have hit anything");
}

static void test_obstacle_hit_fails(void)
{
    Yard y;
    RigSpec spec = rig_specs[1];
    HaulSettings set;
    Rig r;
    YardAttempt a;
    Input in;
    float dt = 1.0f / 60.0f;
    int i;

    yard_init(SCENARIO_OFFSET_BACK_LEFT, &y);
    CHECK(y.n_obstacles > 0, "offset backing should have a corner "
         "obstacle to steer around");

    haul_settings_defaults(&set);
    /* start the rig on top of the scenario's own obstacle on purpose */
    rig_reset(&r, &spec, y.obstacles[0].box.cx, y.obstacles[0].box.cz,
             0.0f, 0.0f);
    yard_attempt_reset(&a);
    memset(&in, 0, sizeof(in));

    for (i = 0; i < 5 && a.result == ATTEMPT_IN_PROGRESS; i++) {
        rig_step(&r, &spec, &in, &set, dt, flat_ground, NULL);
        yard_attempt_update(&a, &y, &r, &spec, dt);
    }
    CHECK(a.result == ATTEMPT_FAIL, "starting parked inside an obstacle "
         "should be flagged a fail (result=%d)", a.result);
}

static void test_grade_ramp(void)
{
    Yard y;
    yard_init(SCENARIO_HILL_START, &y);
    CHECK(y.grade.kind == GRADE_RAMP, "hill start should have a grade");
    CHECK(yard_ground_at(&y, -5.0f, 0.0f) == y.grade.y0,
         "below the ramp should read the flat base height");
    CHECK(yard_ground_at(&y, 100.0f, 0.0f) == y.grade.y1,
         "past the ramp should read the flat top height");
    {
        float mid = yard_ground_at(&y, (y.grade.x0 + y.grade.x1) * 0.5f,
                                   0.0f);
        float expect = (y.grade.y0 + y.grade.y1) * 0.5f;
        CHECK(fabsf(mid - expect) < 0.01f,
             "the ramp midpoint should be the average height "
             "(got %.3f expected %.3f)", mid, expect);
    }
}

static void test_all_scenarios_build(void)
{
    int id;
    for (id = 0; id < SCENARIO_COUNT; id++) {
        Yard y;
        yard_init(id, &y);
        CHECK(y.name[0] != '\0', "scenario %d should have a name", id);
        CHECK(y.target.half_fwd > 0.0f && y.target.half_lat > 0.0f,
             "scenario %d (%s) should have a real target zone", id,
             y.name);
        CHECK(y.n_obstacles <= YARD_MAX_OBSTACLES &&
             y.n_cones <= YARD_MAX_CONES &&
             y.n_boundaries <= YARD_MAX_BOUNDARIES,
             "scenario %d should stay within its geometry limits", id);
    }
}

int main(void)
{
    truck_specs_reset_defaults();

    test_obb_overlap_basic();
    test_obb_overlap_rotated();
    test_obb_circle();
    test_rig_obbs_count();
    test_attempt_pass_and_fail();
    test_obstacle_hit_fails();
    test_grade_ramp();
    test_all_scenarios_build();

    if (failures) {
        printf("%d test(s) FAILED\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
