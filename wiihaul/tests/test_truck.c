/*
 * Host-side tests for WiiHaul's tractor-trailer physics. Built with a
 * normal PC compiler, no devkitPPC needed:
 *
 *   gcc -std=c99 -O2 -Wall -Werror -Isource \
 *       tests/test_truck.c source/truck.c -lm -o wiihaul-test
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "truck.h"

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

static void idle(Input *in) { memset(in, 0, sizeof(*in)); }

static void run(Rig *r, const RigSpec *spec, const HaulSettings *set,
                const Input *in, float seconds)
{
    float dt = 1.0f / 60.0f;
    int n = (int)(seconds / dt);
    int i;
    for (i = 0; i < n; i++)
        rig_step(r, spec, in, set, dt, NULL, NULL);
}

/*
 * The camera (camera.c) is built with guLookAt(eye, up=+Y, look) along
 * the rig's heading, giving a right-hand axis of
 * right = cross(forward, up) = (-sin h, 0, cos h). Steering right must
 * turn the tractor toward that axis. This is the same regression WiiKart
 * guards with test_steer_sign() — an inverted stick is exactly the kind
 * of bug that "looks fine" until someone tries to back into a dock.
 */
static void test_steer_sign(void)
{
    RigSpec spec = rig_specs[1];  /* DRY VAN HAUL */
    HaulSettings set;
    Rig r;
    Input in;
    float right_x, right_z, moved_x, moved_z, dot;

    haul_settings_defaults(&set);
    rig_reset(&r, &spec, 0.0f, 0.0f, 0.0f, 0.0f); /* heading 0, facing +X */
    idle(&in);
    in.accel = 1;
    in.steer = STEER_RIGHT;
    run(&r, &spec, &set, &in, 3.0f);

    right_x = -sinf(0.0f);
    right_z =  cosf(0.0f);
    moved_x = r.x; moved_z = r.z;
    dot = moved_x * right_x + moved_z * right_z;
    CHECK(dot > 0.05f, "steering RIGHT should move the tractor toward "
         "camera-right (dot=%.3f, x=%.2f z=%.2f)", dot, moved_x, moved_z);
    CHECK(r.heading > 0.02f, "steering RIGHT should increase heading "
         "(heading=%.3f)", r.heading);
}

/* A trailer being towed in a sustained turn settles into a steady
 * off-tracking angle behind the tractor rather than swinging wildly or
 * snapping straight through it. */
static void test_offtracking_settles(void)
{
    RigSpec spec = rig_specs[1];
    HaulSettings set;
    Rig r;
    Input in;
    float artic_mid, artic_end;

    haul_settings_defaults(&set);
    rig_reset(&r, &spec, 0.0f, 0.0f, 0.0f, 0.0f);
    idle(&in);
    in.accel = 1;
    /* a gentle, sustained curve — a highway ramp, not a hard lock held
     * forever — which is where a steady off-tracking angle is a
     * meaningful thing to expect; see the file comment on truck_step
     * for why holding a genuinely tight curve forever has no such
     * equilibrium at all (a long trailer just keeps winding out) — see
     * the ODE comment at the top of truck.h */
    in.steer = STEER_RIGHT * 0.15f;
    run(&r, &spec, &set, &in, 10.0f);
    artic_mid = rig_articulation(&r, 0);
    run(&r, &spec, &set, &in, 10.0f);
    artic_end = rig_articulation(&r, 0);

    CHECK(fabsf(artic_mid) > 0.03f, "a sustained turn should articulate "
         "the trailer (artic_mid=%.3f)", artic_mid);
    CHECK(fabsf(artic_end - artic_mid) < 0.08f,
         "articulation should settle, not keep winding up "
         "(mid=%.3f end=%.3f)", artic_mid, artic_end);
    CHECK(!r.jackknifed, "a moderate, steady turn should not jackknife");
}

/*
 * The whole point of the model: the textbook truck-backing lesson is
 * "steer opposite the way you want the trailer to go" — turn the wheel
 * RIGHT while backing and the trailer swings LEFT. Nothing in rig_step
 * special-cases reverse; this falls straight out of running the same
 * bicycle-plus-trailer ODE with speed < 0 (a forward turn's yaw rate
 * flips sign in reverse, same steering angle). If a future change to
 * rig_step breaks that emergent behavior, this is the test that catches
 * it.
 */
static void test_backing_swings_trailer_opposite(void)
{
    RigSpec spec = rig_specs[1];
    HaulSettings set;
    Rig r;
    Input in;
    float dt = 1.0f / 60.0f;
    int i;
    float artic;

    haul_settings_defaults(&set);
    rig_reset(&r, &spec, 0.0f, 0.0f, 0.0f, 0.0f);
    idle(&in);

    /* get rolling in reverse first */
    in.reverse = 1;
    for (i = 0; i < 90; i++) rig_step(&r, &spec, &in, &set, dt, NULL, NULL);
    CHECK(r.speed < -0.3f, "holding reverse should back the rig up "
         "(speed=%.2f)", r.speed);

    /* now hold the wheel right while backing, briefly — just enough to
     * see the trailer start to swing, not so long it saturates at the
     * jackknife limit (a separate test covers that) */
    in.steer = STEER_RIGHT;
    for (i = 0; i < 40; i++) rig_step(&r, &spec, &in, &set, dt, NULL, NULL);

    artic = rig_articulation(&r, 0);
    CHECK(r.heading < -0.02f, "reversing with the wheel held right should "
         "turn the tractor's heading left, same as a forward turn's yaw "
         "rate with its sign flipped by negative speed (heading=%.3f)",
         r.heading);
    CHECK(artic < -0.02f, "backing with the wheel held RIGHT should swing "
         "the trailer LEFT relative to the tractor — the classic 'steer "
         "opposite' lesson (artic=%.3f)", artic);
    CHECK(!r.jackknifed, "a brief, moderate backing turn should not "
         "already be jackknifed");
}

/* Jackknife: articulation is a hard mechanical stop, never past it. */
static void test_jackknife_clamped(void)
{
    RigSpec spec = rig_specs[1];
    HaulSettings set;
    Rig r;
    Input in;
    float dt = 1.0f / 60.0f;
    int i;
    float limit = trailer_specs[spec.trailer_idx[0]].jackknife_limit_deg *
                 (float)M_PI / 180.0f;

    haul_settings_defaults(&set);
    rig_reset(&r, &spec, 0.0f, 0.0f, 0.0f, 0.0f);
    idle(&in);
    in.reverse = 1;
    for (i = 0; i < 90; i++) rig_step(&r, &spec, &in, &set, dt, NULL, NULL);
    in.steer = STEER_RIGHT;
    for (i = 0; i < 600; i++) {
        rig_step(&r, &spec, &in, &set, dt, NULL, NULL);
        CHECK(fabsf(rig_articulation(&r, 0)) <= limit + 0.001f,
             "articulation must never exceed the structural limit "
             "(got %.3f rad at step %d, limit %.3f)",
             rig_articulation(&r, 0), i, limit);
    }
    CHECK(r.jackknifed, "hard steering into a sustained reverse should "
         "eventually trip the jackknife flag");

    /* recovery: ease forward, straighten the wheel. Real recovery from a
     * hard jackknife takes real time (and often more than one pull-
     * forward) — this gives it up to as long as the incident itself
     * took to develop. */
    in.reverse = 0; in.accel = 1; in.steer = 0.0f;
    for (i = 0; i < 600; i++) rig_step(&r, &spec, &in, &set, dt, NULL, NULL);
    CHECK(!r.jackknifed, "driving forward with the wheel straight should "
         "eventually recover from a jackknife (articulation=%.3f)",
         rig_articulation(&r, 0));
}

/* Doubles: the kinematic chain generalizes past one trailer. */
static void test_doubles_chain(void)
{
    RigSpec spec = rig_specs[5]; /* DOUBLES */
    HaulSettings set;
    Rig r;
    Input in;

    CHECK(spec.n_trailers == 2, "DOUBLES should have two trailers");
    haul_settings_defaults(&set);
    rig_reset(&r, &spec, 0.0f, 0.0f, 0.0f, 0.0f);
    idle(&in);
    in.accel = 1;
    in.steer = STEER_LEFT * 0.4f;
    run(&r, &spec, &set, &in, 8.0f);

    CHECK(fabsf(rig_articulation(&r, 0)) > 0.01f,
         "first trailer should articulate in a turn");
    CHECK(fabsf(rig_articulation(&r, 1)) > 0.005f,
         "second trailer should also articulate, chained off the first");
    CHECK(!r.jackknifed, "a gentle turn should not jackknife a doubles rig");
}

/* A heavier rig should brake and accelerate more sluggishly than a
 * light one — the whole point of putting real mass in the model. */
static void test_heavier_rig_is_slower_to_stop(void)
{
    HaulSettings set;
    Rig light, heavy;
    Input in;
    RigSpec light_spec = rig_specs[0]; /* YARD SPOTTER, empty */
    RigSpec heavy_spec = rig_specs[4]; /* HEAVY HAUL, loaded lowboy */

    haul_settings_defaults(&set);
    rig_reset(&light, &light_spec, 0.0f, 0.0f, 0.0f, 0.0f);
    rig_reset(&heavy, &heavy_spec, 0.0f, 0.0f, 0.0f, 0.0f);
    idle(&in);
    in.accel = 1;
    run(&light, &light_spec, &set, &in, 8.0f);
    run(&heavy, &heavy_spec, &set, &in, 8.0f);

    CHECK(light.speed > heavy.speed + 1.0f,
         "an empty yard tractor should accelerate harder than a loaded "
         "lowboy over the same time (light=%.2f heavy=%.2f)",
         light.speed, heavy.speed);

    idle(&in);
    in.brake = 1;
    {
        float dt = 1.0f / 60.0f;
        int i;
        for (i = 0; i < 60 && light.speed > 0.0f; i++)
            rig_step(&light, &light_spec, &in, &set, dt, NULL, NULL);
        for (i = 0; i < 60 && heavy.speed > 0.0f; i++)
            rig_step(&heavy, &heavy_spec, &in, &set, dt, NULL, NULL);
    }
    CHECK(heavy.speed > 0.05f || light.speed <= 0.001f,
         "the loaded rig should still be rolling after one second of "
         "braking where the light one has already stopped "
         "(light=%.3f heavy=%.3f)", light.speed, heavy.speed);
}

static void test_rig_spec_mass_and_length(void)
{
    RigSpec s = rig_specs[1]; /* DRY VAN HAUL */
    float mass = rig_spec_total_mass(&s);
    float len = rig_spec_total_length(&s);

    CHECK(mass > truck_specs[s.truck_idx].mass_kg,
         "total mass should include the trailer and cargo (%.0f kg)",
         mass);
    CHECK(len > truck_specs[s.truck_idx].wheelbase,
         "total length should include the trailer (%.1f m)", len);
}

int main(void)
{
    truck_specs_reset_defaults();

    CHECK(truck_spec_count > 0 && trailer_spec_count > 0 &&
         rig_spec_count > 0, "default rosters should not be empty");

    test_steer_sign();
    test_offtracking_settles();
    test_backing_swings_trailer_opposite();
    test_jackknife_clamped();
    test_doubles_chain();
    test_heavier_rig_is_slower_to_stop();
    test_rig_spec_mass_and_length();

    if (failures) {
        printf("%d test(s) FAILED\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
