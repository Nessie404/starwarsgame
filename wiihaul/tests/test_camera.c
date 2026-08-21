/*
 * Host-side tests for WiiHaul's chase camera.
 *
 *   gcc -std=c99 -O2 -Wall -Werror -Isource \
 *       tests/test_camera.c source/truck.c source/camera.c \
 *       -lm -o wiihaul-test
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "truck.h"
#include "camera.h"

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

static void test_reset_places_camera_behind(void)
{
    RigSpec spec = rig_specs[1];
    HaulSettings set;
    Rig r;
    CameraState c;

    haul_settings_defaults(&set);
    rig_reset(&r, &spec, 0.0f, 0.0f, 0.0f, 0.0f); /* facing +X */
    camera_reset(&c, &set, &r, &spec, NULL, NULL);

    CHECK(c.x < 0.0f, "a fresh camera should sit behind the rig (x=%.2f)",
         c.x);
    CHECK(c.look_x > c.x, "the camera should look back toward the rig "
         "(look_x=%.2f cam_x=%.2f)", c.look_x, c.x);
    CHECK(c.orbit == 0.0f, "orbit should start at 0 (chase view)");
}

/* Reversing should swing the camera round toward the nose, same
 * mechanism WiiKart uses — this only matters here because the whole
 * game is built around it. */
static void test_reverse_swings_camera(void)
{
    RigSpec spec = rig_specs[1];
    HaulSettings set;
    Rig r;
    CameraState c;
    Input in;
    float dt = 1.0f / 60.0f;
    int i;

    haul_settings_defaults(&set);
    rig_reset(&r, &spec, 0.0f, 0.0f, 0.0f, 0.0f);
    camera_reset(&c, &set, &r, &spec, NULL, NULL);
    memset(&in, 0, sizeof(in));
    in.reverse = 1;

    for (i = 0; i < 300; i++) {
        rig_step(&r, &spec, &in, &set, dt, NULL, NULL);
        camera_update(&c, &set, &r, &spec, NULL, NULL, dt);
    }
    CHECK(c.orbit > 1.0f, "sustained reversing should swing the camera "
         "well round toward the nose (orbit=%.2f rad)", c.orbit);
}

static void test_distance_scales_with_rig_length(void)
{
    RigSpec bobtail = rig_specs[0];  /* YARD SPOTTER, shortest combo    */
    RigSpec doubles = rig_specs[5];  /* DOUBLES, the longest            */
    HaulSettings set;
    Rig rb, rd;
    CameraState cb, cd;
    float dist_b, dist_d;

    haul_settings_defaults(&set);
    rig_reset(&rb, &bobtail, 0.0f, 0.0f, 0.0f, 0.0f);
    rig_reset(&rd, &doubles, 0.0f, 0.0f, 0.0f, 0.0f);
    camera_reset(&cb, &set, &rb, &bobtail, NULL, NULL);
    camera_reset(&cd, &set, &rd, &doubles, NULL, NULL);

    dist_b = fabsf(cb.x - rb.x);
    dist_d = fabsf(cd.x - rd.x);
    CHECK(dist_d > dist_b, "the camera should sit further back for a "
         "longer rig (bobtail=%.1fm doubles=%.1fm)", dist_b, dist_d);
}

static void test_mirror_looks_backward_and_outward(void)
{
    RigSpec spec = rig_specs[1];
    Rig r;
    float ex, ey, ez, lx, ly, lz;

    rig_reset(&r, &spec, 0.0f, 0.0f, 0.0f, 0.0f); /* facing +X */
    mirror_camera(&r, &spec, -1.0f, &ex, &ey, &ez, &lx, &ly, &lz);
    (void)ey; (void)ly;

    CHECK(ez < 0.0f, "the driver's-side (-1) mirror should sit to the "
         "left of the rig (ez=%.2f)", ez);
    CHECK(lx < ex, "a mirror should look backward, not forward "
         "(look_x=%.2f eye_x=%.2f)", lx, ex);
}

int main(void)
{
    truck_specs_reset_defaults();

    test_reset_places_camera_behind();
    test_reverse_swings_camera();
    test_distance_scales_with_rig_length();
    test_mirror_looks_backward_and_outward();

    if (failures) {
        printf("%d test(s) FAILED\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
