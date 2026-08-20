/*
 * Host-side tests for the WiiKart simulation. Built with a normal PC
 * compiler (no devkitPPC needed):
 *
 *   gcc -std=c99 -O2 -Wall -Werror -Isource \
 *       tests/test_game.c source/game.c source/track.c source/config.c \
 *       source/camera.c -lm -o wiikart-test
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "config.h"
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

static GameConfig default_cfg(int track_id)
{
    GameConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.track_id = track_id;
    cfg.n_humans = 1;
    cfg.spec[0] = 1;   /* SPORT */
    return cfg;
}

static void idle_inputs(Input in[MAX_HUMANS])
{
    memset(in, 0, sizeof(Input) * MAX_HUMANS);
}

/* place the (human) kart on the centerline of a given segment, aligned
 * with the road, at a given speed */
static void teleport(Game *g, Kart *k, int seg, float speed)
{
    float frac, lat;
    k->x = g->track.px[seg];
    k->z = g->track.pz[seg];
    k->heading = atan2f(g->track.dz[seg], g->track.dx[seg]);
    k->speed = speed;
    k->slip = 0.0f;
    track_locate(&g->track, k->x, k->z, -1, &k->seg, &frac, &lat, &k->y);
    k->prog_raw = (float)k->seg + frac;
    k->total_progress = k->prog_raw;
}

/* as teleport(), but offset laterally from the centerline (positive =
 * left of the direction of travel) */
static void teleport_lat(Game *g, Kart *k, int seg, float lat, float speed)
{
    const Track *t = &g->track;
    float lx = -t->dz[seg], lz = t->dx[seg];
    float frac, got;

    k->x = t->px[seg] + lx * lat;
    k->z = t->pz[seg] + lz * lat;
    k->heading = atan2f(t->dz[seg], t->dx[seg]);
    k->speed = speed;
    k->slip = 0.0f;
    track_locate(t, k->x, k->z, -1, &k->seg, &frac, &got, &k->y);
    k->lat = got;
    k->prog_raw = (float)k->seg + frac;
    k->total_progress = k->prog_raw;
}

/* Pick whichever shoulder points away from nearby folds in the circuit.
 * That makes seam-adjacent cliff tests robust even when a wider road is
 * close to another segment in plan view. */
static void teleport_off_edge(Game *g, Kart *k, int seg, float speed)
{
    float distances[4] = { 6.0f, 10.0f, 16.0f, 24.0f };
    int i, sign;
    for (i = 0; i < 4; i++) {
        for (sign = -1; sign <= 1; sign += 2) {
            teleport_lat(g, k, seg,
                         (g->track.wall_half + distances[i]) * (float)sign,
                         speed);
            if (fabsf(k->lat) > g->track.wall_half + 0.5f)
                return;
        }
    }
    CHECK(0, "%s segment %d has no reachable outside shoulder",
          g->track.name, seg);
}

/* ------------------------------------------------------------------ */

static void test_tracks_geometry(void)
{
    int id, i;
    for (id = 0; id < TRACK_COUNT; id++) {
        Track t;
        track_init(&t, id);
        printf("track %-8s: %3d samples, %6.0f m, climb %3.0f m\n",
               t.name, t.n, t.total_len, t.max_y - t.min_y);
        CHECK(t.n >= 64 && t.n <= TRACK_MAX_POINTS, "samples %d", t.n);
        CHECK(t.total_len > 400.0f && t.total_len < 5000.0f,
              "length %.0f", t.total_len);
        for (i = 0; i < t.n; i++) {
            CHECK(!isnan(t.px[i]) && !isnan(t.py[i]) && !isnan(t.pz[i]),
                  "NaN point %d on track %d", i, id);
            CHECK(t.seg_len[i] > 0.02f && t.seg_len[i] < 40.0f,
                  "odd seg len %.3f at %d on track %d",
                  t.seg_len[i], i, id);
            CHECK(fabsf(t.slope[i]) < 0.30f,
                  "grade %.0f%% at %d on track %d too steep",
                  t.slope[i] * 100.0f, i, id);
            CHECK(t.curv[i] >= 0.0f && t.curv[i] < 0.4f,
                  "curvature %.3f at %d on track %d", t.curv[i], i, id);
        }
        if (id != TRACK_CLASSIC) {
            CHECK(t.max_y - t.min_y > 30.0f, "pass %d too flat", id);
            CHECK(t.alpine, "pass %d not alpine", id);
        } else {
            CHECK(t.max_y - t.min_y < 1.0f, "classic not flat");
        }
    }
}

/* A guardrail set well back from the pavement leaves a shoulder wide
 * enough to straight-line a corner across — reduced grip out there, but
 * often still a faster line than the actual apex. Every barriered track
 * now keeps the rail within a curb's width of the road surface so there
 * is nowhere meaningful left to cut through. */
static void test_pavement_reaches_guardrail(void)
{
    int id;
    for (id = 0; id < TRACK_COUNT; id++) {
        Track t;
        float gap;
        track_init(&t, id);
        if (!t.has_walls)
            continue;
        gap = t.wall_half - t.road_half;
        printf("track %-8s: road %.1f m, rail %.1f m, %.1f m shoulder\n",
               t.name, t.road_half * 2.0f, t.wall_half * 2.0f, gap);
        CHECK(gap >= 0.0f && gap < 2.0f,
              "%s has a %.1f m shoulder — wide enough to cut a corner",
              t.name, gap);
    }
}

static void test_spec_stats(void)
{
    int i;
    for (i = 0; i < SPEC_COUNT; i++) {
        const KartSpec *s = &kart_specs[i];
        float t100 = spec_accel_time(s);
        float top = spec_top_speed(s);
        printf("spec %-6s: %3.0f hp %5.0f kg  0-100 %4.1f s  top %3.0f km/h"
               "  100-0 %2.0f m  %.2f g\n",
               s->name, s->power_hp, s->mass_kg, t100, top,
               s->brake_dist_100, s->lat_g);
        CHECK(t100 > 2.0f && t100 < 15.0f, "0-100 %.1f s odd (%s)",
              t100, s->name);
        CHECK(top > 120.0f && top < 320.0f, "top %.0f km/h odd (%s)",
              top, s->name);
    }
}

static void test_countdown_holds(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    int f;

    game_init(&g, &cfg);
    idle_inputs(in);
    in[0].accel = 1;
    float x0 = g.karts[0].x;
    for (f = 0; f < 60; f++)
        game_update(&g, in, 1.0f / 60.0f);
    CHECK(g.state == STATE_COUNTDOWN, "countdown ended early");
    CHECK(fabsf(g.karts[0].x - x0) < 1e-4f, "kart moved in countdown");
}

static void test_braking_distance(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    int f;

    game_init(&g, &cfg);
    g.state = STATE_RACING;                 /* skip countdown */
    idle_inputs(in);

    /* SPORT from exactly 100 km/h, full brake on the flat straight */
    teleport(&g, &g.karts[0], 2, 27.78f);
    float x0 = g.karts[0].x, z0 = g.karts[0].z;
    in[0].brake = 1;
    for (f = 0; f < 60 * 8 && g.karts[0].speed > 0.3f; f++)
        game_update(&g, in, 1.0f / 60.0f);
    {
        float dx = g.karts[0].x - x0, dz = g.karts[0].z - z0;
        float dist = sqrtf(dx * dx + dz * dz);
        float spec = kart_specs[1].brake_dist_100;
        printf("braking 100-0: %.1f m (spec %.0f m)\n", dist, spec);
        CHECK(fabsf(dist - spec) < spec * 0.18f,
              "stopping distance %.1f m vs spec %.0f m", dist, spec);
    }
}

static void test_gravity_grade(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_BERTHOUD);
    Input in[MAX_HUMANS];
    int f, seg_up = -1, seg_down = -1, i;
    float v_up, v_down;

    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);

    /* Find a climb and a descent that stay straight for the whole coast,
     * otherwise the car runs into a corner and the test measures the
     * barrier rather than gravity. */
    for (i = 0; i < g.track.n; i++) {
        int j, ok_up = 1, ok_down = 1;
        for (j = 0; j < 7; j++) {
            int m = (i + j) % g.track.n;
            if (g.track.curv[m] > 0.012f) {
                ok_up = ok_down = 0;
                break;
            }
            if (g.track.slope[m] < 0.045f)  ok_up = 0;
            if (g.track.slope[m] > -0.045f) ok_down = 0;
        }
        if (seg_up < 0 && ok_up)     seg_up = i;
        if (seg_down < 0 && ok_down) seg_down = i;
    }
    CHECK(seg_up >= 0 && seg_down >= 0,
          "no straight climb/descent found (up %d down %d)",
          seg_up, seg_down);
    if (seg_up < 0 || seg_down < 0) return;

    /* coast from 15 m/s for 2 s on each */
    teleport(&g, &g.karts[0], seg_up, 15.0f);
    for (f = 0; f < 120; f++) game_update(&g, in, 1.0f / 60.0f);
    v_up = g.karts[0].speed;

    teleport(&g, &g.karts[0], seg_down, 15.0f);
    for (f = 0; f < 120; f++) game_update(&g, in, 1.0f / 60.0f);
    v_down = g.karts[0].speed;

    printf("coasting 2 s from 15 m/s: uphill -> %.1f, downhill -> %.1f\n",
           v_up, v_down);
    CHECK(v_up < 14.0f, "uphill did not slow the kart (%.1f)", v_up);
    CHECK(v_down > v_up + 1.5f, "no gravity gain downhill (%.1f vs %.1f)",
          v_down, v_up);
}

static void test_cornering_grip_cap(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    float h0, yaw_rate, cap;

    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);

    teleport(&g, &g.karts[0], 2, 25.0f);
    h0 = g.karts[0].heading;
    in[0].steer = 1.0f;
    game_update(&g, in, 1.0f / 60.0f);
    yaw_rate = fabsf(game_angle_wrap(g.karts[0].heading - h0)) * 60.0f;
    cap = kart_specs[1].lat_g * GRAVITY / 25.0f;
    printf("yaw at 25 m/s full lock: %.2f rad/s (grip cap %.2f)\n",
           yaw_rate, cap);
    CHECK(yaw_rate <= cap * 1.05f, "yaw %.2f exceeds grip cap %.2f",
          yaw_rate, cap);
    CHECK(g.karts[0].slip > 0.1f, "no understeer slip at full lock");
}

/* Three cars, identical in every way except which axle(s) drive them,
 * must actually behave differently: AWD splits the acceleration traction
 * demand across two axles and gets there quicker off the line, and a
 * driven axle spends some of its own grip on traction rather than
 * cornering, so a front-driven car understeers more under power than a
 * rear-driven one. If drivetrain were plumbed through but not read by the
 * physics, this would still pass on lap times but fail here. */
static const char *drivetrain_test_cars =
    "{\"cars\":["
    "{\"name\":\"RWDCAR\",\"mass_kg\":1000,\"power_hp\":400,"
    "\"brake_distance_100_kph_m\":35,\"lateral_grip_g\":1.20,"
    "\"drag_area_m2\":0.60,\"wheelbase_m\":2.50,\"offroad_grip\":0.50,"
    "\"gear_top_speeds_kph\":[300],\"drivetrain\":{\"type\":\"rwd\"}},"
    "{\"name\":\"FWDCAR\",\"mass_kg\":1000,\"power_hp\":400,"
    "\"brake_distance_100_kph_m\":35,\"lateral_grip_g\":1.20,"
    "\"drag_area_m2\":0.60,\"wheelbase_m\":2.50,\"offroad_grip\":0.50,"
    "\"gear_top_speeds_kph\":[300],\"drivetrain\":{\"type\":\"fwd\"}},"
    "{\"name\":\"AWDCAR\",\"mass_kg\":1000,\"power_hp\":400,"
    "\"brake_distance_100_kph_m\":35,\"lateral_grip_g\":1.20,"
    "\"drag_area_m2\":0.60,\"wheelbase_m\":2.50,\"offroad_grip\":0.50,"
    "\"gear_top_speeds_kph\":[300],"
    "\"drivetrain\":{\"type\":\"awd\",\"front_bias\":0.5}}"
    "]}";

static void test_drivetrain_traction(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    char error[80];
    float v_rwd, v_fwd, v_awd;
    int f, spec;
    float *out[3];

    CHECK(config_load_cars_text(drivetrain_test_cars, error,
                                (int)sizeof(error)),
          "drivetrain test cars did not load: %s", error);

    out[0] = &v_rwd; out[1] = &v_fwd; out[2] = &v_awd;
    for (spec = 0; spec < 3; spec++) {
        cfg.spec[0] = spec;
        game_init(&g, &cfg);
        g.state = STATE_RACING;
        idle_inputs(in);
        g.karts[0].speed = 0.0f;
        in[0].accel = 1;
        for (f = 0; f < 60; f++)
            game_update(&g, in, 1.0f / 60.0f);
        *out[spec] = g.karts[0].speed;
    }

    printf("drivetrain traction off the line: RWD %.2f, FWD %.2f, "
           "AWD %.2f m/s after 1 s\n", v_rwd, v_fwd, v_awd);
    CHECK(fabsf(v_rwd - v_fwd) < 0.05f,
          "RWD (%.2f) and FWD (%.2f) should be equally traction-limited",
          v_rwd, v_fwd);
    CHECK(v_awd > v_rwd * 1.08f,
          "AWD (%.2f) did not out-accelerate RWD/FWD (%.2f)", v_awd, v_rwd);

    kart_specs_reset_defaults();
}

static void test_drivetrain_cornering_balance(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    char error[80];
    float h0, yaw_rwd, yaw_fwd;
    int spec;
    float *out[2];

    CHECK(config_load_cars_text(drivetrain_test_cars, error,
                                (int)sizeof(error)),
          "drivetrain test cars did not load: %s", error);

    out[0] = &yaw_rwd; out[1] = &yaw_fwd;
    for (spec = 0; spec < 2; spec++) {
        cfg.spec[0] = spec;
        game_init(&g, &cfg);
        g.state = STATE_RACING;
        idle_inputs(in);
        g.karts[0].speed = 25.0f;
        h0 = g.karts[0].heading;
        in[0].accel = 1;
        in[0].steer = 1.0f;
        game_update(&g, in, 1.0f / 60.0f);
        *out[spec] = fabsf(game_angle_wrap(g.karts[0].heading - h0)) * 60.0f;
    }

    printf("drivetrain cornering under power: RWD yaw %.3f, "
           "FWD yaw %.3f rad/s\n", yaw_rwd, yaw_fwd);
    CHECK(yaw_rwd > yaw_fwd * 1.03f,
          "RWD (%.3f) was not looser under power than FWD (%.3f)",
          yaw_rwd, yaw_fwd);

    kart_specs_reset_defaults();
}

static void test_drivetrain_json_parsing(void)
{
    char error[80];
    const char *no_block =
        "{\"cars\":[{\"name\":\"PLAIN\",\"mass_kg\":1000,"
        "\"power_hp\":150,\"brake_distance_100_kph_m\":35,"
        "\"lateral_grip_g\":1.10,\"drag_area_m2\":0.60,"
        "\"wheelbase_m\":2.50,\"offroad_grip\":0.50,"
        "\"gear_top_speeds_kph\":[60,110,160]}]}";
    const char *bad_type =
        "{\"cars\":[{\"name\":\"BAD\",\"mass_kg\":1000,"
        "\"power_hp\":150,\"brake_distance_100_kph_m\":35,"
        "\"lateral_grip_g\":1.10,\"drag_area_m2\":0.60,"
        "\"wheelbase_m\":2.50,\"offroad_grip\":0.50,"
        "\"gear_top_speeds_kph\":[60,110,160],"
        "\"drivetrain\":{\"type\":\"4wd\"}}]}";
    const char *bad_bias =
        "{\"cars\":[{\"name\":\"BAD\",\"mass_kg\":1000,"
        "\"power_hp\":150,\"brake_distance_100_kph_m\":35,"
        "\"lateral_grip_g\":1.10,\"drag_area_m2\":0.60,"
        "\"wheelbase_m\":2.50,\"offroad_grip\":0.50,"
        "\"gear_top_speeds_kph\":[60,110,160],"
        "\"drivetrain\":{\"type\":\"awd\",\"front_bias\":1.5}}]}";

    CHECK(config_load_cars_text(drivetrain_test_cars, error,
                                (int)sizeof(error)),
          "drivetrain test cars did not load: %s", error);
    CHECK(kart_specs[0].drivetrain == DRIVETRAIN_RWD, "RWDCAR misparsed");
    CHECK(kart_specs[1].drivetrain == DRIVETRAIN_FWD, "FWDCAR misparsed");
    CHECK(kart_specs[2].drivetrain == DRIVETRAIN_AWD &&
          fabsf(kart_specs[2].awd_front_bias - 0.5f) < 0.001f,
          "AWDCAR misparsed");

    CHECK(config_load_cars_text(no_block, error, (int)sizeof(error)),
          "car with no drivetrain block did not load: %s", error);
    CHECK(kart_specs[0].drivetrain == DRIVETRAIN_RWD,
          "a car with no drivetrain block should default to RWD");

    CHECK(!config_load_cars_text(bad_type, error, (int)sizeof(error)),
          "an unknown drivetrain type was accepted");
    CHECK(!config_load_cars_text(bad_bias, error, (int)sizeof(error)),
          "an out-of-range AWD front_bias was accepted");

    kart_specs_reset_defaults();
}

/* Overcooking a corner a little should barely register; overcooking it
 * a lot has to really cost something — the scrub is not a flat rate
 * per unit of slip, it gets steeper the further past the limit the car
 * is. */
static void test_understeer_scrub_is_progressive(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    float v_before, mild_loss, severe_loss;

    /* mild: just over the limit (gentle steer well past what full grip
     * allows at this speed, but not by a huge margin) */
    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    teleport(&g, &g.karts[0], 2, 25.0f);
    v_before = g.karts[0].speed;
    in[0].steer = 0.15f;
    game_update(&g, in, 1.0f / 60.0f);
    mild_loss = v_before - g.karts[0].speed;
    CHECK(g.karts[0].slip > 0.0f && g.karts[0].slip < 0.7f,
          "mild case is not actually mild (slip %.2f)", g.karts[0].slip);

    /* severe: full lock, the same everything else */
    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    teleport(&g, &g.karts[0], 2, 25.0f);
    v_before = g.karts[0].speed;
    in[0].steer = 1.0f;
    game_update(&g, in, 1.0f / 60.0f);
    severe_loss = v_before - g.karts[0].speed;
    CHECK(g.karts[0].slip > 0.7f,
          "severe case is not actually severe (slip %.2f)",
          g.karts[0].slip);

    printf("understeer scrub: mild slip loses %.4f m/s/frame, severe "
           "slip loses %.4f m/s/frame\n", mild_loss, severe_loss);
    /* if the scrub were purely proportional to slip, severe (slip near
     * 1.0) would lose a bit more than double what mild (slip well
     * under 0.5) loses; the progressive curve on top has to make it
     * lose noticeably more than that */
    CHECK(severe_loss > mild_loss * 2.5f,
          "understeer scrub is not progressive (mild %.4f, severe %.4f)",
          mild_loss, severe_loss);
}

/*
 * Power oversteer: a rear-driven car committing hard to a corner on the
 * throttle can rotate faster than pure grip would allow, for free, for
 * as long as the driver keeps the wheel turned hard — ease off in time
 * and it settles back down having gained real rotation over an
 * identical front-driven car; keep it locked over and it spins,
 * losing a lot of speed and control for a while.
 */
static void test_oversteer_rewards_a_catch_and_punishes_a_miss(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    int f;
    float rwd_heading_caught, fwd_heading, v_before_spin, v_after_spin;

    /* --- caught: RWD gains more rotation than an identical FWD car,
     * and does not spin --- */
    {
        char error[80];
        CHECK(config_load_cars_text(drivetrain_test_cars, error,
                                    (int)sizeof(error)),
              "drivetrain test cars did not load: %s", error);
    }
    cfg.spec[0] = 0;   /* RWDCAR */
    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    teleport(&g, &g.karts[0], 2, 25.0f);
    in[0].accel = 1;
    in[0].steer = 1.0f;
    for (f = 0; f < 30; f++) {          /* 0.5 s: builds oversteer      */
        game_update(&g, in, 1.0f / 60.0f);
    }
    in[0].steer = 0.2f;                 /* ease off: catch it           */
    for (f = 0; f < 60; f++)
        game_update(&g, in, 1.0f / 60.0f);
    rwd_heading_caught = fabsf(g.karts[0].heading);
    CHECK(g.karts[0].spin_t <= 0.0f,
          "an oversteer that was caught should not still be spinning");

    cfg.spec[0] = 1;   /* FWDCAR, identical stats otherwise */
    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    teleport(&g, &g.karts[0], 2, 25.0f);
    in[0].accel = 1;
    in[0].steer = 1.0f;
    for (f = 0; f < 30; f++)
        game_update(&g, in, 1.0f / 60.0f);
    in[0].steer = 0.2f;
    for (f = 0; f < 60; f++)
        game_update(&g, in, 1.0f / 60.0f);
    fwd_heading = fabsf(g.karts[0].heading);
    CHECK(g.karts[0].oversteer_t <= 0.0f,
          "a front-driven car should never enter power oversteer");

    printf("oversteer reward: RWD rotated %.3f rad, identical FWD "
           "rotated %.3f rad over the same 1.5 s\n",
           rwd_heading_caught, fwd_heading);
    CHECK(rwd_heading_caught > fwd_heading * 1.05f,
          "a caught oversteer did not out-rotate the identical FWD car "
          "(%.3f vs %.3f)", rwd_heading_caught, fwd_heading);

    /* --- missed: the same RWD car, held flat out with no correction,
     * has to spin: heavy speed loss and slip pinned at 1.0 --- */
    cfg.spec[0] = 0;
    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    teleport(&g, &g.karts[0], 2, 25.0f);
    in[0].accel = 1;
    in[0].steer = 1.0f;
    for (f = 0; f < 65; f++)            /* past oversteer_spin_seconds  */
        game_update(&g, in, 1.0f / 60.0f);
    CHECK(g.karts[0].spin_t > 0.0f,
          "holding the wheel over for a full second under power never "
          "spun the car");
    v_before_spin = g.karts[0].speed;
    for (f = 0; f < 20; f++)
        game_update(&g, in, 1.0f / 60.0f);
    v_after_spin = g.karts[0].speed;
    printf("missed oversteer: %.1f -> %.1f m/s through the spin, "
           "slip %.2f\n", v_before_spin, v_after_spin, g.karts[0].slip);
    CHECK(v_after_spin < v_before_spin - 3.0f,
          "a spin did not cost real speed (%.1f -> %.1f)",
          v_before_spin, v_after_spin);
    CHECK(g.karts[0].slip > 0.9f, "a spinning car should read as fully "
          "sliding, not %.2f", g.karts[0].slip);

    kart_specs_reset_defaults();
}

/* Gentle driving must never trip the oversteer mechanism — this is the
 * regression test for a first cut of the risk condition that compared
 * yaw_cmd against the grip cap directly, which broke down for
 * short-wheelbase cars (their yaw_cmd so outsizes their own cap that
 * "catching" it would have meant nearly releasing the wheel). Risk and
 * catch are both judged on the steering input instead, which means the
 * same 0.6 of lock means the same thing in any car. */
static void test_oversteer_never_triggers_when_driving_gently(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    int f, spec;

    for (spec = 0; spec < SPEC_COUNT; spec++) {
        cfg.spec[0] = spec;
        game_init(&g, &cfg);
        g.state = STATE_RACING;
        idle_inputs(in);
        teleport(&g, &g.karts[0], 2, 20.0f);
        in[0].accel = 1;
        in[0].steer = 0.15f;
        for (f = 0; f < 120; f++)
            game_update(&g, in, 1.0f / 60.0f);
        CHECK(g.karts[0].oversteer_t <= 0.0f && g.karts[0].spin_t <= 0.0f,
              "%s entered oversteer from gentle steering",
              kart_specs[spec].name);
    }
}

/* The whole AI field must be able to finish a full race on every track,
 * cleanly: no NaNs, nobody outside the barriers, and a sane winning lap
 * time for the circuit's length. */
static void test_ai_races_all_tracks(void)
{
    int id;
    for (id = 0; id < TRACK_COUNT; id++) {
        Game g;
        GameConfig cfg = default_cfg(id);
        Input in[MAX_HUMANS];
        int f, i, done = 0, finished = 0;
        /* BERTHOUD 2.0 races roughly double the distance of anything
         * else on the roster by design, so the budget has to cover that
         * rather than the old shorter roster's typical race. */
        const int max_frames = 60 * 800;
        float best_lap = 1e9f, lap_start[NUM_KARTS];
        int last_lap[NUM_KARTS];

        game_init(&g, &cfg);
        idle_inputs(in);
        for (i = 0; i < NUM_KARTS; i++) {
            last_lap[i] = g.karts[i].lap;
            lap_start[i] = 0.0f;
        }

        for (f = 0; f < max_frames && !done; f++) {
            game_update(&g, in, 1.0f / 60.0f);
            done = 1;
            for (i = 1; i < NUM_KARTS; i++) {
                Kart *k = &g.karts[i];
                CHECK(!isnan(k->x) && !isnan(k->speed) && !isnan(k->heading),
                      "AI %d NaN on track %d frame %d", i, id, f);
                if (k->fall_t <= 0.0f && k->respawn_t <= 0.0f)
                    CHECK(fabsf(k->lat) <
                              track_wall_half(&g.track, k->seg) + 2.0f,
                          "AI %d outside the road without falling on track "
                          "%d (lat %.1f, road allows %.1f)", i, id, k->lat,
                          track_wall_half(&g.track, k->seg));
                if (failures) return;
                if (k->lap > last_lap[i]) {
                    float lt = g.race_t - lap_start[i];
                    if (last_lap[i] >= 0 && lt < best_lap)
                        best_lap = lt;
                    last_lap[i] = k->lap;
                    lap_start[i] = g.race_t;
                }
                if (!k->finished)
                    done = 0;
            }
        }

        for (i = 1; i < NUM_KARTS; i++)
            if (g.karts[i].finished)
                finished++;

        printf("track %-8s: %2d/%d AI finished %d laps, best AI lap %.1f s "
               "(%.0f m)\n", g.track.name, finished, NUM_KARTS - 1,
               g.track.laps, best_lap, g.track.total_len);
        CHECK(finished == NUM_KARTS - 1,
              "only %d of %d AI finished on track %d", finished,
              NUM_KARTS - 1, id);
        /* a plausible pace for the distance: 8..36 m/s average. (Corners
         * were widened across the roster to close off shoulder-cutting
         * and the AI's skill multiplier went up, so the fastest,
         * gentlest circuits run quicker than the 32 m/s this cap used
         * to allow.) */
        CHECK(best_lap > g.track.total_len / 36.0f &&
              best_lap < g.track.total_len / 8.0f,
              "best lap %.1f s implausible for %.0f m on track %d",
              best_lap, g.track.total_len, id);
    }
}

static void test_full_race_classic(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    int f, i, done = 0;

    cfg.n_humans = 2;          /* exercise multi-human path (P2 idles) */
    cfg.spec[1] = 0;
    game_init(&g, &cfg);
    idle_inputs(in);

    for (f = 0; f < 60 * 300 && !done; f++) {
        game_update(&g, in, 1.0f / 60.0f);
        done = 1;
        for (i = 2; i < NUM_KARTS; i++)
            if (!g.karts[i].finished)
                done = 0;
    }
    CHECK(done, "AI did not finish %d laps on classic", g.track.laps);
    if (done) {
        int seen[NUM_KARTS + 1] = { 0 };
        printf("classic full race: AI done at %.0f s\n", f / 60.0f);
        for (i = 2; i < NUM_KARTS; i++) {
            Kart *k = &g.karts[i];
            CHECK(k->final_rank >= 1 && k->final_rank <= NUM_KARTS,
                  "bad final rank %d", k->final_rank);
            CHECK(!seen[k->final_rank], "dup final rank %d", k->final_rank);
            seen[k->final_rank] = 1;
        }
    }
    /* ranks always a permutation */
    {
        int seen[NUM_KARTS + 1] = { 0 };
        for (i = 0; i < NUM_KARTS; i++) {
            CHECK(g.karts[i].rank >= 1 && g.karts[i].rank <= NUM_KARTS,
                  "bad rank");
            CHECK(!seen[g.karts[i].rank], "dup rank");
            seen[g.karts[i].rank] = 1;
        }
    }
}

/* The virtual analog stick: holding a key must wind the wheel on
 * progressively rather than snapping to full lock, self-center quickly
 * when released, and be calmer at speed than at a crawl. */
static void test_steering_filter(void)
{
    SteerAxis a;
    const float dt = 1.0f / 60.0f;
    float s1, s_quarter, s_half, s_full, s_fast;
    int f;

    /* one frame of "A held" from centre is a nudge, not a yank */
    steer_axis_reset(&a);
    s1 = steer_axis_update(&a, 1.0f, 20.0f, dt);
    CHECK(s1 > 0.0f && s1 < 0.06f, "first frame steer %.3f too abrupt", s1);

    /* ramps up over a few tenths of a second, monotonically */
    steer_axis_reset(&a);
    for (f = 0; f < 6; f++)  s_quarter = steer_axis_update(&a, 1.0f, 20.0f, dt);
    for (; f < 15; f++)      s_half    = steer_axis_update(&a, 1.0f, 20.0f, dt);
    for (; f < 90; f++)      s_full    = steer_axis_update(&a, 1.0f, 20.0f, dt);
    printf("steering ramp at 20 m/s: 0.1s %.2f  0.25s %.2f  1.5s %.2f\n",
           s_quarter, s_half, s_full);
    CHECK(s_quarter < s_half && s_half < s_full, "ramp not monotonic");
    CHECK(s_quarter < 0.35f, "0.1 s already at %.2f", s_quarter);
    CHECK(s_full > 0.97f, "full lock never reached (%.2f)", s_full);

    /* releasing snaps back toward centre faster than it wound on */
    for (f = 0; f < 6; f++)
        steer_axis_update(&a, 0.0f, 20.0f, dt);
    CHECK(fabsf(steer_axis_update(&a, 0.0f, 20.0f, dt)) < 0.35f,
          "wheel does not self-centre");

    /* the same input is gentler at speed than at a crawl */
    steer_axis_reset(&a);
    for (f = 0; f < 12; f++)
        s_half = steer_axis_update(&a, 1.0f, 5.0f, dt);
    steer_axis_reset(&a);
    for (f = 0; f < 12; f++)
        s_fast = steer_axis_update(&a, 1.0f, 45.0f, dt);
    printf("0.2 s of full input: %.2f at 5 m/s, %.2f at 45 m/s\n",
           s_half, s_fast);
    CHECK(s_fast < s_half * 0.85f,
          "no speed sensitivity (%.2f slow vs %.2f fast)", s_half, s_fast);

    /* symmetric: A and D mirror each other */
    {
        SteerAxis l, r;
        float vl = 0.0f, vr = 0.0f;
        steer_axis_reset(&l);
        steer_axis_reset(&r);
        for (f = 0; f < 20; f++) {
            vl = steer_axis_update(&l,  1.0f, 20.0f, dt);
            vr = steer_axis_update(&r, -1.0f, 20.0f, dt);
        }
        CHECK(fabsf(vl + vr) < 1e-5f, "left/right asymmetric (%.3f/%.3f)",
              vl, vr);
    }
}

/*
 * Steering sign convention, checked the way the player experiences it.
 *
 * The renderer places the chase camera behind the car with up = +Y and
 * looks along the heading, so guLookAt gives it a right-hand axis of
 *     right = cross(forward, up) = (-sin h, 0, cos h)
 * This test re-derives that vector independently and asserts that a
 * STEER_LEFT input actually moves the car toward the LEFT of the screen
 * (a negative projection onto the camera's right axis) and STEER_RIGHT
 * toward the right. Checking only "heading increases" is what let the
 * controls ship mirrored, so do not weaken this back to that.
 */
static void screen_right_axis(float heading, float *rx, float *rz)
{
    *rx = -sinf(heading);
    *rz =  cosf(heading);
}

static float steer_screen_drift(float steer_input)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    float x0, z0, h0, rx, rz;
    int f;

    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);

    teleport(&g, &g.karts[0], 2, 20.0f);
    x0 = g.karts[0].x;
    z0 = g.karts[0].z;
    h0 = g.karts[0].heading;
    screen_right_axis(h0, &rx, &rz);

    in[0].steer = steer_input;
    for (f = 0; f < 45; f++)
        game_update(&g, in, 1.0f / 60.0f);

    /* how far the car ended up to the camera's right of where it began */
    return (g.karts[0].x - x0) * rx + (g.karts[0].z - z0) * rz;
}

static void test_steer_sign(void)
{
    float left = steer_screen_drift(STEER_LEFT);
    float right = steer_screen_drift(STEER_RIGHT);

    printf("steering: STEER_LEFT drifts %+.2f m across the screen, "
           "STEER_RIGHT %+.2f m\n", left, right);
    CHECK(left < -0.5f,
          "STEER_LEFT moved the car %+.2f m on screen (should go left, "
          "i.e. negative)", left);
    CHECK(right > 0.5f,
          "STEER_RIGHT moved the car %+.2f m on screen (should go right, "
          "i.e. positive)", right);
    CHECK(fabsf(left + right) < 0.35f,
          "left and right are not mirror images (%+.2f vs %+.2f)",
          left, right);

    /* and the same relationship must hold for the lateral bookkeeping the
     * AI and the walls rely on: positive lat is the camera's right */
    {
        Game g;
        GameConfig cfg = default_cfg(TRACK_CLASSIC);
        Input in[MAX_HUMANS];
        float rx, rz, proj;
        int seg = 2;

        game_init(&g, &cfg);
        screen_right_axis(atan2f(g.track.dz[seg], g.track.dx[seg]),
                          &rx, &rz);
        idle_inputs(in);
        teleport_lat(&g, &g.karts[0], seg, 3.0f, 0.0f);
        proj = (g.karts[0].x - g.track.px[seg]) * rx +
               (g.karts[0].z - g.track.pz[seg]) * rz;
        CHECK(g.karts[0].lat > 2.0f,
              "teleport_lat(+3) gave lat %.2f", g.karts[0].lat);
        CHECK(proj > 2.0f,
              "positive lat is not the camera's right (projection %.2f)",
              proj);
    }
}


/* ------------------------------------------------------------------ */
/* v1.1: a bigger field, individual strategies, learning, adaptation   */
/* ------------------------------------------------------------------ */

/* Corners are what the AI learn about, so the segmentation has to be
 * sane: several per lap, contiguous ids, and plausible radii. */
static void test_corner_segmentation(void)
{
    int id;
    for (id = 0; id < TRACK_COUNT; id++) {
        Track t;
        int i, c, seen[TRACK_MAX_CORNERS];
        track_init(&t, id);
        {
            float peak = 0.0f;
            for (i = 0; i < t.n_corners; i++)
                if (t.corner_peak[i] > peak)
                    peak = t.corner_peak[i];
            printf("track %-8s: %2d corners, tightest R = %.0f m\n",
                   t.name, t.n_corners, peak > 0.0f ? 1.0f / peak : 0.0f);
        }
        CHECK(t.n_corners >= 4 && t.n_corners <= TRACK_MAX_CORNERS,
              "track %d has %d corners", id, t.n_corners);
        for (c = 0; c < TRACK_MAX_CORNERS; c++)
            seen[c] = 0;
        for (i = 0; i < t.n; i++) {
            c = t.corner_id[i];
            CHECK(c >= -1 && c < t.n_corners,
                  "bad corner id %d at seg %d on track %d", c, i, id);
            if (c >= 0)
                seen[c]++;
        }
        for (c = 0; c < t.n_corners; c++) {
            CHECK(seen[c] > 0, "corner %d on track %d has no segments",
                  c, id);
            CHECK(t.corner_peak[c] > 0.005f && t.corner_peak[c] < 0.25f,
                  "corner %d curvature %.3f implausible", c,
                  t.corner_peak[c]);
        }
        /* no single corner may swallow most of the lap, or a mistake
         * could not be attributed to a specific piece of road */
        for (c = 0; c < t.n_corners; c++)
            CHECK(seen[c] < t.n / 3,
                  "corner %d covers %d of %d segments on track %d",
                  c, seen[c], t.n, id);
    }
}

/*
 * Berthoud Pass 2.0 is a real switchback stack (climb, summit, descent,
 * valley loop-back, climb home) — that character is the whole point of
 * the circuit and it has been smoothed away by well-meaning "fix the
 * geometry" passes before: v1.17.0 replaced the actual hairpins with a
 * gentle sine-wiggle shape to make a self-intersection check pass,
 * which incidentally made the track boring. v1.19.1 brought the real
 * hairpins back (see track.c) once it turned out the "self-intersection"
 * was never a gameplay bug in the first place — track_locate is always
 * called with a windowed hint during driving and grid placement (the
 * only place it isn't, decorative tree scatter in main.c, doesn't
 * matter if it's occasionally wrong) so two switchback tiers landing
 * close together in plan view, at very different elevations, was never
 * actually confusable at the wheel. This test is the tripwire: if a
 * future pass flattens the corner count or radius back down chasing a
 * diagram-flat minimap, this fails.
 */
static void test_berthoud2_keeps_its_switchbacks(void)
{
    Track t;
    float peak = 0.0f;
    int i;

    track_init(&t, TRACK_BERTHOUD2);
    for (i = 0; i < t.n_corners; i++)
        if (t.corner_peak[i] > peak)
            peak = t.corner_peak[i];
    printf("Berthoud 2.0: %d corners, tightest R %.0f m, %.0f m climb\n",
           t.n_corners, peak > 0.0f ? 1.0f / peak : 0.0f,
           t.max_y - t.min_y);
    CHECK(t.n_corners >= 25,
          "Berthoud 2.0 has only %d corners — the switchback stack got "
          "smoothed away again", t.n_corners);
    CHECK(peak > 0.0f && 1.0f / peak < 20.0f,
          "Berthoud 2.0's tightest corner is a %.0f m radius — that is "
          "not a real hairpin any more", 1.0f / peak);
    CHECK(t.max_y - t.min_y > 60.0f,
          "Berthoud 2.0 lost most of its climb (%.0f m)",
          t.max_y - t.min_y);
}

/* A twelve-car grid has to fit on the road, including the back row that
 * sits ~40 m behind the line and often round a bend. */
static void test_full_grid_fits(void)
{
    int id, i, j;
    for (id = 0; id < TRACK_COUNT; id++) {
        Game g;
        GameConfig cfg = default_cfg(id);
        game_init(&g, &cfg);
        for (i = 0; i < NUM_KARTS; i++) {
            Kart *k = &g.karts[i];
            CHECK(fabsf(k->lat) <= g.track.road_half + 0.5f,
                  "grid slot %d off the road on track %d (lat %.2f)",
                  i, id, k->lat);
            CHECK(k->total_progress < 0.5f,
                  "grid slot %d starts past the line on track %d (%.2f)",
                  i, id, k->total_progress);
            for (j = i + 1; j < NUM_KARTS; j++) {
                float dx = g.karts[j].x - k->x;
                float dz = g.karts[j].z - k->z;
                CHECK(dx * dx + dz * dz > 1.6f * 1.6f,
                      "grid slots %d and %d overlap on track %d", i, j, id);
            }
        }
    }
}

/* Run a race and hand back the game state for the behavioural tests. */
static void race_for(Game *g, int track_id, float seconds,
                     int *mist_thirds)
{
    GameConfig cfg = default_cfg(track_id);
    Input in[MAX_HUMANS];
    int f, i, frames = (int)(seconds * 60.0f);

    game_init(g, &cfg);
    idle_inputs(in);
    if (mist_thirds)
        mist_thirds[0] = mist_thirds[1] = mist_thirds[2] = 0;

    for (f = 0; f < frames; f++) {
        int before = 0, after = 0, third = (f * 3) / frames;
        if (mist_thirds)
            for (i = 1; i < NUM_KARTS; i++)
                before += g->karts[i].mistakes;
        game_update(g, in, 1.0f / 60.0f);
        if (mist_thirds) {
            for (i = 1; i < NUM_KARTS; i++)
                after += g->karts[i].mistakes;
            mist_thirds[third > 2 ? 2 : third] += after - before;
        }
    }
}

/* YOLO is the "no guts, no glory" sheet: on paper it has to out-commit
 * every other strategy in the roster, and at least a couple of drivers
 * have to actually be racing on it rather than it sitting unused. */
static void test_yolo_strategy_is_the_wildest(void)
{
    int i, yolo_drivers = 0;
    const AIStrategy *yolo = &ai_strategies[AI_YOLO];

    for (i = 0; i < AI_STRATEGY_COUNT; i++) {
        if (i == AI_YOLO) continue;
        CHECK(yolo->conf_start >= ai_strategies[i].conf_start,
              "%s is more overconfident than YOLO", ai_strategies[i].name);
        CHECK(yolo->attack >= ai_strategies[i].attack,
              "%s attacks more than YOLO", ai_strategies[i].name);
        CHECK(yolo->defend <= ai_strategies[i].defend,
              "%s defends less than YOLO", ai_strategies[i].name);
    }

    for (i = 0; i < ai_driver_count(); i++)
        if (ai_driver(i)->strategy == AI_YOLO)
            yolo_drivers++;
    printf("YOLO drivers in the field: %d\n", yolo_drivers);
    CHECK(yolo_drivers >= 2, "fewer than two drivers actually race YOLO");
}

/*
 * A wild strategy still has to actually finish races rather than get
 * stuck falling off the same unguarded corner forever — this is the
 * regression test for exactly that bug: an early YOLO tuning was
 * overconfident enough that one driver could respawn straight back
 * into failing the same corner on Monarch, over and over, without ever
 * completing a lap.
 */
static void test_yolo_can_finish_the_hardest_track(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_MONARCH);
    Input in[MAX_HUMANS];
    int f, i, done = 0, yolo_kart = -1;

    game_init(&g, &cfg);
    idle_inputs(in);
    for (i = 1; i < NUM_KARTS; i++)
        if (g.karts[i].strategy == AI_YOLO) { yolo_kart = i; break; }
    CHECK(yolo_kart >= 0, "no YOLO driver in the default field");

    for (f = 0; f < 60 * 400 && !done; f++) {
        game_update(&g, in, 1.0f / 60.0f);
        done = g.karts[yolo_kart].finished;
    }
    printf("YOLO driver on MONARCH: finished=%d after %.0f s, %d falls\n",
           g.karts[yolo_kart].finished, (float)f / 60.0f,
           g.karts[yolo_kart].falls);
    CHECK(done, "the YOLO driver never finished MONARCH in 400 s");
}

/* Eleven AI on eight strategy sheets must actually drive differently:
 * different lines, different error counts, different pace. */
static void test_ai_strategies_differ(void)
{
    Game g;
    int i, distinct_lines = 0, min_mist = 1 << 30, max_mist = -1;
    float min_conf = 9.9f, max_conf = -9.9f;
    float lines[NUM_KARTS];

    race_for(&g, TRACK_CLASSIC, 100.0f, NULL);

    for (i = 1; i < NUM_KARTS; i++) {
        Kart *k = &g.karts[i];
        float conf = ai_corner_conf(k, &g.track, g.track.corner_entry[0]);
        int j, dup = 0;

        CHECK(k->strategy >= 0 && k->strategy < AI_STRATEGY_COUNT,
              "kart %d has bad strategy %d", i, k->strategy);
        if (k->mistakes < min_mist) min_mist = k->mistakes;
        if (k->mistakes > max_mist) max_mist = k->mistakes;
        if (conf < min_conf) min_conf = conf;
        if (conf > max_conf) max_conf = conf;

        lines[i] = ai_strategies[k->strategy].line_bias;
        for (j = 1; j < i; j++)
            if (fabsf(lines[j] - lines[i]) < 0.05f)
                dup = 1;
        if (!dup)
            distinct_lines++;
    }

    printf("field spread: mistakes %d..%d, corner-1 nerve %.2f..%.2f, "
           "%d distinct racing lines\n",
           min_mist, max_mist, min_conf, max_conf, distinct_lines);
    CHECK(distinct_lines >= 4, "only %d distinct racing lines",
          distinct_lines);
    CHECK(max_mist >= min_mist + 3,
          "error counts too uniform (%d..%d)", min_mist, max_mist);
    CHECK(max_conf - min_conf > 0.04f,
          "learned nerve too uniform (%.3f..%.3f)", min_conf, max_conf);
}

/* Learning: the field should make fewer mistakes as the race goes on,
 * the overconfident sheets should talk themselves down, and the timid
 * one should find pace. */
static void test_ai_learns_from_mistakes(void)
{
    Game g;
    int thirds[3];
    int i, checked_bold = 0, checked_timid = 0, total_events = 0;
    int bold_mistakes = 0;

    race_for(&g, TRACK_CLASSIC, 150.0f, thirds);

    printf("mistakes by race third: %d / %d / %d\n",
           thirds[0], thirds[1], thirds[2]);
    CHECK(thirds[0] > 0, "no mistakes at all to learn from");
    CHECK(thirds[2] < thirds[0],
          "field did not get tidier (%d then %d)", thirds[0], thirds[2]);

    for (i = 1; i < NUM_KARTS; i++) {
        Kart *k = &g.karts[i];
        const AIStrategy *st = &ai_strategies[k->strategy];
        float sum = 0.0f, mean;
        int c;

        total_events += k->learn_events;
        for (c = 0; c < g.track.n_corners; c++)
            sum += k->corner_conf[c];
        mean = sum / (float)g.track.n_corners;

        CHECK(mean >= 0.70f && mean <= st->conf_max + 0.01f,
              "%s ended with nerve %.3f outside [0.70, %.3f]",
              st->name, mean, st->conf_max);

        if (k->strategy == AI_LATE || k->strategy == AI_CHARGER ||
            k->strategy == AI_YOLO) {
            /* started believing it could beat the grip limit */
            CHECK(mean < st->conf_start - 0.02f,
                  "%s never learned to brake earlier (%.3f from %.3f)",
                  st->name, mean, st->conf_start);
            /* one grippy car under one bold driver can go a whole race
             * clean — it is the cohort that has to have something to
             * learn from, not every single pairing */
            bold_mistakes += k->mistakes;
            checked_bold = 1;
        }
        if (k->strategy == AI_CRUISER && mean > st->conf_start + 0.02f)
            checked_timid = 1;   /* a timid sheet finding extra pace */
    }
    CHECK(checked_bold, "did not exercise a bold strategy");
    CHECK(bold_mistakes > 0,
          "no bold-strategy driver made a mistake to learn from");
    CHECK(checked_timid,
          "no cautious driver found any extra pace over the race");
    CHECK(total_events > 50, "only %d learning updates", total_events);
}

/* Park a human on an AI's bumper and check the AI covers the side that
 * human has been passing on — the same setup with the opposite learned
 * habit must produce the opposite defensive line. */
static float defended_line(float learned_pass_side)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    Kart *ai = NULL;
    int i, f;

    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);

    for (i = 1; i < NUM_KARTS; i++)
        if (g.karts[i].strategy == AI_DEFENDER) {
            ai = &g.karts[i];
            break;
        }
    if (!ai)
        return 0.0f;

    g.pmodel[0].pass_side = learned_pass_side;
    teleport(&g, ai, 40, 22.0f);
    ai->line_target = 0.0f;

    for (f = 0; f < 90; f++) {
        /* hold the human just behind the AI, in its mirrors */
        int behind = (ai->seg - 1 + g.track.n) % g.track.n;
        teleport(&g, &g.karts[0], behind, 22.0f);
        g.karts[0].prev_progress = g.karts[0].total_progress;
        g.pmodel[0].pass_side = learned_pass_side;
        game_update(&g, in, 1.0f / 60.0f);
    }
    return ai->line_target;
}

static void test_ai_adapts_to_player(void)
{
    float left = defended_line(1.0f);
    float right = defended_line(-1.0f);

    printf("defender line vs a left-side passer %.2f m, "
           "right-side passer %.2f m\n", left, right);
    CHECK(left > right + 0.5f,
          "defender ignores which side the player passes on (%.2f vs %.2f)",
          left, right);
    CHECK(left > 0.0f && right < 0.0f,
          "defender covered the wrong side (%.2f / %.2f)", left, right);
}

/* The player model itself: overtakes are noticed, with the side they
 * happened on, and contact is counted. */
static void test_player_model_learns(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    Kart *ai = NULL;
    int i, f;

    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);

    for (i = 1; i < NUM_KARTS; i++)
        if (g.karts[i].strategy == AI_CRUISER) {
            ai = &g.karts[i];
            break;
        }
    CHECK(ai != NULL, "no cruiser in the field");
    if (!ai) return;

    /* clear the road: move everyone else far up the track so only this
     * one AI is in play */
    for (i = 1; i < NUM_KARTS; i++)
        if (&g.karts[i] != ai)
            teleport_lat(&g, &g.karts[i], 70 + i, 0.0f, 0.0f);

    /* the AI ambles along on the right, the human arrives fast on the
     * left: the overtake should be logged as a left-side pass */
    teleport_lat(&g, ai, 30, -2.0f, 6.0f);
    teleport_lat(&g, &g.karts[0], 28, 3.0f, 30.0f);

    for (f = 0; f < 120 && g.pmodel[0].passes == 0; f++)
        game_update(&g, in, 1.0f / 60.0f);

    printf("player model: %d pass(es), side bias %.2f\n",
           g.pmodel[0].passes, g.pmodel[0].pass_side);
    CHECK(g.pmodel[0].passes > 0, "overtake was not noticed");
    CHECK(g.pmodel[0].pass_side > 0.1f,
          "pass side learned wrong (%.2f, expected positive/left)",
          g.pmodel[0].pass_side);
}


/*
 * No rubber-banding. The same AI car, from a standstill on the same piece
 * of road, must accelerate identically whether the human is right next to
 * it or half a lap up the road. Anything else means the game is handing
 * out horsepower based on position, which is what makes a race feel
 * scripted.
 */
static float ai_launch_speed(int human_seg)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    Kart *ai;
    int i, f;

    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);

    ai = &g.karts[1];
    /* park the rest of the field out of the way so traffic cannot
     * influence the measurement */
    for (i = 2; i < NUM_KARTS; i++)
        teleport(&g, &g.karts[i], (30 + i * 4) % g.track.n, 0.0f);

    teleport(&g, &g.karts[0], human_seg, 25.0f);
    teleport(&g, ai, 4, 0.0f);
    ai->corner_conf[0] = ai->corner_conf[0];   /* leave learning alone */

    for (f = 0; f < 120; f++)
        game_update(&g, in, 1.0f / 60.0f);
    return ai->speed;
}

static void test_no_rubber_banding(void)
{
    float chased = ai_launch_speed(6);     /* human alongside          */
    float dropped = ai_launch_speed(70);   /* human way up the road    */

    printf("no-elastic check: AI reaches %.2f m/s with the human "
           "alongside, %.2f m/s with the human half a lap ahead\n",
           chased, dropped);
    CHECK(fabsf(chased - dropped) < 0.25f,
          "AI gained %.2f m/s when left behind — that is rubber-banding",
          dropped - chased);
}

/* ------------------------------------------------------------------ */
/* v1.2: gearboxes, cliffs and checkpoints, the two new passes          */
/* ------------------------------------------------------------------ */

/* The gear power curve: bogging below the band, full in it, nothing at
 * the limiter (which is what stops a gear pulling past its top speed). */
static void test_gear_power_curve(void)
{
    printf("gear curve: bog %.2f  band %.2f  peak-out %.2f  limiter %.2f\n",
           gear_power_scale(0.15f), gear_power_scale(0.70f),
           gear_power_scale(0.97f), gear_power_scale(1.10f));
    CHECK(gear_power_scale(0.15f) < 0.8f, "bogging is not penalised");
    CHECK(gear_power_scale(0.70f) > 0.99f, "no full power in the band");
    CHECK(gear_power_scale(0.97f) < 1.0f &&
          gear_power_scale(0.97f) > 0.7f, "past-peak falloff wrong");
    CHECK(gear_power_scale(1.10f) == 0.0f, "limiter still makes power");
    /* monotonic through the bog region */
    CHECK(gear_power_scale(0.05f) < gear_power_scale(0.25f),
          "bog region not monotonic");
}

/* Every car must have a sane gearbox: rising ratios, and a top gear that
 * roughly matches the drag-limited top speed. */
static void test_gearboxes_sane(void)
{
    int i, gi;
    for (i = 0; i < SPEC_COUNT; i++) {
        const KartSpec *s = &kart_specs[i];
        CHECK(s->n_gears >= 3 && s->n_gears <= MAX_GEARS,
              "%s has %d gears", s->name, s->n_gears);
        for (gi = 1; gi < s->n_gears; gi++)
            CHECK(s->gear_top[gi] > s->gear_top[gi - 1] * 1.15f,
                  "%s gear %d is not taller than %d", s->name, gi + 1, gi);
        printf("%-6s %d gears, 1st tops %.0f km/h, top gear %.0f km/h\n",
               s->name, s->n_gears, s->gear_top[0] * 3.6f,
               s->gear_top[s->n_gears - 1] * 3.6f);
    }
}

/* A manual box only shifts on a fresh press, and an automatic works its
 * way up the box under power without hunting. */
static void test_shifting(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    int f, shifts, start_gear;

    /* --- manual: holding the button must not run through the box --- */
    cfg.gearbox[0] = GEARBOX_MANUAL;
    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    teleport(&g, &g.karts[0], 2, 30.0f);
    in[0].gear_up = 1;
    start_gear = g.karts[0].gear;
    for (f = 0; f < 90; f++)
        game_update(&g, in, 1.0f / 60.0f);
    printf("manual: holding upshift for 1.5 s moved %d gear(s)\n",
           g.karts[0].gear - start_gear);
    CHECK(g.karts[0].gear == start_gear + 1,
          "held upshift changed %d gears, should be exactly 1",
          g.karts[0].gear - start_gear);

    /* releasing and pressing again gives another gear */
    in[0].gear_up = 0;
    game_update(&g, in, 1.0f / 60.0f);
    in[0].gear_up = 1;
    for (f = 0; f < 30; f++)
        game_update(&g, in, 1.0f / 60.0f);
    CHECK(g.karts[0].gear == start_gear + 2, "second press did not shift");

    /* --- automatic: climbs the box, and does not hunt at steady speed */
    cfg.gearbox[0] = GEARBOX_AUTO;
    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    /* Nobody is steering here, so the car will eventually run wide on a
     * curving track; take the PEAK gear rather than the gear it happens
     * to be in once it has found a barrier. */
    teleport(&g, &g.karts[0], 2, 2.0f);
    in[0].accel = 1;
    {
        int top_gear = 0;
        float vpeak = 0.0f;
        for (f = 0; f < 60 * 12; f++) {
            game_update(&g, in, 1.0f / 60.0f);
            if (g.karts[0].gear > top_gear) top_gear = g.karts[0].gear;
            if (fabsf(g.karts[0].speed) > vpeak)
                vpeak = fabsf(g.karts[0].speed);
        }
        printf("auto: worked up to gear %d of %d, peak %.0f km/h\n",
               top_gear + 1, kart_specs[g.karts[0].spec].n_gears,
               vpeak * 3.6f);
        CHECK(top_gear > 0, "automatic never upshifted");
        CHECK(top_gear >= 2, "automatic only reached gear %d", top_gear + 1);
    }

    /* Hunting is a property of the gearbox, not of the driving, so hold
     * the speed fixed and simply count gear changes: a sane box settles
     * on one gear and stays there. */
    {
        int last;
        game_init(&g, &cfg);
        g.state = STATE_RACING;
        idle_inputs(in);
        teleport(&g, &g.karts[0], 2, 26.0f);
        in[0].accel = 1;
        last = g.karts[0].gear;
        shifts = 0;
        for (f = 0; f < 60 * 6; f++) {
            g.karts[0].speed = 26.0f;      /* pin the speed */
            game_update(&g, in, 1.0f / 60.0f);
            if (g.karts[0].gear != last) {
                shifts++;
                last = g.karts[0].gear;
            }
        }
        printf("auto: %d shift(s) while pinned at 26 m/s for 6 s\n", shifts);
        CHECK(shifts <= 2, "automatic gearbox is hunting (%d shifts)",
              shifts);
    }
}

/*
 * Boost has to actually be driven by revs, not just by whether the
 * throttle is down — the same throttle input at low revs and at high
 * revs in the same gear must charge the meter at very different rates,
 * following a curve rather than a flat rate. Any shift has to wipe
 * whatever is charged, and the use button has to spend it all at once
 * for an immediate speed bump rather than a rate change over time.
 */
static void test_boost_system(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    float top, delta_high, delta_low, before, after;

    cfg.gearbox[0] = GEARBOX_MANUAL;

    /* --- builds with revs, on a curve, not with the throttle alone --- */
    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    g.karts[0].gear = 2;
    top = kart_specs[g.karts[0].spec].gear_top[2];
    teleport(&g, &g.karts[0], 2, top * 0.92f);   /* deep in the band */
    in[0].accel = 1;
    game_update(&g, in, 1.0f / 60.0f);
    delta_high = g.karts[0].boost_meter;

    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    g.karts[0].gear = 2;
    teleport(&g, &g.karts[0], 2, top * 0.10f);   /* same gear, low revs */
    in[0].accel = 1;
    game_update(&g, in, 1.0f / 60.0f);
    delta_low = g.karts[0].boost_meter;

    printf("boost build: %.4f/frame near redline, %.4f/frame low in the "
           "band (same gear, same throttle)\n", delta_high, delta_low);
    CHECK(delta_high > 0.0f, "boost never builds at all");
    CHECK(delta_high > delta_low * 4.0f,
          "boost does not build on a curve with revs (%.4f near redline "
          "vs %.4f low in the band)", delta_high, delta_low);

    /* --- any shift wipes the meter --- */
    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    g.karts[0].gear = 1;
    teleport(&g, &g.karts[0], 2,
             kart_specs[g.karts[0].spec].gear_top[1] * 0.95f);
    in[0].accel = 1;
    game_update(&g, in, 1.0f / 60.0f);
    CHECK(g.karts[0].boost_meter > 0.0f, "boost never built up to shift away");
    in[0].gear_up = 1;
    game_update(&g, in, 1.0f / 60.0f);
    CHECK(g.karts[0].gear == 2, "did not actually shift");
    CHECK(g.karts[0].boost_meter == 0.0f,
          "boost survived a shift (%.3f left)", g.karts[0].boost_meter);

    /* --- the use button spends the meter for an instant speed bump --- */
    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    teleport(&g, &g.karts[0], 2, 25.0f);
    g.karts[0].boost_meter = 0.6f;
    before = g.karts[0].speed;
    in[0].boost = 1;
    game_update(&g, in, 1.0f / 60.0f);
    after = g.karts[0].speed;
    printf("boost use: %.1f -> %.1f m/s spending a 0.6 meter (max bonus "
           "%.1f m/s)\n", before, after, g.settings.boost_max_speed_bonus_mps);
    CHECK(after - before > 0.6f * g.settings.boost_max_speed_bonus_mps * 0.8f,
          "the use button did not give an instant speed bump (%.1f -> "
          "%.1f)", before, after);
    CHECK(g.karts[0].boost_meter == 0.0f,
          "the use button did not spend the meter");

    /* holding the button after the meter is empty does nothing more */
    before = g.karts[0].speed;
    game_update(&g, in, 1.0f / 60.0f);
    after = g.karts[0].speed;
    CHECK(after - before < 1.0f,
          "an empty meter still gave a boost (%.1f -> %.1f)", before, after);
}

/* A gear caps speed: in first, with a manual box, the car cannot pull
 * past the limiter no matter how long you hold the throttle. */
static void test_gear_limits_speed(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    float top1;
    int f;

    cfg.gearbox[0] = GEARBOX_MANUAL;
    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    teleport(&g, &g.karts[0], 2, 2.0f);
    in[0].accel = 1;
    {
        float vpeak = 0.0f;
        for (f = 0; f < 60 * 10; f++) {
            game_update(&g, in, 1.0f / 60.0f);
            if (fabsf(g.karts[0].speed) > vpeak)
                vpeak = fabsf(g.karts[0].speed);
        }
        top1 = kart_specs[g.karts[0].spec].gear_top[0];
        printf("first gear: peak %.1f m/s, limiter at %.1f m/s\n",
               vpeak, top1);
        CHECK(g.karts[0].gear == 0, "manual box shifted itself");
        CHECK(vpeak < top1 * 1.04f,
              "pulled past the first-gear limiter (%.1f vs %.1f)",
              vpeak, top1);
        CHECK(vpeak > top1 * 0.85f,
              "never reached the first-gear limiter (%.1f vs %.1f)",
              vpeak, top1);
    }
}

/* Tire compounds have to actually trade grip against slipperiness. */
static void test_tire_compounds(void)
{
    GameSettings st;
    int c;

    printf("tires: soft grip x%.2f drag x%.2f, hard grip x%.2f drag x%.2f\n",
           tire_grip_mult(TIRE_SOFT), tire_drag_mult(TIRE_SOFT),
           tire_grip_mult(TIRE_HARD), tire_drag_mult(TIRE_HARD));
    CHECK(tire_grip_mult(TIRE_SOFT) > tire_grip_mult(TIRE_MEDIUM),
          "softs do not grip more");
    CHECK(tire_grip_mult(TIRE_MEDIUM) > tire_grip_mult(TIRE_HARD),
          "hards do not grip less");
    CHECK(tire_drag_mult(TIRE_HARD) < tire_drag_mult(TIRE_SOFT),
          "hards are not the slipperier tire");

    /* the compound trade-offs, as parameters rather than as lap times */
    game_settings_defaults(&st);
    CHECK(st.tire_wear_rate[TIRE_SOFT] > st.tire_wear_rate[TIRE_MEDIUM] &&
          st.tire_wear_rate[TIRE_MEDIUM] > st.tire_wear_rate[TIRE_HARD],
          "softs do not wear out faster than hards");
    CHECK(st.tire_rolling_mult[TIRE_HARD] < st.tire_rolling_mult[TIRE_MEDIUM],
          "hards do not roll better");
    CHECK(st.tire_temp_optimal[TIRE_SOFT] < st.tire_temp_optimal[TIRE_HARD],
          "softs do not want to run cooler than hards");

    /* cold, warm and overheated rubber, and worn rubber */
    for (c = 0; c < TIRE_COMPOUNDS; c++) {
        float cold = tire_condition_grip(&st, c, 10.0f, 0.0f);
        float warm = tire_condition_grip(&st, c, st.tire_temp_optimal[c],
                                         0.0f);
        float hot = tire_condition_grip(&st, c,
                                        st.tire_temp_optimal[c] +
                                        st.tire_temp_window[c] * 2.0f, 0.0f);
        float worn = tire_condition_grip(&st, c, st.tire_temp_optimal[c],
                                         1.0f);
        printf("  %-6s cold %.2f  warm %.2f  overheated %.2f  worn out %.2f\n",
               tire_name(c), cold, warm, hot, worn);
        CHECK(warm > cold && warm > hot,
              "%s does not have a temperature window", tire_name(c));
        CHECK(worn < warm, "%s does not lose grip as it wears",
              tire_name(c));
        CHECK(cold > 0.5f && hot > 0.5f,
              "%s falls off a cliff outside its window", tire_name(c));
    }
    CHECK(tire_condition_grip(&st, TIRE_SOFT, st.tire_temp_optimal[TIRE_SOFT],
                              1.0f) <
          tire_condition_grip(&st, TIRE_MEDIUM,
                              st.tire_temp_optimal[TIRE_MEDIUM], 1.0f),
          "a worn-out soft is not worse than a worn-out medium");
}

/* Weather is CLASSIC-only: every other circuit's zones stay empty no
 * matter what. */
static void test_weather_only_on_classic(void)
{
    int id;
    for (id = 0; id < TRACK_COUNT; id++) {
        Track t;
        int i, any = 0;
        track_init(&t, id);
        for (i = 0; i < t.n; i++)
            if (t.weather_zone[i] >= 0)
                any = 1;
        if (id == TRACK_CLASSIC) {
            CHECK(t.n_weather_zones > 0 && any,
                  "CLASSIC has no weather zones");
        } else {
            CHECK(t.n_weather_zones == 0 && !any,
                  "%s has weather zones of its own", t.name);
        }
    }
}

/* A patch starts as snow, melts into ice, then a puddle, and stays a
 * puddle — it does not thaw back into anything drier. */
static void test_weather_melts_over_time(void)
{
    Track t;
    GameSettings st;
    int seg = -1, i;

    track_init(&t, TRACK_CLASSIC);
    game_settings_defaults(&st);
    for (i = 0; i < t.n; i++)
        if (t.weather_zone[i] == 0) { seg = i; break; }
    CHECK(seg >= 0, "could not find weather zone 0 on CLASSIC");

    CHECK(track_weather_at(&t, seg, 0.0f, &st) == WEATHER_SNOW,
          "zone 0 is not snow at the green flag");
    CHECK(track_weather_at(&t, seg, st.weather_snow_to_ice_s + 1.0f, &st) ==
              WEATHER_ICE,
          "zone 0 did not melt from snow into ice");
    CHECK(track_weather_at(&t, seg, st.weather_ice_to_puddle_s + 1.0f,
                           &st) == WEATHER_PUDDLE,
          "zone 0 did not melt from ice into a puddle");
    CHECK(track_weather_at(&t, seg, st.weather_ice_to_puddle_s + 5000.0f,
                           &st) == WEATHER_PUDDLE,
          "a puddle dried back into something else");

    /* off the zone entirely, pavement is always clear regardless of
     * the race clock */
    {
        int clear_seg = -1;
        for (i = 0; i < t.n; i++)
            if (t.weather_zone[i] < 0) { clear_seg = i; break; }
        CHECK(clear_seg >= 0, "CLASSIC has no clear pavement left");
        CHECK(track_weather_at(&t, clear_seg, 500.0f, &st) == WEATHER_CLEAR,
              "bare pavement reported weather");
    }
}

/* Soft rubber is the tire to have in snow and ice and the worst choice
 * once it is a puddle; hard rubber is exactly the other way round; the
 * medium compound never wins or loses that trade. Clear pavement never
 * touches any of this. */
static void test_weather_tire_grip_ordering(void)
{
    GameSettings st;
    int w;

    game_settings_defaults(&st);
    CHECK(weather_tire_grip_mult(&st, WEATHER_SNOW, TIRE_SOFT) >
              weather_tire_grip_mult(&st, WEATHER_SNOW, TIRE_MEDIUM) &&
          weather_tire_grip_mult(&st, WEATHER_SNOW, TIRE_MEDIUM) >
              weather_tire_grip_mult(&st, WEATHER_SNOW, TIRE_HARD),
          "soft is not the best tire in snow");
    CHECK(weather_tire_grip_mult(&st, WEATHER_ICE, TIRE_SOFT) >
              weather_tire_grip_mult(&st, WEATHER_ICE, TIRE_MEDIUM) &&
          weather_tire_grip_mult(&st, WEATHER_ICE, TIRE_MEDIUM) >
              weather_tire_grip_mult(&st, WEATHER_ICE, TIRE_HARD),
          "soft is not the best tire on ice");
    CHECK(weather_tire_grip_mult(&st, WEATHER_PUDDLE, TIRE_HARD) >
              weather_tire_grip_mult(&st, WEATHER_PUDDLE, TIRE_MEDIUM) &&
          weather_tire_grip_mult(&st, WEATHER_PUDDLE, TIRE_MEDIUM) >
              weather_tire_grip_mult(&st, WEATHER_PUDDLE, TIRE_SOFT),
          "hard is not the best tire in a puddle");
    for (w = WEATHER_SNOW; w <= WEATHER_PUDDLE; w++)
        CHECK(weather_tire_grip_mult(&st, w, TIRE_SOFT) < 1.0f &&
              weather_tire_grip_mult(&st, w, TIRE_MEDIUM) < 1.0f &&
              weather_tire_grip_mult(&st, w, TIRE_HARD) < 1.0f,
              "%s gives some tire a bonus over clean, dry pavement",
              weather_name(w));
    CHECK(weather_tire_grip_mult(&st, WEATHER_CLEAR, TIRE_SOFT) == 1.0f &&
          weather_tire_grip_mult(&st, WEATHER_CLEAR, TIRE_HARD) == 1.0f,
          "clear pavement is not neutral");
}

/* The point of the whole system is that it reaches the physics: a hard
 * tire on a snowed-over corner has to actually understeer more than a
 * soft tire does, not just carry a settings value nobody reads. */
static void test_weather_reaches_the_physics(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    int seg = -1, i;
    float h0, yaw_soft, yaw_hard;

    game_init(&g, &cfg);
    for (i = 0; i < g.track.n; i++)
        if (g.track.weather_zone[i] == 0) { seg = i; break; }
    CHECK(seg >= 0, "could not find a weather zone to test on");
    g.race_t = 0.0f;               /* zone 0 is fresh snow at t=0 */

    g.state = STATE_RACING;
    idle_inputs(in);
    g.karts[0].tire = TIRE_SOFT;
    teleport(&g, &g.karts[0], seg, 20.0f);
    h0 = g.karts[0].heading;
    in[0].steer = 1.0f;
    game_update(&g, in, 1.0f / 60.0f);
    yaw_soft = fabsf(game_angle_wrap(g.karts[0].heading - h0)) * 60.0f;

    game_init(&g, &cfg);
    g.race_t = 0.0f;
    g.state = STATE_RACING;
    idle_inputs(in);
    g.karts[0].tire = TIRE_HARD;
    teleport(&g, &g.karts[0], seg, 20.0f);
    h0 = g.karts[0].heading;
    in[0].steer = 1.0f;
    game_update(&g, in, 1.0f / 60.0f);
    yaw_hard = fabsf(game_angle_wrap(g.karts[0].heading - h0)) * 60.0f;

    printf("snow at the wheels: soft tire yaw %.3f, hard tire yaw %.3f\n",
           yaw_soft, yaw_hard);
    CHECK(yaw_soft > yaw_hard * 1.05f,
          "a soft tire (%.3f) is not meaningfully better than a hard "
          "one (%.3f) in fresh snow", yaw_soft, yaw_hard);
}

/* Standing water should cost more than cornering grip — it drags at the
 * car in a straight line too, on top of whatever the tire's own
 * drag_multiplier already costs. Coast the same kart through the same
 * spot once while it is still snow (no extra drag) and once well into
 * the puddle stage, and the puddle has to bleed off more speed. */
static void test_weather_puddle_drag(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    int seg = -1, i;
    float v_snow, v_puddle;

    game_init(&g, &cfg);
    for (i = 0; i < g.track.n; i++)
        if (g.track.weather_zone[i] == 0) { seg = i; break; }
    CHECK(seg >= 0, "could not find a weather zone to test on");

    g.state = STATE_RACING;
    idle_inputs(in);
    g.race_t = 0.0f;                    /* zone 0 is fresh snow at t=0 */
    teleport(&g, &g.karts[0], seg, 30.0f);
    game_update(&g, in, 1.0f / 60.0f);
    v_snow = g.karts[0].speed;

    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    g.race_t = 200.0f;      /* well past ice_to_puddle_seconds for zone 0 */
    teleport(&g, &g.karts[0], seg, 30.0f);
    game_update(&g, in, 1.0f / 60.0f);
    v_puddle = g.karts[0].speed;

    printf("puddle drag: coasting from 30 m/s reaches %.3f m/s over snow, "
           "%.3f m/s over a puddle\n", v_snow, v_puddle);
    CHECK(v_puddle < v_snow,
          "a puddle cost no extra speed while coasting (%.3f vs %.3f)",
          v_puddle, v_snow);
}

/*
 * The AI's corner-speed lookahead now discounts grip the same way the
 * physics itself does for whatever is under the wheels — a segment sat
 * in a weather patch gets approached slower, same as a corner this
 * driver has learned to respect. Hard rubber is the one tire the
 * puddle stage treats far better than the snow stage (0.55 -> 0.92,
 * the sharpest swing in the whole weather table): send the same AI
 * driver, on the same hard tire, through the same zone once while it
 * is still fresh snow and once once it has become a puddle, and a
 * weather-aware driver should genuinely go faster through the puddle
 * — the grip really is there now, and it should know it, rather than
 * driving both exactly the same because it cannot see the difference.
 */
static void test_ai_weather_awareness(void)
{
    int variant;
    float end_speed[2];

    for (variant = 0; variant < 2; variant++) {
        Game g;
        GameConfig cfg = default_cfg(TRACK_CLASSIC);
        Input in[MAX_HUMANS];
        int seg = -1, i, f;

        game_init(&g, &cfg);
        for (i = 0; i < g.track.n; i++)
            if (g.track.weather_zone[i] == 0) { seg = i; break; }
        CHECK(seg >= 0, "could not find a weather zone to test on");

        g.state = STATE_RACING;
        /* zone 0: fresh snow at t=0 (hard tire grip 0.55), a settled
         * puddle by t=200 (hard tire grip 0.92) — same tire throughout */
        g.race_t = (variant == 0) ? 0.0f : 200.0f;
        idle_inputs(in);
        /* TOURER: front_bias 0.50 puts rwd_bias at exactly 0.50, which
         * is not > 0.50 — the power-oversteer mechanic never engages,
         * so this isolates the weather-aware speed target from the
         * separate (and separately tested) oversteer/spin behaviour */
        g.karts[1].spec = 3;
        g.karts[1].tire = TIRE_HARD;
        teleport(&g, &g.karts[1], seg, 15.0f);

        for (f = 0; f < 150; f++)
            game_update(&g, in, 1.0f / 60.0f);
        end_speed[variant] = g.karts[1].speed;
    }

    printf("AI on hard tires: %.2f m/s through fresh snow, %.2f m/s "
           "through the same spot once it is a puddle\n",
           end_speed[0], end_speed[1]);
    CHECK(end_speed[1] > end_speed[0] * 1.05f,
          "the AI did not speed up once the same tire's grip genuinely "
          "improved (%.2f m/s in snow vs %.2f m/s in the puddle) — it "
          "looks like it cannot see the weather change",
          end_speed[0], end_speed[1]);
}

/*
 * The point of compounds is that none of them is the answer. Run the same
 * race on each and check that the sprint and the long haul want different
 * rubber — this is the test that stops softs quietly becoming the only
 * sensible choice again.
 *
 * Averaged over several AI drivers rather than read off a single one:
 * one kart's finish time carries the timing of its own overcommit gambles
 * (ai_random01 is deterministic, but which corner it lands on shifts with
 * anything that nudges the lap by even a few milliseconds), which is
 * enough noise on its own to blur a compound comparison. Four cars average
 * that out to the trend that is actually there.
 */
static void test_tire_strategy_crossover(void)
{
    struct { int track; float t[TIRE_COMPOUNDS]; } run[2];
    const int sample[] = { 1, 2, 3, 4 };
    int r, c, si;

    run[0].track = TRACK_CLASSIC;    /* a sprint    */
    run[1].track = TRACK_KENOSHA;    /* a long haul */

    for (r = 0; r < 2; r++) {
        for (c = 0; c < TIRE_COMPOUNDS; c++) {
            Game g;
            GameConfig cfg = default_cfg(run[r].track);
            Input in[MAX_HUMANS];
            int f, i;
            float total = 0.0f;
            int finished = 0;

            cfg.tire[0] = c;
            game_init(&g, &cfg);
            idle_inputs(in);
            for (i = 1; i < NUM_KARTS; i++)
                g.karts[i].tire = c;          /* one compound, whole field */
            for (f = 0; f < 60 * 420; f++) {
                game_update(&g, in, 1.0f / 60.0f);
                finished = 0;
                for (si = 0; si < (int)(sizeof(sample) / sizeof(sample[0]));
                     si++)
                    if (g.karts[sample[si]].finished)
                        finished++;
                if (finished == (int)(sizeof(sample) / sizeof(sample[0])))
                    break;
            }
            finished = 0;
            for (si = 0; si < (int)(sizeof(sample) / sizeof(sample[0]));
                 si++) {
                const Kart *k = &g.karts[sample[si]];
                if (k->finished) {
                    total += k->finish_time;
                    finished++;
                }
            }
            run[r].t[c] = finished ? total / (float)finished : 9999.0f;
        }
        printf("%-9s soft %.1fs  medium %.1fs  hard %.1fs\n",
               track_name(run[r].track), run[r].t[TIRE_SOFT],
               run[r].t[TIRE_MEDIUM], run[r].t[TIRE_HARD]);
    }

    CHECK(run[0].t[TIRE_SOFT] < run[0].t[TIRE_MEDIUM],
          "softs are not quicker over a sprint (%.1f vs %.1f)",
          run[0].t[TIRE_SOFT], run[0].t[TIRE_MEDIUM]);
    CHECK(run[1].t[TIRE_SOFT] > run[1].t[TIRE_MEDIUM],
          "softs still win the long race (%.1f vs %.1f) — they are the "
          "automatic choice again", run[1].t[TIRE_SOFT],
          run[1].t[TIRE_MEDIUM]);
    CHECK(run[1].t[TIRE_HARD] < run[1].t[TIRE_SOFT],
          "hards do not outlast softs over a long race (%.1f vs %.1f)",
          run[1].t[TIRE_HARD], run[1].t[TIRE_SOFT]);
}

/* Wear and heat come from work, so a parked car does neither. */
static void test_tires_need_work(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    Kart *k;
    int f;
    float temp0, wear0;

    cfg.tire[0] = TIRE_SOFT;
    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    k = &g.karts[0];
    k->speed = 0.0f;
    temp0 = k->tire_temp;
    wear0 = k->tire_wear;
    for (f = 0; f < 60 * 60; f++) {
        k->speed = 0.0f;                 /* held still, on the road */
        game_update(&g, in, 1.0f / 60.0f);
    }
    printf("parked for a minute: tires %.0fC (was %.0f), wear %.3f\n",
           k->tire_temp, temp0, k->tire_wear);
    CHECK(k->tire_wear < wear0 + 0.01f,
          "a parked car wore its tires out by %.3f", k->tire_wear - wear0);
    CHECK(k->tire_temp < temp0 + 5.0f,
          "a parked car heated its tires to %.0fC", k->tire_temp);
}


/* The passes: which are barriered, and the shape of the new ones. */
static void test_track_roster(void)
{
    Track cl, be, lo, ke, mo;
    track_init(&cl, TRACK_CLASSIC);
    track_init(&be, TRACK_BERTHOUD);
    track_init(&lo, TRACK_LOVELAND);
    track_init(&ke, TRACK_KENOSHA);
    track_init(&mo, TRACK_MONARCH);

    printf("roster: %s %.0fm w%.1f rails%d | %s %.0fm w%.1f rails%d | "
           "%s %.0fm w%.1f rails%d\n",
           lo.name, lo.total_len, lo.road_half * 2.0f, lo.has_walls,
           ke.name, ke.total_len, ke.road_half * 2.0f, ke.has_walls,
           mo.name, mo.total_len, mo.road_half * 2.0f, mo.has_walls);

    /* guardrails: gone from Loveland and Monarch, kept elsewhere */
    CHECK(cl.has_walls && be.has_walls && ke.has_walls,
          "a barriered track lost its rails");
    CHECK(!lo.has_walls, "Loveland still has guardrails");
    CHECK(!mo.has_walls, "Monarch still has guardrails");

    /* Loveland got wider and longer than it was (was 1454 m, 8.4 m wide) */
    CHECK(lo.total_len > 1600.0f, "Loveland is only %.0f m", lo.total_len);
    CHECK(lo.road_half * 2.0f > 9.0f, "Loveland is only %.1f m wide",
          lo.road_half * 2.0f);

    /* Kenosha: longer and wider than either older pass, and turnier */
    CHECK(ke.total_len > be.total_len && ke.total_len > lo.total_len,
          "Kenosha is not the longest (%.0f m)", ke.total_len);
    CHECK(ke.road_half > be.road_half && ke.road_half > lo.road_half,
          "Kenosha is not the widest");
    CHECK(ke.n_corners > be.n_corners,
          "Kenosha (%d corners) is not turnier than Berthoud (%d)",
          ke.n_corners, be.n_corners);

    /* Monarch: the hardest — narrowest, unguarded, tightest, steepest */
    {
        float mo_peak = 0.0f, be_peak = 0.0f, mo_grade = 0.0f;
        int i;
        for (i = 0; i < mo.n_corners; i++)
            if (mo.corner_peak[i] > mo_peak) mo_peak = mo.corner_peak[i];
        for (i = 0; i < be.n_corners; i++)
            if (be.corner_peak[i] > be_peak) be_peak = be.corner_peak[i];
        for (i = 0; i < mo.n; i++)
            if (fabsf(mo.slope[i]) > mo_grade) mo_grade = fabsf(mo.slope[i]);
        printf("Monarch: tightest R %.0f m, steepest %.0f%%, %d corners\n",
               1.0f / mo_peak, mo_grade * 100.0f, mo.n_corners);
        CHECK(mo.road_half < lo.road_half && mo.road_half < ke.road_half,
              "Monarch is not the narrowest");
        CHECK(mo_peak >= be_peak * 0.95f,
              "Monarch is not as tight as Berthoud");
        /* Monarch's difficulty is narrow, tight, unguarded and STEEP —
         * Berthoud climbs a little more overall, but not as sharply. */
        {
            float be_grade = 0.0f;
            for (i = 0; i < be.n; i++)
                if (fabsf(be.slope[i]) > be_grade)
                    be_grade = fabsf(be.slope[i]);
            CHECK(mo_grade > be_grade,
                  "Monarch (%.0f%%) is not steeper than Berthoud (%.0f%%)",
                  mo_grade * 100.0f, be_grade * 100.0f);
            CHECK(mo.max_y - mo.min_y > 60.0f,
                  "Monarch only climbs %.0f m", mo.max_y - mo.min_y);
        }
    }

    /* every track needs checkpoints to respawn at */
    {
        Track *all[5]; int i;
        all[0] = &cl; all[1] = &be; all[2] = &lo; all[3] = &ke; all[4] = &mo;
        for (i = 0; i < 5; i++) {
            CHECK(all[i]->n_checkpoints >= 4,
                  "%s has only %d checkpoints", all[i]->name,
                  all[i]->n_checkpoints);
            CHECK(track_checkpoint_for(all[i], 0) == 0,
                  "%s checkpoint lookup wrong at the line", all[i]->name);
            CHECK(track_checkpoint_for(all[i], all[i]->n - 1) ==
                      all[i]->n_checkpoints - 1,
                  "%s checkpoint lookup wrong at the end", all[i]->name);
        }
    }
}

/* Race length is set per circuit, so a long pass is not the same number
 * of laps as a short speedway: every race should cover a similar
 * distance. */
static void test_lap_counts(void)
{
    int id;
    for (id = 0; id < TRACK_COUNT; id++) {
        Track t;
        float dist;
        track_init(&t, id);
        dist = t.total_len * (float)t.laps;
        printf("%-9s %5.0f m x %d laps = %.0f m of racing\n",
               t.name, t.total_len, t.laps, dist);
        CHECK(t.laps >= 2 && t.laps <= 4, "%s has %d laps", t.name, t.laps);
        /* BERTHOUD 2.0 is a deliberate exception to "every race covers a
         * similar distance": it is meant to be experienced at epic
         * length, not chopped down to fit the others' band. */
        CHECK(dist > 1800.0f &&
              dist < (id == TRACK_BERTHOUD2 ? 10000.0f : 5000.0f),
              "%s race is %.0f m, out of line with the others",
              t.name, dist);
    }
}

/* Going over an unguarded edge must drop the car and then put it back on
 * the road at the last checkpoint it passed — without handing out any
 * free progress. */
static void test_cliff_respawn(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_MONARCH);
    Input in[MAX_HUMANS];
    Kart *k;
    int f, cp_before, lap_before, fell = 0;
    float prog_before, y_start;

    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    k = &g.karts[0];

    /* drive a little way down the road so a checkpoint is behind us */
    teleport(&g, k, 30, 16.0f);
    in[0].accel = 1;
    for (f = 0; f < 60; f++)
        game_update(&g, in, 1.0f / 60.0f);
    cp_before = k->last_checkpoint;
    lap_before = k->lap;
    prog_before = k->total_progress;
    CHECK(cp_before > 0, "no checkpoint recorded while driving (%d)",
          cp_before);

    /* now shove it off the side, well past the shoulder */
    teleport_off_edge(&g, k, k->seg, 12.0f);
    y_start = k->y;
    idle_inputs(in);
    for (f = 0; f < 60 * 3; f++) {
        game_update(&g, in, 1.0f / 60.0f);
        if (k->fall_t > 0.0f)
            fell = 1;
        if (k->respawned)
            break;
    }

    printf("cliff: fell %d, respawned at checkpoint %d (was %d), "
           "dropped %.1f m first\n", fell, k->last_checkpoint, cp_before,
           y_start - k->y);
    CHECK(fell, "car off an unguarded edge did not start falling");
    CHECK(k->respawned, "car never came back from over the edge");
    CHECK(fabsf(k->lat) < 1.0f, "respawned off the road (lat %.2f)",
          k->lat);
    CHECK(fabsf(k->speed) < 0.5f, "respawned still moving (%.2f m/s)",
          k->speed);
    CHECK(k->lap == lap_before, "respawn changed the lap count");
    CHECK(k->total_progress <= prog_before + 0.5f,
          "respawn handed out free progress (%.1f -> %.1f)",
          prog_before, k->total_progress);
    CHECK(k->seg == g.track.checkpoint_seg[cp_before],
          "respawned at segment %d, not checkpoint segment %d",
          k->seg, g.track.checkpoint_seg[cp_before]);
    CHECK(k->respawn_t > g.settings.respawn_fade_seconds,
          "respawn did not enter its black hold (%.2f s)", k->respawn_t);
    CHECK(k->invincible_t >= k->respawn_t + 4.9f,
          "respawn immunity is too short (%.2f s)", k->invincible_t);

    /* Neither the blackout nor the visible flashing period may let a
     * nearby rival shove the recovered car. */
    {
        Kart *rival = &g.karts[1];
        Kart rival_before = *rival;
        float safe_x = k->x, safe_z = k->z;
        rival->x = k->x + 0.2f;
        rival->z = k->z;
        rival->y = k->y;
        rival->seg = k->seg;
        rival->lat = k->lat;
        rival->speed = 0.0f;
        game_update(&g, in, 1.0f / 60.0f);
        CHECK(fabsf(k->x - safe_x) < 0.01f &&
              fabsf(k->z - safe_z) < 0.01f,
              "collision moved the car during blackout");

        while (k->respawn_t > 0.0f)
            game_update(&g, in, 1.0f / 60.0f);
        safe_x = k->x;
        safe_z = k->z;
        rival->x = k->x + 0.2f;
        rival->z = k->z;
        rival->speed = 0.0f;
        game_update(&g, in, 1.0f / 60.0f);
        CHECK(k->invincible_t > 4.8f,
              "visible invincibility expired during the fade");
        CHECK(fabsf(k->x - safe_x) < 0.01f &&
              fabsf(k->z - safe_z) < 0.01f,
              "collision moved the flashing invincible car");
        *rival = rival_before;
    }

    /* and it must be drivable again afterwards */
    in[0].accel = 1;
    for (f = 0; f < 180; f++)
        game_update(&g, in, 1.0f / 60.0f);
    CHECK(k->speed > 3.0f,
          "car is dead after respawning (%.2f m/s, fall %.2f, recover %.2f, "
          "immune %.2f, lat %.2f, seg %d)",
          k->speed, k->fall_t, k->respawn_t, k->invincible_t,
          k->lat, k->seg);
    CHECK(!isnan(k->x) && !isnan(k->y), "respawn left NaNs behind");
}

/*
 * Going over the edge must never be a shortcut. Rebuilding progress as
 * lap*n + checkpoint looked right but handed out nearly a whole lap when
 * the car left the road just before the start line: the lap counter ticks
 * over while it is in the air, and the checkpoint it comes back to is
 * still on the previous lap. Sweep the segments either side of the line.
 */
static void test_respawn_near_the_line(void)
{
    int tracks[2];
    int ti, off;
    float worst_gain = 0.0f;
    int worst_seg = -1, worst_track = -1;

    tracks[0] = TRACK_LOVELAND;   /* the two circuits with no barriers */
    tracks[1] = TRACK_MONARCH;

    for (ti = 0; ti < 2; ti++) {
        for (off = 1; off <= 12; off++) {
            Game g;
            GameConfig cfg = default_cfg(tracks[ti]);
            Input in[MAX_HUMANS];
            Kart *k;
            int seg, f, lap_before, lap_gain = 0, respawned = 0;
            float prog_before, gain = 0.0f;

            game_init(&g, &cfg);
            g.state = STATE_RACING;
            idle_inputs(in);
            k = &g.karts[0];

            /* already past the shoulder, a few segments short of the line */
            seg = g.track.n - off;
            teleport_off_edge(&g, k, seg, 28.0f);
            k->last_checkpoint = track_checkpoint_for(&g.track, seg);
            if (k->last_checkpoint < 0)
                k->last_checkpoint = 0;
            /* teleport() moves progress, so bring the lap with it — the
             * grid position it was placed from is a lap behind */
            k->lap = (int)floorf(k->total_progress / (float)g.track.n);
            lap_before = k->lap;
            prog_before = k->total_progress;

            for (f = 0; f < 60 * 5; f++) {
                float prev = k->total_progress;
                int prev_lap = k->lap;
                game_update(&g, in, 1.0f / 60.0f);
                if (k->respawned) {
                    /* what the respawn itself did, on its own frame:
                     * the slide before it is ordinary travel */
                    gain = k->total_progress - prev;
                    lap_gain = k->lap - prev_lap;
                    respawned = 1;
                    break;
                }
            }

            CHECK(respawned, "%s: car never came back from over the edge "
                  "at segment %d", track_name(tracks[ti]), seg);
            CHECK(gain < 1.0f,
                  "%s: respawn at segment %d gained %.1f segments "
                  "(%.3f laps) of free progress",
                  track_name(tracks[ti]), seg, gain,
                  gain / (float)g.track.n);
            CHECK(lap_gain <= 0,
                  "%s: the respawn at segment %d handed out %d lap(s)",
                  track_name(tracks[ti]), seg, lap_gain);
            /* the whole episode — slide plus respawn — may only move the
             * car by the ground it actually covered, nothing like a lap */
            CHECK(k->total_progress - prog_before < (float)g.track.n * 0.1f,
                  "%s: falling off at segment %d advanced the car %.1f "
                  "segments of %d (%.0f%% of a lap)",
                  track_name(tracks[ti]), seg,
                  k->total_progress - prog_before, g.track.n,
                  100.0f * (k->total_progress - prog_before)
                      / (float)g.track.n);
            CHECK(k->lap - lap_before <= 1,
                  "%s: falling off at segment %d gained %d laps",
                  track_name(tracks[ti]), seg, k->lap - lap_before);
            CHECK(!k->finished,
                  "%s: respawn at segment %d finished the race",
                  track_name(tracks[ti]), seg);
            if (gain > worst_gain) {
                worst_gain = gain;
                worst_seg = seg;
                worst_track = tracks[ti];
            }
        }
    }
    printf("respawn near the line: worst progress change %+.2f segments"
           " (%s segment %d)\n", worst_gain,
           worst_track >= 0 ? track_name(worst_track) : "-", worst_seg);
}

/* A negative checkpoint index must be clamped, not used as an index. */
static void test_respawn_bad_checkpoint(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_MONARCH);
    Input in[MAX_HUMANS];
    Kart *k;
    int f, ok = 0;

    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    k = &g.karts[0];

    teleport_off_edge(&g, k, 40, 14.0f);
    k->last_checkpoint = -3;               /* must not index backwards */

    for (f = 0; f < 60 * 4; f++) {
        game_update(&g, in, 1.0f / 60.0f);
        if (k->respawned) { ok = 1; break; }
    }
    CHECK(ok, "no respawn with a negative checkpoint index");
    CHECK(k->seg >= 0 && k->seg < g.track.n,
          "respawned onto segment %d, off the track", k->seg);
    CHECK(k->seg == g.track.checkpoint_seg[0],
          "a negative checkpoint index should clamp to the first one, "
          "landed at segment %d", k->seg);
    CHECK(!isnan(k->x) && !isnan(k->z) && !isnan(k->y),
          "respawn from a bad index left NaNs behind");
}

/* The drop has to be visible: k->y used to be overwritten with the road
 * surface on the same frame the fall wrote it, so the car slid along at
 * road height and then teleported with nothing to see. */
static void test_fall_is_visible(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_LOVELAND);
    Input in[MAX_HUMANS];
    Kart *k;
    int f;
    float max_drop = 0.0f;

    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    k = &g.karts[0];
    teleport_off_edge(&g, k, 60, 20.0f);

    for (f = 0; f < 60 * 3; f++) {
        int seg;
        float frac, lat, surface;
        game_update(&g, in, 1.0f / 60.0f);
        if (k->respawned)
            break;
        track_locate(&g.track, k->x, k->z, k->seg, &seg, &frac, &lat,
                     &surface);
        if (surface - k->y > max_drop)
            max_drop = surface - k->y;
    }
    printf("cliff drop: car fell %.1f m below the road before respawning\n",
           max_drop);
    CHECK(max_drop > 1.0f,
          "the fall is invisible: car stayed %.2f m below the road",
          max_drop);
}

/* Crossing the line must not hand the car to the AI driver, which used to
 * run it on AI fields a human kart never had: zero skill and zero corner
 * confidence read as "brake for everything", so a finished player watched
 * their car stop and then reverse back down the circuit. */
static void test_finished_human_coasts(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    Kart *k;
    int f;
    float v_flag, v_min;

    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    k = &g.karts[0];
    teleport(&g, k, 20, 26.0f);
    in[0].accel = 1;
    for (f = 0; f < 60; f++)
        game_update(&g, in, 1.0f / 60.0f);

    k->finished = 1;                 /* flag drops, throttle still down */
    k->finish_time = g.race_t;
    v_flag = k->speed;
    v_min = v_flag;
    for (f = 0; f < 60 * 6; f++) {
        game_update(&g, in, 1.0f / 60.0f);
        if (k->speed < v_min)
            v_min = k->speed;
    }
    printf("finished human: %.1f km/h at the flag, %.1f km/h six seconds "
           "later (lowest %.1f)\n", v_flag * 3.6f, k->speed * 3.6f,
           v_min * 3.6f);
    CHECK(v_min > -0.5f,
          "a finished car drove backwards (%.2f m/s)", v_min);
    CHECK(k->speed < v_flag,
          "a finished car did not slow down (%.2f -> %.2f m/s)",
          v_flag, k->speed);
    CHECK(fabsf(k->lat) < g.track.wall_half + 2.0f,
          "a finished car wandered off the road (lat %.2f)", k->lat);
}

/* One poisoned number must not freeze the race. Nothing in the Wii layer
 * is known to produce a NaN, but a single one reaching a position or a
 * heading would stick there for the rest of the session (and take the
 * chase camera with it), so the entry points reject them. */
static void test_nan_hardening(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_BERTHOUD);
    Input in[MAX_HUMANS];
    Kart *k;
    float nan_v = (float)NAN;
    float x_before, prog_before;
    int f;

    CHECK(game_clampf(nan_v, -1.0f, 1.0f) == -1.0f,
          "game_clampf let a NaN through");
    CHECK(game_clampf((float)INFINITY, -1.0f, 1.0f) == 1.0f,
          "game_clampf did not clamp infinity");
    CHECK(!isnan(game_angle_wrap(nan_v)),
          "game_angle_wrap returned a NaN");
    CHECK(fabsf(game_angle_wrap(1.0e9f)) <= 3.2f,
          "game_angle_wrap did not bound a huge angle");

    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    k = &g.karts[0];
    teleport(&g, k, 30, 18.0f);
    x_before = k->x;
    prog_before = k->total_progress;

    game_update(&g, in, nan_v);            /* a bad frame time */
    CHECK(k->x == x_before && k->total_progress == prog_before,
          "a NaN frame time was allowed to step the race");

    in[0].accel = 1;
    in[0].steer = nan_v;                   /* a bad steering axis */
    game_update(&g, in, 1.0f / 60.0f);
    idle_inputs(in);
    in[0].accel = 1;
    for (f = 0; f < 120; f++)
        game_update(&g, in, 1.0f / 60.0f);
    CHECK(!isnan(k->x) && !isnan(k->z) && !isnan(k->y) &&
          !isnan(k->heading) && !isnan(k->speed),
          "one NaN steering frame poisoned the car for good");
    printf("nan hardening: bad dt ignored, bad steer survived "
           "(x %.1f speed %.1f)\n", k->x, k->speed);
}

/* A barriered track must still hold cars in rather than dropping them. */
static void test_guardrails_still_hold(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    int f;

    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    teleport_lat(&g, &g.karts[0], 20, g.track.wall_half + 5.0f, 10.0f);
    for (f = 0; f < 60; f++)
        game_update(&g, in, 1.0f / 60.0f);
    CHECK(fabsf(g.karts[0].lat) <= g.track.wall_half + 0.1f,
          "guardrail let the car through (lat %.2f)", g.karts[0].lat);
    CHECK(g.karts[0].fall_t == 0.0f, "car fell on a barriered track");
    CHECK(!g.karts[0].respawned, "barriered track used a respawn");
}

/* AI shifting styles must differ, and none of them may hunt. */
static void test_ai_shift_styles(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_BERTHOUD);
    Input in[MAX_HUMANS];
    int shifts[NUM_KARTS], last[NUM_KARTS];
    int f, i, lo_s = 1 << 30, hi_s = -1;

    game_init(&g, &cfg);
    idle_inputs(in);
    for (i = 0; i < NUM_KARTS; i++) {
        shifts[i] = 0;
        last[i] = g.karts[i].gear;
    }
    for (f = 0; f < 60 * 90; f++) {
        game_update(&g, in, 1.0f / 60.0f);
        for (i = 1; i < NUM_KARTS; i++)
            if (g.karts[i].gear != last[i]) {
                shifts[i]++;
                last[i] = g.karts[i].gear;
            }
    }
    for (i = 1; i < NUM_KARTS; i++) {
        if (shifts[i] < lo_s) lo_s = shifts[i];
        if (shifts[i] > hi_s) hi_s = shifts[i];
        /* 90 s of racing: more than ~1.5 shifts a second is hunting */
        CHECK(shifts[i] < 135,
              "%s shifted %d times in 90 s — hunting",
              ai_strategy_name(g.karts[i].strategy), shifts[i]);
    }
    printf("AI shifting over 90 s: %d..%d changes across the field\n",
           lo_s, hi_s);
    CHECK(hi_s > lo_s + 8, "shift counts too uniform (%d..%d)", lo_s, hi_s);

    /* the strategy sheets themselves must describe different styles */
    {
        float up_lo = 9.0f, up_hi = -9.0f;
        for (i = 0; i < AI_STRATEGY_COUNT; i++) {
            if (ai_strategies[i].shift_up_frac < up_lo)
                up_lo = ai_strategies[i].shift_up_frac;
            if (ai_strategies[i].shift_up_frac > up_hi)
                up_hi = ai_strategies[i].shift_up_frac;
            CHECK(ai_strategies[i].shift_down_frac <
                      ai_strategies[i].shift_up_frac - 0.2f,
                  "%s up/down shift points are too close",
                  ai_strategies[i].name);
        }
        CHECK(up_hi - up_lo > 0.1f,
              "every strategy shifts up at the same point");
    }
}

/*
 * The compiled-in roster (`default_kart_specs`/`kart_specs` in game.c)
 * is what a release actually runs on whenever there's no SD card to
 * read cars.json from — the common case opening a DOL directly in an
 * emulator. It is kept in sync with cars.json by hand, not generated
 * from it, so nothing stops the two drifting apart one field at a
 * time: `kart_spec_count == DEFAULT_SPEC_COUNT` only proves the counts
 * match, not that a car's actual numbers do. This test loads cars.json
 * over a freshly reset default roster and diffs every field of every
 * car, so a car that plays differently depending on whether the JSON
 * was actually read fails here specifically instead of just looking
 * fine in whichever way happened to get tested. See HANDOFF.md §6.
 */
static void test_compiled_roster_matches_cars_json(void)
{
    KartSpec from_defaults[MAX_KART_SPECS];
    char error[128];
    int default_count;
    int i, g;

    kart_specs_reset_defaults();
    default_count = kart_spec_count;
    memcpy(from_defaults, kart_specs, sizeof(from_defaults));

    CHECK(config_load_cars_file("config/cars.json", error,
                                (int)sizeof(error)),
          "shipped cars.json did not load: %s", error);
    CHECK(default_count == kart_spec_count,
          "compiled roster has %d cars, cars.json has %d",
          default_count, kart_spec_count);

    for (i = 0; i < default_count && i < kart_spec_count; i++) {
        const KartSpec *d = &from_defaults[i];
        const KartSpec *j = &kart_specs[i];

        CHECK(strcmp(d->name, j->name) == 0,
              "compiled car %d is \"%s\", cars.json car %d is \"%s\"",
              i, d->name, i, j->name);
        CHECK(fabsf(d->mass_kg - j->mass_kg) < 0.01f &&
              fabsf(d->power_hp - j->power_hp) < 0.01f &&
              fabsf(d->brake_dist_100 - j->brake_dist_100) < 0.01f &&
              fabsf(d->lat_g - j->lat_g) < 0.001f &&
              fabsf(d->cd_a - j->cd_a) < 0.001f &&
              fabsf(d->wheelbase - j->wheelbase) < 0.001f &&
              fabsf(d->offroad_grip - j->offroad_grip) < 0.001f,
              "%s: compiled roster and cars.json disagree on mass/power/"
              "brake/grip/drag/wheelbase/dirt", d->name);
        CHECK(d->drivetrain == j->drivetrain &&
              fabsf(d->awd_front_bias - j->awd_front_bias) < 0.001f,
              "%s: compiled roster and cars.json disagree on drivetrain "
              "(%d vs %d) or AWD bias (%.2f vs %.2f)", d->name,
              d->drivetrain, j->drivetrain, d->awd_front_bias,
              j->awd_front_bias);
        CHECK(d->n_gears == j->n_gears,
              "%s: compiled roster has %d gears, cars.json has %d",
              d->name, d->n_gears, j->n_gears);
        for (g = 0; g < d->n_gears && g < j->n_gears; g++)
            CHECK(fabsf(d->gear_top[g] - j->gear_top[g]) < 0.01f,
                  "%s: gear %d tops out at %.2f m/s compiled, %.2f m/s "
                  "from cars.json", d->name, g, d->gear_top[g],
                  j->gear_top[g]);
    }
    kart_specs_reset_defaults();
}

static void test_json_configuration(void)
{
    GameSettings settings;
    ControlConfig controls;
    Track widened;
    char error[80];
    const char *one_car =
        "{\"cars\":[{\"name\":\"CUSTOM\",\"mass_kg\":1040,"
        "\"power_hp\":180,\"brake_distance_100_kph_m\":35,"
        "\"lateral_grip_g\":1.05,\"drag_area_m2\":0.61,"
        "\"wheelbase_m\":2.50,\"offroad_grip\":0.50,"
        "\"gear_top_speeds_kph\":[55,92,138,190,245]}]}";
    const char *partial_settings =
        "{\"_comment\":\"race\","
        "\"respawn\":{\"black_hold_seconds\":1.5},"
        "\"tracks\":{\"classic\":{\"width_multiplier\":1.2,"
        "\"laps\":7}}}";

    kart_specs_reset_defaults();
    CHECK(config_load_cars_file("config/cars.json", error,
                                (int)sizeof(error)),
          "shipped cars.json did not load: %s", error);
    CHECK(kart_spec_count == DEFAULT_SPEC_COUNT,
          "shipped car count is %d", kart_spec_count);
    CHECK(strcmp(kart_specs[1].name, "SPORT") == 0,
          "shipped SPORT car disappeared");
    CHECK(strcmp(kart_specs[4].name, "RUBY") == 0 &&
          kart_specs[4].n_gears == 6,
          "shipped RUBY car did not parse");
    CHECK(strcmp(kart_specs[5].name, "BUGGY") == 0 &&
          kart_specs[5].offroad_grip > 0.7f,
          "shipped BUGGY car did not parse");
    CHECK(strcmp(kart_specs[7].name, "FORMULA") == 0 &&
          kart_specs[7].lat_g > 1.3f,
          "shipped FORMULA car did not parse");
    CHECK(strcmp(kart_specs[8].name, "TRUCK") == 0 &&
          kart_specs[8].mass_kg > 1900.0f,
          "shipped TRUCK car did not parse");
    CHECK(strcmp(kart_specs[10].name, "MUSCLE") == 0 &&
          kart_specs[10].power_hp > 400.0f,
          "shipped MUSCLE car did not parse");

    CHECK(config_load_cars_text(one_car, error, (int)sizeof(error)),
          "custom car did not load: %s", error);
    CHECK(kart_spec_count == 1 &&
          strcmp(kart_specs[0].name, "CUSTOM") == 0,
          "custom roster was not committed");
    CHECK(kart_specs[0].n_gears == 5 &&
          fabsf(kart_specs[0].gear_top[4] - 245.0f / 3.6f) < 0.01f,
          "custom gearing was not converted from km/h");
    CHECK(!config_load_cars_text("{\"cars\":[{}]}", error,
                                 (int)sizeof(error)),
          "invalid car was accepted");
    CHECK(kart_spec_count == 1 &&
          strcmp(kart_specs[0].name, "CUSTOM") == 0,
          "invalid JSON partially replaced the valid roster");
    CHECK(!config_load_cars_text(
              "{\"cars\":[{\"name\":\"BROKEN\" \"mass_kg\":1000}]}",
              error, (int)sizeof(error)),
          "JSON with a missing comma was accepted");
    CHECK(kart_spec_count == 1 &&
          strcmp(kart_specs[0].name, "CUSTOM") == 0,
          "malformed JSON changed the valid roster");
    kart_specs_reset_defaults();

    game_settings_defaults(&settings);
    CHECK(config_load_settings_file(&settings, "config/settings.json",
                                    error, (int)sizeof(error)),
          "shipped settings.json did not load: %s", error);
    CHECK(fabsf(settings.ai_skill_mult - 1.06f) < 0.001f,
          "AI skill setting did not load");
    CHECK(fabsf(settings.weather_snow_to_ice_s - 40.0f) < 0.001f &&
          fabsf(settings.weather_puddle_grip[TIRE_HARD] - 0.92f) < 0.001f,
          "weather settings did not load");
    CHECK(config_load_settings_text(&settings, partial_settings, error,
                                    (int)sizeof(error)),
          "partial settings did not load: %s", error);
    CHECK(fabsf(settings.respawn_black_seconds - 1.5f) < 0.001f,
          "respawn setting did not change");
    CHECK(!config_load_settings_text(&settings, "{\"ai\":5}", error,
                                     (int)sizeof(error)),
          "wrong settings section type was accepted");
    track_init_with_settings(&widened, TRACK_CLASSIC, &settings);
    CHECK(fabsf(widened.road_half - 5.6f * 1.2f) < 0.01f,
          "track width multiplier was ignored (%.2f)", widened.road_half);
    CHECK(widened.laps == 7,
          "explicit track lap count was clamped to %d", widened.laps);

    control_config_defaults(&controls);
    CHECK(config_load_controls_file(&controls, "config/controls.json",
                                    error, (int)sizeof(error)),
          "shipped controls.json did not load: %s", error);
    CHECK(controls.keyboard[0][CONTROL_ACCEL][0] == 'W',
          "keyboard gas is not W");
    CHECK((controls.gamecube[CONTROL_ACCEL] & GC_INPUT_A) != 0,
          "GameCube A is not translated to gas");
    CHECK(strcmp(controls.xbox_label[CONTROL_ACCEL], "RT") == 0,
          "Xbox gas label is not RT");

    printf("json config: %d cars, width x%.1f, gas W / RT -> %s\n",
           kart_spec_count, settings.track_width_mult[TRACK_CLASSIC],
           control_gamecube_name(controls.gamecube[CONTROL_ACCEL]));
}

/* The risky AI path is deliberate but repeatable: at least one aggressive
 * driver should overcommit on unguarded Monarch, fall, and still recover
 * well enough for the field to keep racing. */
static void test_ai_can_fall(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_MONARCH);
    Input in[MAX_HUMANS];
    int f, i, falls = 0, aggressive_falls = 0;

    game_init(&g, &cfg);
    idle_inputs(in);
    for (f = 0; f < 60 * 360; f++) {
        int done = 1;
        game_update(&g, in, 1.0f / 60.0f);
        for (i = 1; i < NUM_KARTS; i++)
            if (!g.karts[i].finished) done = 0;
        if (done) break;
    }
    for (i = 1; i < NUM_KARTS; i++) {
        falls += g.karts[i].falls;
        if (g.karts[i].strategy == AI_LATE ||
            g.karts[i].strategy == AI_CHARGER ||
            g.karts[i].strategy == AI_DRAFTER ||
            g.karts[i].strategy == AI_YOLO)
            aggressive_falls += g.karts[i].falls;
    }
    printf("fallible AI: %d cliff falls, %d by aggressive strategies\n",
           falls, aggressive_falls);
    CHECK(falls > 0, "no AI ever fell off unguarded Monarch");
    CHECK(aggressive_falls > 0,
          "aggressive strategies never paid for an overcommit");
}


/*
 * The road is not one width all the way round any more. Check that the
 * profiles are actually there, that they are sane, and — the part that
 * matters — that the simulation uses the width of the piece of road the
 * car is on rather than the circuit's average.
 */
static void test_variable_road_width(void)
{
    int id;
    for (id = 0; id < TRACK_COUNT; id++) {
        Track t;
        int i, narrow_seg = 0, wide_seg = 0;
        float lo = 1.0e9f, hi = 0.0f;

        track_init(&t, id);
        for (i = 0; i < t.n; i++) {
            float w = track_road_half(&t, i);
            CHECK(w > 1.0f && w < 40.0f,
                  "%s segment %d is %.2f m of road half-width",
                  track_name(id), i, w);
            if (w < lo) { lo = w; narrow_seg = i; }
            if (w > hi) { hi = w; wide_seg = i; }
        }
        printf("%-9s width %.1f m to %.1f m (narrowest at segment %d, "
               "widest at %d)\n", track_name(id), lo * 2.0f, hi * 2.0f,
               narrow_seg, wide_seg);
        CHECK(track_road_half(&t, -1) == t.road_half &&
              track_road_half(&t, t.n) == t.road_half,
              "%s: an out-of-range segment did not fall back to the "
              "nominal width", track_name(id));
        CHECK(t.road_half >= lo - 0.001f && t.road_half <= hi + 0.001f,
              "%s: the nominal width %.2f is outside its own range",
              track_name(id), t.road_half);
    }

    /* the three passes with profiles have real variation in them */
    {
        int shaped[3];
        int j;
        shaped[0] = TRACK_BERTHOUD;
        shaped[1] = TRACK_LOVELAND;
        shaped[2] = TRACK_MONARCH;
        for (j = 0; j < 3; j++) {
            Track t;
            int i;
            float lo = 1.0e9f, hi = 0.0f;
            track_init(&t, shaped[j]);
            for (i = 0; i < t.n; i++) {
                float w = track_road_half(&t, i);
                if (w < lo) lo = w;
                if (w > hi) hi = w;
            }
            CHECK(hi / lo > 1.30f,
                  "%s barely varies in width (%.2f to %.2f)",
                  track_name(shaped[j]), lo * 2.0f, hi * 2.0f);
        }
    }
}

/*
 * A narrow section has to be narrow in the physics too: park a car at an
 * offset that is on the road at the circuit's average width, and on the
 * narrowest part of the same circuit it must be off it.
 */
static void test_narrow_sections_bite(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_BERTHOUD);
    Input in[MAX_HUMANS];
    Kart *k;
    int i, narrow = 0, wide = 0, f;
    float lo = 1.0e9f, hi = 0.0f, offset;
    float speed_narrow, speed_wide;

    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    k = &g.karts[0];

    for (i = 0; i < g.track.n; i++) {
        float w = track_road_half(&g.track, i);
        if (w < lo) { lo = w; narrow = i; }
        if (w > hi) { hi = w; wide = i; }
    }
    /* just off the narrow road, comfortably on the wide one */
    offset = (lo + hi) * 0.5f;

    teleport_lat(&g, k, wide, offset, 12.0f);
    in[0].accel = 1;
    for (f = 0; f < 60; f++)
        game_update(&g, in, 1.0f / 60.0f);
    speed_wide = k->speed;

    game_init(&g, &cfg);
    g.state = STATE_RACING;
    k = &g.karts[0];
    teleport_lat(&g, k, narrow, offset, 12.0f);
    for (f = 0; f < 60; f++)
        game_update(&g, in, 1.0f / 60.0f);
    speed_narrow = k->speed;

    printf("width bites: %.1f m off center is %.1f km/h on the wide part, "
           "%.1f km/h on the narrow one\n", offset,
           speed_wide * 3.6f, speed_narrow * 3.6f);
    CHECK(speed_wide > speed_narrow + 1.0f,
          "the narrow section drove the same as the wide one "
          "(%.1f vs %.1f km/h)", speed_wide * 3.6f, speed_narrow * 3.6f);
}

/*
 * The two circuits added in v1.6.0, checked against the brief they were
 * written to rather than against whatever they happen to be.
 */
static void test_new_passes(void)
{
    Track bn, gu, be, mo;
    int i;
    float bn_grade = 0.0f, mo_grade = 0.0f, gu_peak = 0.0f, be_peak = 0.0f;
    int gu_hairpins = 0;

    track_init(&bn, TRACK_BREAKNECK);
    track_init(&gu, TRACK_GUANELLA);
    track_init(&be, TRACK_BERTHOUD);
    track_init(&mo, TRACK_MONARCH);

    for (i = 0; i < bn.n; i++)
        if (fabsf(bn.slope[i]) > bn_grade) bn_grade = fabsf(bn.slope[i]);
    for (i = 0; i < mo.n; i++)
        if (fabsf(mo.slope[i]) > mo_grade) mo_grade = fabsf(mo.slope[i]);
    for (i = 0; i < gu.n_corners; i++) {
        if (gu.corner_peak[i] > gu_peak) gu_peak = gu.corner_peak[i];
        if (1.0f / gu.corner_peak[i] < 12.0f) gu_hairpins++;
    }
    for (i = 0; i < be.n_corners; i++)
        if (be.corner_peak[i] > be_peak) be_peak = be.corner_peak[i];

    printf("BREAKNECK %.0f m, %.0f m of climb, %.0f%% steepest, %.1f m wide, "
           "rails %d\n", bn.total_len, bn.max_y - bn.min_y,
           bn_grade * 100.0f, bn.road_half * 2.0f, bn.has_walls);
    printf("GUANELLA  %.0f m, %d corners of which %d are hairpins "
           "(tightest R %.0f m), %.1f m wide, rails %d\n", gu.total_len,
           gu.n_corners, gu_hairpins, 1.0f / gu_peak, gu.road_half * 2.0f,
           gu.has_walls);

    /* Breakneck: short, steep, abrupt, narrow, no barriers */
    CHECK(bn.total_len < be.total_len,
          "Breakneck (%.0f m) is not shorter than Berthoud (%.0f m)",
          bn.total_len, be.total_len);
    CHECK(!bn.has_walls, "Breakneck has guardrails");
    CHECK(bn.max_y - bn.min_y > 35.0f,
          "Breakneck only climbs %.0f m", bn.max_y - bn.min_y);
    CHECK(bn.road_half * 2.0f < 8.5f,
          "Breakneck is %.1f m wide, which is not narrow",
          bn.road_half * 2.0f);
    /* the elevation is meant to arrive in steps, not one smooth arc:
     * count how often the grade changes sharply along the lap */
    {
        int steps = 0;
        for (i = 0; i < bn.n; i++) {
            int j = (i + 1) % bn.n;
            if (fabsf(bn.slope[j] - bn.slope[i]) > 0.010f)
                steps++;
        }
        printf("BREAKNECK grade changes sharply at %d of %d samples\n",
               steps, bn.n);
        CHECK(steps > 8, "Breakneck's climb is one smooth arc (%d steps)",
              steps);
    }

    /* Guanella: medium length, switchback after switchback, narrow, bare */
    CHECK(gu.total_len > bn.total_len && gu.total_len < 2400.0f,
          "Guanella is %.0f m, which is not medium", gu.total_len);
    CHECK(!gu.has_walls, "Guanella has guardrails");
    CHECK(gu_hairpins >= 5,
          "Guanella has only %d hairpins for a switchback pass",
          gu_hairpins);
    CHECK(gu_peak > be_peak,
          "Guanella's tightest corner (R %.0f) is no tighter than "
          "Berthoud's (R %.0f)", 1.0f / gu_peak, 1.0f / be_peak);
    CHECK(gu.road_half * 2.0f < 9.0f,
          "Guanella is %.1f m wide, which is not narrow",
          gu.road_half * 2.0f);
    CHECK(gu.max_y - gu.min_y < mo.max_y - mo.min_y,
          "Guanella climbs %.0f m, more than Monarch's %.0f — it is "
          "supposed to be the modest one", gu.max_y - gu.min_y,
          mo.max_y - mo.min_y);
}

/*
 * Hills. The gravity component along a road is g*sin(theta) and the load
 * on the tires is m*g*cos(theta): a climb should cost real speed, a
 * descent should give it back, and neither should be a rounding error.
 */
static void test_grade_costs_speed(void)
{
    struct { float slope; float reached; } run[3];
    int i;

    for (i = 0; i < 3; i++) {
        Game g;
        GameConfig cfg = default_cfg(TRACK_CLASSIC);
        Input in[MAX_HUMANS];
        Kart *k;
        int f, j, straight = 0;
        float flattest = 1.0e9f;

        game_init(&g, &cfg);
        g.state = STATE_RACING;
        idle_inputs(in);
        in[0].accel = 1;
        k = &g.karts[0];

        /* CLASSIC is flat, so the grade is imposed directly: -20%, 0, +20% */
        run[i].slope = (float)(i - 1) * 0.20f;
        for (j = 0; j < g.track.n; j++)
            g.track.slope[j] = run[i].slope;

        /* start on the straightest piece of road so the run is about the
         * hill rather than about steering off the outside of a corner */
        for (j = 0; j < g.track.n; j++)
            if (g.track.curv[j] < flattest) { flattest = g.track.curv[j];
                                              straight = j; }
        teleport(&g, k, straight, 12.0f);
        for (f = 0; f < 60 * 3; f++)
            game_update(&g, in, 1.0f / 60.0f);
        run[i].reached = k->speed;
    }

    printf("grade: %.1f km/h down a 20%% drop, %.1f km/h on the flat, "
           "%.1f km/h up a 20%% climb (three seconds from 43 km/h)\n",
           run[0].reached * 3.6f, run[1].reached * 3.6f,
           run[2].reached * 3.6f);
    CHECK(run[0].reached > run[1].reached + 1.0f,
          "a 20%% descent gave nothing back (%.1f vs %.1f m/s)",
          run[0].reached, run[1].reached);
    CHECK(run[1].reached > run[2].reached + 2.0f,
          "a 20%% climb cost almost nothing (%.1f vs %.1f m/s)",
          run[1].reached, run[2].reached);
    /* g*sin(atan(0.2)) is 1.92 m/s^2, so three seconds of climbing rather
     * than descending has to be worth several m/s either way */
    CHECK(run[0].reached - run[2].reached > 6.0f,
          "descending and climbing differed by only %.1f m/s",
          run[0].reached - run[2].reached);
}

/* Steep ground also takes weight off the tires, so grip goes with it. */
static void test_grade_costs_grip(void)
{
    float flat_stop, steep_stop;
    int i;

    for (i = 0; i < 2; i++) {
        Game g;
        GameConfig cfg = default_cfg(TRACK_CLASSIC);
        Input in[MAX_HUMANS];
        Kart *k;
        int f, j;
        float travelled;

        game_init(&g, &cfg);
        g.state = STATE_RACING;
        idle_inputs(in);
        k = &g.karts[0];
        for (j = 0; j < g.track.n; j++)
            g.track.slope[j] = (i == 0) ? 0.0f : 0.28f;
        {
            int b = 0; float flattest = 1.0e9f;
            for (j = 0; j < g.track.n; j++)
                if (g.track.curv[j] < flattest) { flattest = g.track.curv[j];
                                                  b = j; }
            teleport(&g, k, b, 26.0f);
        }
        travelled = k->total_progress;

        in[0].brake = 1;
        for (f = 0; f < 60 * 8 && k->speed > 1.0f; f++)
            game_update(&g, in, 1.0f / 60.0f);
        travelled = (k->total_progress - travelled) *
                    (g.track.total_len / (float)g.track.n);
        if (i == 0) flat_stop = travelled; else steep_stop = travelled;
    }
    printf("braking from 26 m/s: %.1f m on the flat, %.1f m on a 28%% "
           "climb\n", flat_stop, steep_stop);
    /* uphill the slope helps stop the car, but the tires have less to
     * work with; the point is only that the load term is doing something */
    CHECK(steep_stop < flat_stop,
          "an uphill stop (%.1f m) was not shorter than a flat one (%.1f m)",
          steep_stop, flat_stop);
}

/* A car can bring its own automatic shift points, per car or per gear. */
static void test_car_shift_points(void)
{
    char error[128];
    const char *early =
        "{\"cars\":[{\"name\":\"SHORTY\",\"mass_kg\":900,"
        "\"power_hp\":150,\"brake_distance_100_kph_m\":38,"
        "\"lateral_grip_g\":0.95,\"drag_area_m2\":0.66,"
        "\"wheelbase_m\":2.4,\"offroad_grip\":0.45,"
        "\"gear_top_speeds_kph\":[47,79,119,162],"
        "\"automatic_upshift_fraction\":0.62,"
        "\"automatic_downshift_fraction\":0.30}]}";
    const char *per_gear =
        "{\"cars\":[{\"name\":\"STAGED\",\"mass_kg\":900,"
        "\"power_hp\":150,\"brake_distance_100_kph_m\":38,"
        "\"lateral_grip_g\":0.95,\"drag_area_m2\":0.66,"
        "\"wheelbase_m\":2.4,\"offroad_grip\":0.45,"
        "\"gear_top_speeds_kph\":[47,79,119,162],"
        "\"automatic_upshift_per_gear\":[0.70,0.85,0.95,0.99]}]}";
    const char *hunting =
        "{\"cars\":[{\"name\":\"HUNTER\",\"mass_kg\":900,"
        "\"power_hp\":150,\"brake_distance_100_kph_m\":38,"
        "\"lateral_grip_g\":0.95,\"drag_area_m2\":0.66,"
        "\"wheelbase_m\":2.4,\"offroad_grip\":0.45,"
        "\"gear_top_speeds_kph\":[47,79,119,162],"
        "\"automatic_upshift_fraction\":0.50,"
        "\"automatic_downshift_fraction\":0.45}]}";
    int shifts_default, shifts_early;
    int variant;

    /* the shipped cars keep the standard points */
    kart_specs_reset_defaults();
    CHECK(fabsf(kart_specs[1].auto_up[0] - AUTO_UP_FRAC) < 0.001f,
          "a built-in car lost its default upshift point");

    /* a car that shifts at 62% of each gear must use more gears sooner */
    for (variant = 0; variant < 2; variant++) {
        Game g;
        GameConfig cfg;
        Input in[MAX_HUMANS];
        int f, changes = 0, last;

        kart_specs_reset_defaults();
        if (variant == 1)
            CHECK(config_load_cars_text(early, error, (int)sizeof(error)),
                  "an early-shifting car was rejected: %s", error);
        cfg = default_cfg(TRACK_CLASSIC);
        cfg.spec[0] = variant == 1 ? 0 : 1;
        game_init(&g, &cfg);
        g.state = STATE_RACING;
        idle_inputs(in);
        in[0].accel = 1;
        last = g.karts[0].gear;
        for (f = 0; f < 60 * 20; f++) {
            game_update(&g, in, 1.0f / 60.0f);
            if (g.karts[0].gear != last) { changes++; last = g.karts[0].gear; }
        }
        if (variant == 0) shifts_default = changes; else shifts_early = changes;
    }
    printf("shift points: %d changes in 20 s with the standard box, "
           "%d with one set to 62%%\n", shifts_default, shifts_early);
    CHECK(shifts_early >= shifts_default,
          "an early-shifting car changed gear less often (%d vs %d)",
          shifts_early, shifts_default);

    /* per-gear points load and land where they were put */
    kart_specs_reset_defaults();
    CHECK(config_load_cars_text(per_gear, error, (int)sizeof(error)),
          "per-gear shift points were rejected: %s", error);
    CHECK(fabsf(kart_specs[0].auto_up[0] - 0.70f) < 0.001f &&
          fabsf(kart_specs[0].auto_up[3] - 0.99f) < 0.001f,
          "per-gear shift points did not land (%.2f, %.2f)",
          kart_specs[0].auto_up[0], kart_specs[0].auto_up[3]);
    CHECK(fabsf(kart_specs[0].auto_down[0] - AUTO_DOWN_FRAC) < 0.001f,
          "setting upshifts clobbered the downshifts");

    /* points close enough to hunt are refused, and nothing is applied */
    kart_specs_reset_defaults();
    CHECK(!config_load_cars_text(hunting, error, (int)sizeof(error)),
          "a gearbox that would hunt was accepted");
    CHECK(kart_spec_count == DEFAULT_SPEC_COUNT,
          "a rejected car file was applied anyway");
}

/*
 * The whole point of cars.json is that editing a number in the file
 * changes the car in the garage. Walk that path end to end — read the
 * shipped file, change one value, write it somewhere else, load it as the
 * game does, and check the roster — because "I am not sure the JSON
 * works" deserves a test rather than an opinion.
 */
static void test_editing_cars_json_changes_the_car(void)
{
    char error[128];
    char *text;
    long len;
    FILE *f;
    const char *tmp = "wiikart-test-cars.json";
    char *edited;
    const char *needle = "\"power_hp\": 150";
    char *at;
    int i, sport = -1;

    /* the file as shipped */
    kart_specs_reset_defaults();
    CHECK(config_load_cars_file("config/cars.json", error,
                                (int)sizeof(error)),
          "the shipped cars.json did not load: %s", error);
    for (i = 0; i < kart_spec_count; i++)
        if (strcmp(kart_specs[i].name, "SPORT") == 0) sport = i;
    CHECK(sport >= 0, "the shipped roster has no SPORT");
    if (sport < 0) return;
    CHECK(fabsf(kart_specs[sport].power_hp - 150.0f) < 0.5f,
          "SPORT starts at %.0f hp, not the 150 the file says",
          kart_specs[sport].power_hp);

    f = fopen("config/cars.json", "rb");
    CHECK(f != NULL, "could not reopen the shipped cars.json");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    text = (char *)malloc((size_t)len + 1);
    edited = (char *)malloc((size_t)len + 32);
    if (!text || !edited) { fclose(f); free(text); free(edited); return; }
    len = (long)fread(text, 1, (size_t)len, f);
    text[len] = '\0';
    fclose(f);

    /* the edit a player would make: give SPORT more power */
    at = strstr(text, needle);
    CHECK(at != NULL, "cars.json no longer contains %s", needle);
    if (!at) { free(text); free(edited); return; }
    {
        long head = (long)(at - text);
        memcpy(edited, text, (size_t)head);
        edited[head] = '\0';
        strcat(edited, "\"power_hp\": 210");
        strcat(edited, at + strlen(needle));
    }

    f = fopen(tmp, "wb");
    CHECK(f != NULL, "could not write a test copy of cars.json");
    if (f) {
        fwrite(edited, 1, strlen(edited), f);
        fclose(f);

        kart_specs_reset_defaults();
        CHECK(config_load_cars_file(tmp, error, (int)sizeof(error)),
              "the edited cars.json was rejected: %s", error);
        sport = -1;
        for (i = 0; i < kart_spec_count; i++)
            if (strcmp(kart_specs[i].name, "SPORT") == 0) sport = i;
        CHECK(sport >= 0 &&
              fabsf(kart_specs[sport].power_hp - 210.0f) < 0.5f,
              "editing the file did not change the car (%.0f hp)",
              sport >= 0 ? kart_specs[sport].power_hp : -1.0f);
        printf("cars.json edit: SPORT 150 hp -> %.0f hp in the garage\n",
               sport >= 0 ? kart_specs[sport].power_hp : -1.0f);
        remove(tmp);
    }

    /* a file that is not there must fail cleanly and change nothing */
    kart_specs_reset_defaults();
    CHECK(!config_load_cars_file("config/does-not-exist.json", error,
                                 (int)sizeof(error)),
          "loading a missing file reported success");
    CHECK(kart_spec_count == DEFAULT_SPEC_COUNT,
          "a missing file disturbed the roster");

    free(text);
    free(edited);
    kart_specs_reset_defaults();
}

/*
 * The slowing-down lap. Crossing the line takes the car away from the
 * player, so a stand-in driver has to bring it home: down the road, over
 * to the side, to a stop — without going over the edge of an unguarded
 * pass, and without selecting reverse and driving back into the field.
 */
static void test_cooldown_driver_brings_the_car_home(void)
{
    int ti;
    int tracks[3];

    tracks[0] = TRACK_MONARCH;    /* unguarded and twisty  */
    tracks[1] = TRACK_GUANELLA;   /* unguarded switchbacks */
    tracks[2] = TRACK_BERTHOUD;   /* barriered, for contrast */

    for (ti = 0; ti < 3; ti++) {
        Game g;
        GameConfig cfg = default_cfg(tracks[ti]);
        Input in[MAX_HUMANS];
        Kart *k;
        int f, off_road_frames = 0, reversed = 0;
        float v_flag, fastest_after = 0.0f, distance;
        float start_progress;

        game_init(&g, &cfg);
        g.state = STATE_RACING;
        idle_inputs(in);
        k = &g.karts[0];

        /* on the road at racing speed, and the flag drops. No warm-up
         * lap of full throttle and no steering first: that just puts the
         * car in the barrier and measures the crash instead of the
         * stand-in driver. */
        teleport(&g, k, 12, 26.0f);
        k->finished = 1;
        k->finish_time = g.race_t;
        k->cooldown_t = 0.0f;
        k->cooldown_v0 = fabsf(k->speed);
        v_flag = k->speed;
        start_progress = k->total_progress;

        for (f = 0; f < 60 * 30; f++) {
            game_update(&g, in, 1.0f / 60.0f);
            if (k->speed > fastest_after) fastest_after = k->speed;
            if (k->speed < -0.5f) reversed = 1;
            if (fabsf(k->lat) > track_wall_half(&g.track, k->seg) + 0.5f)
                off_road_frames++;
        }
        distance = (k->total_progress - start_progress) *
                   (g.track.total_len / (float)g.track.n);

        printf("%-9s after the flag: %.0f km/h at the line, %.1f km/h 30 s "
               "later, %.0f m driven, %d falls, %d frames off the road\n",
               track_name(tracks[ti]), v_flag * 3.6f, k->speed * 3.6f,
               distance, k->falls, off_road_frames);

        CHECK(k->falls == 0, "%s: the finished car fell off %d time(s)",
              track_name(tracks[ti]), k->falls);
        CHECK(!reversed, "%s: the finished car drove backwards",
              track_name(tracks[ti]));
        CHECK(fabsf(k->speed) < 1.5f,
              "%s: the finished car was still doing %.1f km/h after 30 s",
              track_name(tracks[ti]), k->speed * 3.6f);
        CHECK(fastest_after <= v_flag + 1.0f,
              "%s: the finished car sped up after the flag (%.1f from %.1f)",
              track_name(tracks[ti]), fastest_after, v_flag);
        CHECK(distance > 30.0f,
              "%s: the finished car only travelled %.0f m — it is supposed "
              "to be driven home, not dropped", track_name(tracks[ti]),
              distance);
        CHECK(off_road_frames < 60,
              "%s: the finished car spent %d frames off the road",
              track_name(tracks[ti]), off_road_frames);
    }
}

/*
 * And it has to keep doing that while the rest of the field is still
 * racing past it: a whole race, everyone finishing, nobody lost.
 */
static void test_whole_field_survives_the_flag(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_MONARCH);
    Input in[MAX_HUMANS];
    int f, i, falls_after_flag = 0;

    game_init(&g, &cfg);
    idle_inputs(in);
    for (f = 0; f < 60 * 500; f++) {
        game_update(&g, in, 1.0f / 60.0f);
        for (i = 0; i < NUM_KARTS; i++)
            if (g.karts[i].finished && g.karts[i].fall_t > 0.0f)
                falls_after_flag++;
        if (g.finish_count >= NUM_KARTS - 1)
            break;
    }
    printf("after the flag on MONARCH: %d frames of finished cars falling\n",
           falls_after_flag);
    CHECK(falls_after_flag == 0,
          "finished cars went over the edge for %d frames", falls_after_flag);
}

/* ------------------------------------------------------------------ */
/* The wrong-way marshal                                               */
/* ------------------------------------------------------------------ */

/*
 * Turn the human around on CLASSIC and hold the throttle down. The
 * marshal should not be instant (a three-point turn at a hairpin should
 * not trip it), but it should show up well before it would let a driver
 * complete a lap backward, and once it has, the car should be nowhere
 * near as fast as one that was never caught.
 */
static void test_wrong_way_marshal_cuts_power(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    Kart *k;
    float speed_hist[60 * 6];
    const int n = 60 * 6;
    int f, flag_frame = -1;
    float accel_before, accel_after;
    const float pi = 3.14159265358979f;

    game_init(&g, &cfg);
    idle_inputs(in);
    in[0].accel = 1;

    k = &g.karts[0];
    teleport(&g, k, 5, 0.0f);
    k->heading = game_angle_wrap(k->heading + pi);
    g.state = STATE_RACING;                 /* skip countdown */

    CHECK(!k->wrong_way, "flagged before it had even moved");

    for (f = 0; f < n; f++) {
        game_update(&g, in, 1.0f / 60.0f);
        speed_hist[f] = fabsf(k->speed);
        if (flag_frame < 0 && k->wrong_way)
            flag_frame = f;
    }

    /* not instant — a three-point turn at a hairpin should not trip it —
     * but well before it would let a driver complete a lap backward, and
     * with a full second either side still inside this run to compare */
    CHECK(flag_frame >= 60 && flag_frame <= n - 60 - 1,
          "marshal arrived at frame %d, out of the range this test can "
          "measure (wanted 60..%d)", flag_frame, n - 60 - 1);

    accel_before = speed_hist[flag_frame] - speed_hist[flag_frame - 60];
    accel_after  = speed_hist[flag_frame + 60] - speed_hist[flag_frame];
    printf("wrong-way marshal on CLASSIC: flagged at %.2fs (%.1f km/h), "
           "gained %.1f km/h in the second before, %.1f km/h in the second "
           "after\n", flag_frame / 60.0f, speed_hist[flag_frame] * 3.6f,
           accel_before * 3.6f, accel_after * 3.6f);

    CHECK(accel_after < accel_before * 0.6f,
          "gained %.1f km/h the second before being caught and %.1f km/h "
          "the second after — not much of a power cut",
          accel_before * 3.6f, accel_after * 3.6f);
}

/*
 * The AI's own reverse-out recovery must never trip this: point an AI car
 * backward exactly the same way and confirm it is never flagged, however
 * long the net progress stays negative.
 */
static void test_wrong_way_marshal_leaves_the_ai_alone(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    int f;
    const float pi = 3.14159265358979f;
    Kart *ai;

    game_init(&g, &cfg);
    idle_inputs(in);
    ai = &g.karts[1];
    CHECK(ai->human < 0, "test bug: grid slot 1 is not an AI car");
    teleport(&g, ai, 5, 0.0f);
    ai->heading = game_angle_wrap(ai->heading + pi);
    ai->speed = -15.0f;   /* net moving backward, whatever the AI itself wants */
    g.state = STATE_RACING;                 /* skip countdown */

    for (f = 0; f < 60 * 6; f++) {
        game_update(&g, in, 1.0f / 60.0f);
        CHECK(!ai->wrong_way, "an AI car got flagged wrong-way at frame %d", f);
    }
}

/* ------------------------------------------------------------------ */
/* Driver identity                                                     */
/* ------------------------------------------------------------------ */

/*
 * The field is meant to contain people, not eleven copies of "quite
 * good": someone elite and willing, someone quick but wild, someone
 * careful and slow, someone dependable. Check the roster really has that
 * shape, and that a driver is the same person every race.
 */
static void test_driver_field_has_characters(void)
{
    int n = ai_driver_count();
    int i;
    int elite_aggressive = 0, wild = 0, passive = 0, dependable = 0;
    float skill_lo = 9.0f, skill_hi = 0.0f;

    CHECK(n >= NUM_KARTS - 1, "only %d drivers for %d grid slots", n,
          NUM_KARTS - 1);

    printf("field:\n");
    for (i = 0; i < n; i++) {
        const AIDriver *d = ai_driver(i);
        printf("  %-8s %-9s skill %.2f  consistency %.2f  aggression %.2f  "
               "tires %.2f  %s\n", d->name, ai_strategy_name(d->strategy),
               d->skill, d->consistency, d->aggression, d->tire_care,
               d->trait);
        if (d->skill < skill_lo) skill_lo = d->skill;
        if (d->skill > skill_hi) skill_hi = d->skill;

        if (d->skill >= 1.02f && d->aggression >= 0.70f) elite_aggressive++;
        if (d->consistency <= 0.60f && d->aggression >= 0.80f) wild++;
        if (d->aggression <= 0.25f) passive++;
        if (d->consistency >= 0.90f && d->aggression >= 0.40f &&
            d->aggression <= 0.70f) dependable++;
    }
    printf("field spread: skill %.2f to %.2f\n", skill_lo, skill_hi);

    CHECK(skill_hi - skill_lo >= 0.15f,
          "the whole field is within %.2f of the same skill", skill_hi - skill_lo);
    CHECK(elite_aggressive >= 1, "nobody is both quick and willing");
    CHECK(wild >= 2, "only %d driver(s) overdrive the car", wild);
    CHECK(passive >= 2, "only %d driver(s) are content to follow", passive);
    CHECK(dependable >= 2, "only %d dependable driver(s)", dependable);

    /* the same rival, race after race */
    {
        Game a, b;
        GameConfig cfg = default_cfg(TRACK_BERTHOUD);
        int k;
        game_init(&a, &cfg);
        cfg.track_id = TRACK_MONARCH;
        game_init(&b, &cfg);
        for (k = 1; k < NUM_KARTS; k++) {
            CHECK(strcmp(ai_driver_name(a.karts[k].driver_no),
                         ai_driver_name(b.karts[k].driver_no)) == 0,
                  "grid slot %d was %s and then %s", k,
                  ai_driver_name(a.karts[k].driver_no),
                  ai_driver_name(b.karts[k].driver_no));
            CHECK(fabsf(a.karts[k].ai_skill - b.karts[k].ai_skill) < 0.001f &&
                  a.karts[k].strategy == b.karts[k].strategy,
                  "%s changed between races",
                  ai_driver_name(a.karts[k].driver_no));
            CHECK(a.karts[k].paint_idx == b.karts[k].paint_idx,
                  "%s changed colour between races",
                  ai_driver_name(a.karts[k].driver_no));
        }
    }
}

/*
 * Temperament has to show up on track, or it is just a table. The wild
 * drivers should gamble on corners far more often than the steady ones,
 * and the ones who are kind to their tires should be on softer rubber.
 */
static void test_temperament_shows_on_track(void)
{
    Game g;
    /* an unguarded circuit: gambling on a corner only means anything
     * where the penalty for getting it wrong is the mountainside */
    GameConfig cfg = default_cfg(TRACK_MONARCH);
    Input in[MAX_HUMANS];
    int f, i;
    int bold_risk = 0, steady_risk = 0, bold_n = 0, steady_n = 0;

    game_init(&g, &cfg);
    idle_inputs(in);
    for (f = 0; f < 60 * 300 && g.finish_count < NUM_KARTS - 1; f++)
        game_update(&g, in, 1.0f / 60.0f);

    for (i = 1; i < NUM_KARTS; i++) {
        const Kart *k = &g.karts[i];
        const AIDriver *d = ai_driver(k->driver_no);
        float wildness = d->aggression * (1.0f - d->consistency);

        if (wildness > 0.30f)      { bold_risk += k->overcommits; bold_n++; }
        else if (wildness < 0.10f) { steady_risk += k->overcommits; steady_n++; }

        /* the compound follows the hands: hard on rubber, harder tire */
        if (d->tire_care > 1.10f)
            CHECK(k->tire == TIRE_HARD, "%s is hard on tires but took %s",
                  d->name, tire_name(k->tire));
        if (d->tire_care < 0.85f)
            CHECK(k->tire == TIRE_SOFT, "%s is gentle but took %s",
                  d->name, tire_name(k->tire));
    }

    printf("risk taking: %d gambles from %d wild drivers, %d from %d steady "
           "ones\n", bold_risk, bold_n, steady_risk, steady_n);
    CHECK(bold_n > 0 && steady_n > 0, "the field has no spread of nerve");
    CHECK(bold_risk > steady_risk,
          "the wild drivers (%d gambles) were no braver than the steady "
          "ones (%d)", bold_risk, steady_risk);
}

/*
 * Skill has to be worth something on its own. Give the same driver
 * profile two different skill values on the same circuit and the quicker
 * one has to set the quicker lap — this is the property that stops the
 * strategy sheet being the only thing that decides pace.
 */
static void test_skill_sets_pace(void)
{
    float lap[2];
    int v;

    for (v = 0; v < 2; v++) {
        Game g;
        GameConfig cfg = default_cfg(TRACK_BERTHOUD);
        Input in[MAX_HUMANS];
        Kart *k;
        int f;

        game_init(&g, &cfg);
        idle_inputs(in);
        k = &g.karts[1];
        /* same person, same sheet, different skill */
        k->ai_skill = (v == 0) ? 0.86f : 1.06f;
        for (f = 0; f < 60 * 300 && !k->finished; f++)
            game_update(&g, in, 1.0f / 60.0f);
        lap[v] = k->best_lap_time > 0.0f ? k->best_lap_time : 9999.0f;
    }
    printf("skill and pace: %.1f s a lap at 0.86 skill, %.1f s at 1.06\n",
           lap[0], lap[1]);
    CHECK(lap[1] < lap[0],
          "the more skilled driver was not quicker (%.1f vs %.1f s)",
          lap[1], lap[0]);
}

/*
 * The racing line used to be a fixed lateral offset held for the whole
 * lap, so a sheet came out quicker or slower depending on which way a
 * particular circuit's corners happened to bend — nothing to do with the
 * driver holding the wheel. Give two drivers the same skill and the same
 * learned corner confidence, differing only in which sheet (and so which
 * apex commitment, ai_tactical_line) they drive solo round the same
 * circuit, and their best laps should land close together. INSIDE and
 * CRUISER are the two ends of the commitment range, so they are the
 * sharpest version of this test.
 */
static void test_racing_line_pace_is_not_the_sheet(void)
{
    float lap[2];
    const int strategies[2] = { AI_INSIDE, AI_CRUISER };
    int v;

    for (v = 0; v < 2; v++) {
        Game g;
        GameConfig cfg = default_cfg(TRACK_BERTHOUD);
        Input in[MAX_HUMANS];
        Kart *k;
        int f, c;

        game_init(&g, &cfg);
        idle_inputs(in);
        k = &g.karts[1];
        k->ai_skill = 1.0f;
        k->strategy = strategies[v];
        for (c = 0; c < TRACK_MAX_CORNERS; c++)
            k->corner_conf[c] = 1.0f;      /* equal nerve, isolate the line */
        for (f = 0; f < 60 * 300 && !k->finished; f++)
            game_update(&g, in, 1.0f / 60.0f);
        lap[v] = k->best_lap_time > 0.0f ? k->best_lap_time : 9999.0f;
    }

    printf("same skill, INSIDE vs CRUISER on BERTHOUD: %.1f s vs %.1f s "
           "(%.1f%% apart)\n", lap[0], lap[1],
           100.0f * fabsf(lap[0] - lap[1]) / lap[0]);
    CHECK(fabsf(lap[0] - lap[1]) < 0.03f * lap[0],
          "%.1f s vs %.1f s — the sheet is still deciding pace, not skill",
          lap[0], lap[1]);
}

/* ------------------------------------------------------------------ */
/* Lap timing                                                          */
/* ------------------------------------------------------------------ */

/*
 * Every driver keeps their own stopwatch. Run a full race and check the
 * times against the one number we already trust — the finishing time.
 */
static void test_lap_times(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    int f, i, lap_events = 0, best_events = 0;
    float first_lap_time = 0.0f;

    game_init(&g, &cfg);
    idle_inputs(in);
    in[0].accel = 1;

    for (f = 0; f < 60 * 400; f++) {
        game_update(&g, in, 1.0f / 60.0f);
        for (i = 0; i < NUM_KARTS; i++) {
            if (g.karts[i].lap_event) {
                lap_events++;
                if (i == 1 && first_lap_time == 0.0f && f > 60)
                    first_lap_time = g.karts[i].last_lap_time;
            }
            if (g.karts[i].lap_best_event)
                best_events++;
        }
        if (g.finish_count >= NUM_KARTS)
            break;
    }

    /* the human kart is holding the throttle flat with no steering, so
     * it is not expected to finish; the AI field is */
    CHECK(g.finish_count >= NUM_KARTS - 1,
          "only %d of %d karts finished in 400 s", g.finish_count,
          NUM_KARTS);
    CHECK(lap_events > 0, "no lap was ever recorded");
    printf("lap timing: %d laps recorded, %d of them personal bests, "
           "first AI lap %.2f s\n", lap_events, best_events, first_lap_time);

    for (i = 0; i < NUM_KARTS; i++) {
        const Kart *k = &g.karts[i];
        if (!k->finished)
            continue;
        CHECK(k->laps_done >= g.track.laps - 1,
              "kart %d finished with only %d timed laps of %d", i,
              k->laps_done, g.track.laps);
        CHECK(k->best_lap_time > 0.0f, "kart %d has no best lap", i);
        CHECK(k->best_lap_time <= k->last_lap_time + 0.001f ||
              k->last_lap_time <= 0.0f,
              "kart %d: best lap %.2f is slower than its last %.2f", i,
              k->best_lap_time, k->last_lap_time);
        /* the laps have to add up to the race: every timed lap fits
         * inside the finishing time, and the best one is a real lap */
        CHECK(k->best_lap_time * (float)k->laps_done <= k->finish_time + 0.5f,
              "kart %d: %d laps at best %.2f exceed its %.2f s race", i,
              k->laps_done, k->best_lap_time, k->finish_time);
        CHECK(k->best_lap_time > 5.0f,
              "kart %d recorded an impossible %.2f s lap", i,
              k->best_lap_time);
    }
}

/*
 * Going over a cliff and being put back at a checkpoint is not a lap, and
 * must not hand out a suspiciously quick one either.
 */
static void test_lap_times_survive_respawn(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_MONARCH);
    Input in[MAX_HUMANS];
    Kart *k;
    int f, laps_before, respawned = 0;
    float best_before;

    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);
    in[0].accel = 1;
    k = &g.karts[0];

    for (f = 0; f < 60 * 60; f++)
        game_update(&g, in, 1.0f / 60.0f);
    laps_before = k->laps_done;
    best_before = k->best_lap_time;

    /* off the edge, a couple of segments short of the line */
    teleport_lat(&g, k, g.track.n - 3, g.track.wall_half + 4.0f, 26.0f);
    k->lap = (int)floorf(k->total_progress / (float)g.track.n);
    for (f = 0; f < 60 * 6; f++) {
        game_update(&g, in, 1.0f / 60.0f);
        if (k->respawned)
            respawned = 1;
        /* lap_start_t is reset as the event fires, so the thing to
         * check is the time it actually recorded */
        CHECK(!k->lap_event || k->last_lap_time > 5.0f,
              "a lap of %.2f s was recorded around a respawn",
              k->last_lap_time);
    }

    printf("lap timing across a respawn: %d laps before, %d after, "
           "best %.2f -> %.2f\n", laps_before, k->laps_done, best_before,
           k->best_lap_time);
    CHECK(respawned, "the car never fell and respawned");
    CHECK(k->laps_done <= laps_before + 1,
          "a respawn added %d laps", k->laps_done - laps_before);
    CHECK(k->best_lap_time <= 0.0f || k->best_lap_time > 5.0f,
          "a respawn recorded a %.2f s best lap", k->best_lap_time);
}

/*
 * The rivals have names, and those names have to survive being drawn by a
 * HUD font with a limited alphabet and a narrow column.
 */
/*
 * Difficulty presets are scaffolding (see the DifficultyPreset comment
 * in game.h): the data shape has to be sound even though nothing wires
 * it into an actual race yet. This is a data-model test, not a
 * gameplay one — there is no gameplay effect to test.
 */
static void test_difficulty_presets_scaffolding(void)
{
    int i;

    CHECK(strcmp(difficulty_preset_name(DIFFICULTY_EASY), "EASY") == 0 &&
          strcmp(difficulty_preset_name(DIFFICULTY_NORMAL), "NORMAL") == 0 &&
          strcmp(difficulty_preset_name(DIFFICULTY_HARD), "HARD") == 0,
          "difficulty preset names are wired up wrong");
    CHECK(strcmp(difficulty_preset_name(99), "NORMAL") == 0,
          "an out-of-range preset should fall back to NORMAL");

    for (i = 0; i < DIFFICULTY_PRESET_COUNT; i++)
        CHECK(difficulty_presets[i].name && difficulty_presets[i].name[0],
              "preset %d has no name", i);

    CHECK(difficulty_presets[DIFFICULTY_EASY].ai_aggressiveness <
              difficulty_presets[DIFFICULTY_NORMAL].ai_aggressiveness &&
          difficulty_presets[DIFFICULTY_NORMAL].ai_aggressiveness <
              difficulty_presets[DIFFICULTY_HARD].ai_aggressiveness,
          "difficulty presets do not get more aggressive from easy to hard");
    CHECK(difficulty_presets[DIFFICULTY_EASY].guardrails ==
              DIFFICULTY_GUARDRAILS_ON &&
          difficulty_presets[DIFFICULTY_HARD].guardrails ==
              DIFFICULTY_GUARDRAILS_OFF,
          "easy/hard do not lean the expected way on guardrails");
}

/* Team mode is scaffolding, same as difficulty presets: this only checks
 * the data shape (names, distinct paint colours) is sane, not that a
 * race actually groups karts into teams — nothing wires that up yet. */
static void test_team_mode_scaffolding(void)
{
    int i, j;
    GameConfig cfg;

    for (i = 0; i < TEAM_COUNT; i++)
        CHECK(team_defs[i].name && team_defs[i].name[0],
              "team %d has no name", i);
    CHECK(strcmp(team_name(0), team_defs[0].name) == 0,
          "team_name(0) does not match the table");
    CHECK(strcmp(team_name(99), "CRIMSON") == 0,
          "an out-of-range team should fall back to CRIMSON");

    for (i = 0; i < TEAM_COUNT; i++)
        for (j = i + 1; j < TEAM_COUNT; j++)
            CHECK(team_defs[i].paint_idx != team_defs[j].paint_idx,
                  "teams %d and %d fly the same paint colour", i, j);

    /* zero-initializing a GameConfig (the usual pattern) must leave team
     * mode off — this is a scaffolding field, so nothing may start a
     * race grouped into teams by accident */
    memset(&cfg, 0, sizeof(cfg));
    CHECK(cfg.team_mode == 0, "a zero-initialized config has team mode on");
}

/* Career mode is scaffolding too: check the one piece of plumbing it
 * has (career_record_result) actually stores what it's given, and that
 * a race does not yet act on it — every human still starts at the back
 * of the grid no matter what CareerState says. */
static void test_career_mode_scaffolding(void)
{
    CareerState cs;
    Game plain, careered;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);

    memset(&cs, 0, sizeof(cs));
    CHECK(cs.has_last_result == 0, "a zero-initialized CareerState has a result");
    career_record_result(&cs, 3);
    CHECK(cs.has_last_result == 1 && cs.last_finish_rank == 3,
          "career_record_result did not store the finish (%d, rank %d)",
          cs.has_last_result, cs.last_finish_rank);

    game_init(&plain, &cfg);
    cfg.career[0] = cs;                 /* pretend the human finished 3rd */
    game_init(&careered, &cfg);
    CHECK(fabsf(plain.karts[0].x - careered.karts[0].x) < 0.001f &&
          fabsf(plain.karts[0].z - careered.karts[0].z) < 0.001f &&
          fabsf(plain.karts[0].heading - careered.karts[0].heading) < 0.001f,
          "a populated CareerState changed the starting grid — "
          "game_init should not read it yet");
}

static void test_driver_names(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    int i, j;

    game_init(&g, &cfg);
    for (i = g.cfg.n_humans; i < NUM_KARTS; i++) {
        const char *name = ai_driver_name(g.karts[i].driver_no);
        int len = (int)strlen(name);
        CHECK(len > 1 && len <= 8, "driver name \"%s\" is %d characters",
              name, len);
        for (j = 0; j < len; j++)
            CHECK(name[j] >= 'A' && name[j] <= 'Z',
                  "driver name \"%s\" has a character the HUD cannot draw",
                  name);
        for (j = g.cfg.n_humans; j < i; j++)
            CHECK(strcmp(ai_driver_name(g.karts[j].driver_no), name) != 0,
                  "two cars are both called %s", name);
    }
    printf("drivers: %s, %s, %s ... %s\n",
           ai_driver_name(0), ai_driver_name(1), ai_driver_name(2),
           ai_driver_name(NUM_KARTS - 2));
}

/* The tachometer's rev range is configurable, and has to make sense. */
static void test_tacho_settings(void)
{
    GameSettings s;
    char error[128];

    game_settings_defaults(&s);
    CHECK(s.tacho_redline_rpm > s.tacho_idle_rpm,
          "the built-in redline is not above idle");
    CHECK(config_load_settings_text(&s,
              "{\"instruments\":{\"tacho_redline_rpm\":9500}}", error,
              (int)sizeof(error)),
          "a redline change was rejected: %s", error);
    CHECK(fabsf(s.tacho_redline_rpm - 9500.0f) < 0.5f,
          "the redline change did not take (%.0f)", s.tacho_redline_rpm);

    game_settings_defaults(&s);
    CHECK(!config_load_settings_text(&s,
              "{\"instruments\":{\"tacho_redline_rpm\":400,"
              "\"tacho_idle_rpm\":3000}}", error, (int)sizeof(error)),
          "a redline below idle was accepted");
    CHECK(fabsf(s.tacho_redline_rpm - 7800.0f) < 0.5f,
          "a rejected instruments block was applied anyway");
}

/* The pre-race lap count overrides the circuit's own, within limits. */
static void test_lap_count_override(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_BERTHOUD);
    int automatic;

    game_init(&g, &cfg);
    automatic = g.track.laps;

    cfg.laps_override = 5;
    game_init(&g, &cfg);
    CHECK(g.track.laps == 5, "lap override ignored (%d laps)", g.track.laps);

    cfg.laps_override = 900;
    game_init(&g, &cfg);
    CHECK(g.track.laps <= 20, "an absurd lap override stuck (%d)",
          g.track.laps);

    cfg.laps_override = 0;
    game_init(&g, &cfg);
    CHECK(g.track.laps == automatic,
          "clearing the override did not restore the circuit's %d laps",
          automatic);
    printf("lap override: BERTHOUD is %d laps automatically, 5 when asked\n",
           automatic);
}

/* ------------------------------------------------------------------ */
/* Chase camera                                                        */
/* ------------------------------------------------------------------ */

/* is the camera in front of the car, or behind it? positive = in front */
static float camera_ahead_of(const CameraState *c, const Kart *k)
{
    return (c->x - k->x) * cosf(k->heading) + (c->z - k->z) * sinf(k->heading);
}

/* how far ahead of the car the aim point sits, along the car's nose */
static float camera_aim_ahead_of(const CameraState *c, const Kart *k)
{
    return (c->look_x - k->x) * cosf(k->heading) +
           (c->look_z - k->z) * sinf(k->heading);
}

static void camera_settle(CameraState *c, const GameSettings *s,
                          const Kart *k, const Track *t, float seconds)
{
    int f, n = (int)(seconds * 60.0f);
    for (f = 0; f < n; f++)
        camera_update(c, s, k, t, 1.0f / 60.0f);
}

/*
 * Reversing swings the view round to the nose so you are looking where
 * the car is actually going, and driving forward again brings it back.
 */
static void test_camera_reverse_swing(void)
{
    GameSettings s;
    Track t;
    CameraState c;
    Kart k;
    float ahead_fwd, ahead_rev, aim_rev, back_after;

    game_settings_defaults(&s);
    track_init_with_settings(&t, TRACK_CLASSIC, &s);
    memset(&k, 0, sizeof(k));
    k.x = t.px[20]; k.z = t.pz[20]; k.y = t.py[20];
    k.heading = atan2f(t.dz[20], t.dx[20]);
    k.seg = 20;

    k.speed = 20.0f;
    camera_reset(&c, &s, &k, &t);
    camera_settle(&c, &s, &k, &t, 2.0f);
    ahead_fwd = camera_ahead_of(&c, &k);
    CHECK(ahead_fwd < -2.0f,
          "driving forward, the camera should sit behind the car (%.1f m)",
          ahead_fwd);
    CHECK(camera_aim_ahead_of(&c, &k) > 1.0f,
          "driving forward, the camera should look up the road");

    /* now reverse, hard enough to be past the dead zone */
    k.speed = -8.0f;
    camera_settle(&c, &s, &k, &t, 4.0f);
    ahead_rev = camera_ahead_of(&c, &k);
    aim_rev = camera_aim_ahead_of(&c, &k);
    printf("camera: %.1f m behind going forward, %.1f m ahead reversing, "
           "aim %.1f m\n", -ahead_fwd, ahead_rev, aim_rev);
    CHECK(ahead_rev > 2.0f,
          "reversing, the camera should swing round to the nose (%.1f m)",
          ahead_rev);
    CHECK(aim_rev < -1.0f,
          "reversing, the camera should look back down the road (%.1f m)",
          aim_rev);
    CHECK(c.orbit > 2.6f, "reverse orbit only reached %.2f rad", c.orbit);

    /* and back to normal once the car is going forward again */
    k.speed = 14.0f;
    camera_settle(&c, &s, &k, &t, 4.0f);
    back_after = camera_ahead_of(&c, &k);
    CHECK(c.orbit < 0.15f,
          "the camera did not come back (orbit %.2f rad)", c.orbit);
    CHECK(back_after < -2.0f,
          "the camera did not return behind the car (%.1f m)", back_after);
}

/*
 * Crossing through zero must not snap the view round, and sitting on the
 * edge of the dead zone must not set it hunting: a car rocking back and
 * forth at walking pace is the worst case for both.
 */
static void test_camera_no_snap_or_hunt(void)
{
    GameSettings s;
    Track t;
    CameraState c;
    Kart k;
    int f;
    float prev, worst_step = 0.0f, max_orbit = 0.0f;
    int direction_changes = 0, last_sign = 0;

    game_settings_defaults(&s);
    track_init_with_settings(&t, TRACK_CLASSIC, &s);
    memset(&k, 0, sizeof(k));
    k.x = t.px[20]; k.z = t.pz[20]; k.y = t.py[20];
    k.heading = atan2f(t.dz[20], t.dx[20]);
    k.seg = 20;
    k.speed = 0.0f;
    camera_reset(&c, &s, &k, &t);

    /* rock across zero and along the dead-zone edge for ten seconds */
    for (f = 0; f < 600; f++) {
        float phase = (float)f / 60.0f;
        k.speed = sinf(phase * 3.0f) * (s.cam_reverse_deadzone_mps + 0.15f);
        prev = c.orbit;
        camera_update(&c, &s, &k, &t, 1.0f / 60.0f);
        if (fabsf(c.orbit - prev) > worst_step)
            worst_step = fabsf(c.orbit - prev);
        if (c.orbit > max_orbit)
            max_orbit = c.orbit;
        if (fabsf(c.orbit - prev) > 1.0e-4f) {
            int sign = (c.orbit > prev) ? 1 : -1;
            if (last_sign != 0 && sign != last_sign)
                direction_changes++;
            last_sign = sign;
        }
    }
    printf("camera near zero: largest step %.3f rad/frame, peak orbit "
           "%.3f rad, %d direction changes\n", worst_step, max_orbit,
           direction_changes);
    CHECK(worst_step <= s.cam_reverse_orbit_rate_dps * (3.14159265f / 180.0f)
                        / 60.0f + 1.0e-4f,
          "the camera moved %.3f rad in one frame, past its rate limit",
          worst_step);
    CHECK(max_orbit < 0.6f,
          "rocking around the dead zone swung the camera %.2f rad", max_orbit);
    CHECK(direction_changes < 40,
          "the camera hunted back and forth %d times", direction_changes);
}

/*
 * On a climb and a descent the camera should tilt with the road — enough
 * to keep a consistent view of it — without ever aiming into the pavement
 * or losing the road over a crest.
 */
static void test_camera_follows_road_pitch(void)
{
    GameSettings s;
    Track t;
    int seg, climb_seg = -1, drop_seg = -1, i;
    float pitch_flat = 0.0f, pitch_climb = 0.0f, pitch_drop = 0.0f;
    float steepest_up = 0.0f, steepest_down = 0.0f;
    Kart k;
    CameraState c;

    game_settings_defaults(&s);
    track_init_with_settings(&t, TRACK_MONARCH, &s);

    for (i = 0; i < t.n; i++) {
        if (t.slope[i] > steepest_up)   { steepest_up = t.slope[i];   climb_seg = i; }
        if (t.slope[i] < steepest_down) { steepest_down = t.slope[i]; drop_seg = i; }
    }
    CHECK(climb_seg >= 0 && drop_seg >= 0, "no gradient found on MONARCH");

    for (i = 0; i < 3; i++) {
        float view;
        seg = (i == 0) ? 0 : (i == 1 ? climb_seg : drop_seg);
        /* a flat-ish reference for i == 0: pick the shallowest segment */
        if (i == 0) {
            int j; float best = 1.0e9f;
            for (j = 0; j < t.n; j++)
                if (fabsf(t.slope[j]) < best) { best = fabsf(t.slope[j]); seg = j; }
        }
        memset(&k, 0, sizeof(k));
        k.x = t.px[seg]; k.z = t.pz[seg]; k.y = t.py[seg];
        k.heading = atan2f(t.dz[seg], t.dx[seg]);
        k.seg = seg;
        k.speed = 18.0f;
        camera_reset(&c, &s, &k, &t);
        camera_settle(&c, &s, &k, &t, 3.0f);
        view = camera_view_pitch(&c);
        if (i == 0) pitch_flat = view;
        else if (i == 1) pitch_climb = view;
        else pitch_drop = view;

        CHECK(c.y > k.y - 0.5f,
              "camera sank below the car on segment %d (%.1f vs %.1f)",
              seg, c.y, k.y);
        CHECK(fabsf(view) < 1.0f,
              "camera view pitch %.2f rad on segment %d is extreme",
              view, seg);
        CHECK(camera_aim_ahead_of(&c, &k) > 1.0f,
              "camera lost the road ahead on segment %d", seg);
    }

    printf("camera pitch: flat %+.1f deg, climb %+.1f deg (grade %+.0f%%), "
           "descent %+.1f deg (grade %+.0f%%)\n",
           pitch_flat * 57.2958f, pitch_climb * 57.2958f, steepest_up * 100.0f,
           pitch_drop * 57.2958f, steepest_down * 100.0f);

    /* uphill the view tilts up relative to flat, downhill it tilts down */
    CHECK(pitch_climb < pitch_flat - 0.02f,
          "the camera did not tilt up for a %.0f%% climb (%.2f vs %.2f rad)",
          steepest_up * 100.0f, pitch_climb, pitch_flat);
    CHECK(pitch_drop > pitch_flat + 0.02f,
          "the camera did not tilt down for a %.0f%% descent (%.2f vs %.2f)",
          steepest_down * 100.0f, pitch_drop, pitch_flat);
}

/*
 * Drive whole laps of every circuit and watch the camera: it must stay
 * above the ground, keep the car's direction of travel in front of it,
 * and never produce a number that is not a number.
 */
static void test_camera_stays_sane_everywhere(void)
{
    int id;
    for (id = 0; id < TRACK_COUNT; id++) {
        Game g;
        GameConfig cfg = default_cfg(id);
        Input in[MAX_HUMANS];
        CameraState c;
        int f;
        float lowest = 1.0e9f, worst_pitch = 0.0f;

        game_init(&g, &cfg);
        g.state = STATE_RACING;
        idle_inputs(in);
        in[0].accel = 1;
        camera_reset(&c, &g.settings, &g.karts[0], &g.track);

        for (f = 0; f < 60 * 90; f++) {
            int seg;
            float frac, lat, ground;
            game_update(&g, in, 1.0f / 60.0f);
            camera_update(&c, &g.settings, &g.karts[0], &g.track,
                          1.0f / 60.0f);
            if (f % 7 == 0)                    /* steer about a bit */
                in[0].steer = sinf((float)f / 90.0f);
            track_locate(&g.track, c.x, c.z, g.karts[0].seg, &seg, &frac,
                         &lat, &ground);
            if (c.y - ground < lowest)
                lowest = c.y - ground;
            if (fabsf(camera_view_pitch(&c)) > fabsf(worst_pitch))
                worst_pitch = camera_view_pitch(&c);
            CHECK(!isnan(c.x) && !isnan(c.y) && !isnan(c.z) &&
                  !isnan(c.look_x) && !isnan(c.look_y) && !isnan(c.look_z),
                  "%s: camera went NaN at frame %d", track_name(id), f);
            if (isnan(c.x))
                break;
        }
        printf("camera on %-9s lowest %.2f m over the ground, worst view "
               "pitch %+.1f deg\n", track_name(id), lowest,
               worst_pitch * 57.2958f);
        CHECK(lowest > g.settings.cam_min_height_m - 0.05f,
              "%s: camera got %.2f m off the ground, floor is %.2f",
              track_name(id), lowest, g.settings.cam_min_height_m);
        CHECK(fabsf(worst_pitch) < 1.2f,
              "%s: camera view pitch reached %.2f rad", track_name(id),
              worst_pitch);
    }
}

/*
 * Being picked up and put back at a checkpoint is a teleport, not a
 * drive: the camera has to start again there rather than streak across
 * the mountain to catch up.
 */
static void test_camera_snaps_after_respawn(void)
{
    GameSettings s;
    Track t;
    Kart k;
    CameraState c;
    float gap;
    int far_seg;

    game_settings_defaults(&s);
    track_init_with_settings(&t, TRACK_MONARCH, &s);
    memset(&k, 0, sizeof(k));
    k.x = t.px[10]; k.z = t.pz[10]; k.y = t.py[10];
    k.heading = atan2f(t.dz[10], t.dx[10]);
    k.seg = 10;
    k.speed = 20.0f;
    camera_reset(&c, &s, &k, &t);
    camera_settle(&c, &s, &k, &t, 2.0f);

    far_seg = t.n / 2;                 /* halfway round the circuit */
    k.x = t.px[far_seg]; k.z = t.pz[far_seg]; k.y = t.py[far_seg];
    k.heading = atan2f(t.dz[far_seg], t.dx[far_seg]);
    k.seg = far_seg;
    k.speed = 0.0f;
    camera_update(&c, &s, &k, &t, 1.0f / 60.0f);

    gap = sqrtf((c.x - k.x) * (c.x - k.x) + (c.z - k.z) * (c.z - k.z));
    printf("camera after a respawn: %.1f m from the car in one frame "
           "(distance setting %.1f)\n", gap, s.cam_distance_m);
    CHECK(gap < s.cam_distance_m * 1.6f,
          "the camera was left %.1f m behind after a respawn", gap);
    CHECK(camera_aim_ahead_of(&c, &k) > 1.0f,
          "the camera was not looking up the road after a respawn");
}

/* Look-ahead grows with speed and stops at its configured cap. */
static void test_camera_look_ahead_scales(void)
{
    GameSettings s;
    Track t;
    Kart k;
    CameraState c;
    float slow, fast, flat_out;

    game_settings_defaults(&s);
    track_init_with_settings(&t, TRACK_CLASSIC, &s);
    memset(&k, 0, sizeof(k));
    k.x = t.px[20]; k.z = t.pz[20]; k.y = t.py[20];
    k.heading = atan2f(t.dz[20], t.dx[20]);
    k.seg = 20;

    k.speed = 5.0f;
    camera_reset(&c, &s, &k, &t);
    camera_settle(&c, &s, &k, &t, 2.0f);
    slow = camera_aim_ahead_of(&c, &k);

    k.speed = 40.0f;
    camera_settle(&c, &s, &k, &t, 2.0f);
    fast = camera_aim_ahead_of(&c, &k);

    k.speed = 400.0f;               /* absurd, to prove the cap holds */
    camera_settle(&c, &s, &k, &t, 2.0f);
    flat_out = camera_aim_ahead_of(&c, &k);

    printf("camera look-ahead: %.1f m at 5 m/s, %.1f m at 40 m/s, "
           "%.1f m flat out (cap %.1f)\n", slow, fast, flat_out,
           s.cam_look_ahead_max_m);
    CHECK(fast > slow + 2.0f,
          "look-ahead did not grow with speed (%.1f -> %.1f m)", slow, fast);
    CHECK(flat_out <= s.cam_look_ahead_max_m + 0.5f,
          "look-ahead blew past its %.1f m cap (%.1f m)",
          s.cam_look_ahead_max_m, flat_out);
}

/* The camera block in settings.json is real, and bad values are refused. */
static void test_camera_settings_json(void)
{
    GameSettings s;
    char error[128];
    const char *tuned =
        "{\"camera\":{\"distance_m\":14.0,\"pitch_influence\":0.25,"
        "\"reverse_full_mps\":9.0}}";
    const char *broken =
        "{\"camera\":{\"distance_m\":900.0}}";
    const char *backwards =
        "{\"camera\":{\"reverse_deadzone_mps\":8.0,\"reverse_full_mps\":4.0}}";

    game_settings_defaults(&s);
    CHECK(config_load_settings_file(&s, "config/settings.json", error,
                                    (int)sizeof(error)),
          "the shipped settings.json failed to load: %s", error);
    CHECK(s.cam_distance_m > 0.0f && s.cam_pitch_max_deg > 0.0f,
          "the shipped settings.json left the camera block empty");

    game_settings_defaults(&s);
    CHECK(config_load_settings_text(&s, tuned, error, (int)sizeof(error)),
          "a camera tune was rejected: %s", error);
    CHECK(fabsf(s.cam_distance_m - 14.0f) < 0.001f &&
          fabsf(s.cam_pitch_influence - 0.25f) < 0.001f,
          "the camera tune did not take effect");
    CHECK(fabsf(s.cam_height_m - 3.6f) < 0.001f,
          "tuning one camera value clobbered the others");

    game_settings_defaults(&s);
    CHECK(!config_load_settings_text(&s, broken, error, (int)sizeof(error)),
          "a 900 m camera distance was accepted");
    CHECK(fabsf(s.cam_distance_m - 9.0f) < 0.001f,
          "a rejected camera block was applied anyway (%.1f m)",
          s.cam_distance_m);

    game_settings_defaults(&s);
    CHECK(!config_load_settings_text(&s, backwards, error,
                                     (int)sizeof(error)),
          "a dead zone above the full-swing speed was accepted");
}

int main(void)
{
    test_compiled_roster_matches_cars_json();
    test_json_configuration();
    test_steering_filter();
    test_steer_sign();
    test_tracks_geometry();
    test_pavement_reaches_guardrail();
    test_spec_stats();
    test_countdown_holds();
    test_braking_distance();
    test_gravity_grade();
    test_cornering_grip_cap();
    test_drivetrain_traction();
    test_drivetrain_cornering_balance();
    test_drivetrain_json_parsing();
    test_gear_power_curve();
    test_gearboxes_sane();
    test_shifting();
    test_boost_system();
    test_gear_limits_speed();
    test_tire_compounds();
    test_weather_only_on_classic();
    test_weather_melts_over_time();
    test_weather_tire_grip_ordering();
    test_weather_reaches_the_physics();
    test_weather_puddle_drag();
    test_ai_weather_awareness();
    test_tire_strategy_crossover();
    test_tires_need_work();
    test_track_roster();
    test_lap_counts();
    test_cliff_respawn();
    test_guardrails_still_hold();
    test_respawn_near_the_line();
    test_respawn_bad_checkpoint();
    test_fall_is_visible();
    test_finished_human_coasts();
    test_nan_hardening();
    test_corner_segmentation();
    test_berthoud2_keeps_its_switchbacks();
    test_full_grid_fits();
    test_understeer_scrub_is_progressive();
    test_oversteer_rewards_a_catch_and_punishes_a_miss();
    test_oversteer_never_triggers_when_driving_gently();
    test_ai_races_all_tracks();
    test_full_race_classic();
    test_yolo_strategy_is_the_wildest();
    test_yolo_can_finish_the_hardest_track();
    test_ai_strategies_differ();
    test_ai_learns_from_mistakes();
    test_ai_adapts_to_player();
    test_no_rubber_banding();
    test_ai_shift_styles();
    test_ai_can_fall();
    test_player_model_learns();
    test_cooldown_driver_brings_the_car_home();
    test_whole_field_survives_the_flag();
    test_wrong_way_marshal_cuts_power();
    test_wrong_way_marshal_leaves_the_ai_alone();
    test_driver_field_has_characters();
    test_temperament_shows_on_track();
    test_skill_sets_pace();
    test_racing_line_pace_is_not_the_sheet();
    test_editing_cars_json_changes_the_car();
    test_grade_costs_speed();
    test_grade_costs_grip();
    test_car_shift_points();
    test_new_passes();
    test_variable_road_width();
    test_narrow_sections_bite();
    test_lap_times();
    test_lap_times_survive_respawn();
    test_lap_count_override();
    test_difficulty_presets_scaffolding();
    test_team_mode_scaffolding();
    test_career_mode_scaffolding();
    test_driver_names();
    test_tacho_settings();
    test_camera_reverse_swing();
    test_camera_no_snap_or_hunt();
    test_camera_follows_road_pitch();
    test_camera_look_ahead_scales();
    test_camera_snaps_after_respawn();
    test_camera_stays_sane_everywhere();
    test_camera_settings_json();

    if (failures) {
        printf("%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
