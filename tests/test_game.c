/*
 * Host-side tests for the WiiKart simulation. Built with a normal PC
 * compiler (no devkitPPC needed):
 *
 *   gcc -std=c99 -O2 -Wall -Werror -Isource \
 *       tests/test_game.c source/game.c source/track.c -lm -o wiikart-test
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"

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
        CHECK(t.total_len > 400.0f && t.total_len < 4000.0f,
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
            CHECK(t.max_y - t.min_y > 40.0f, "pass %d too flat", id);
            CHECK(t.alpine, "pass %d not alpine", id);
        } else {
            CHECK(t.max_y - t.min_y < 1.0f, "classic not flat");
            CHECK(t.n_pads > 0, "classic has no boost pads");
        }
        CHECK(t.n_items > 0, "track %d has no item rows", id);
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

    /* find a steady climb and a steady descent */
    for (i = 0; i < g.track.n; i++) {
        if (seg_up < 0 && g.track.slope[i] > 0.06f &&
            g.track.curv[i] < 0.02f)
            seg_up = i;
        if (seg_down < 0 && g.track.slope[i] < -0.06f &&
            g.track.curv[i] < 0.02f)
            seg_down = i;
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

static void test_items(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    int f;

    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);

    /* drive through the item row from just before it */
    {
        int seg = (g.track.item_seg[0] - 2 + g.track.n) % g.track.n;
        teleport(&g, &g.karts[0], seg, 10.0f);
    }
    in[0].accel = 1;
    for (f = 0; f < 240 && !g.karts[0].item_held; f++)
        game_update(&g, in, 1.0f / 60.0f);
    CHECK(g.karts[0].item_held, "did not pick up an item");

    in[0].item = 1;
    game_update(&g, in, 1.0f / 60.0f);
    CHECK(!g.karts[0].item_held, "item not consumed");
    CHECK(g.karts[0].boost_t > 1.0f, "item gave no nitro");
}

static void test_ai_laps_all_tracks(void)
{
    int id;
    for (id = 0; id < TRACK_COUNT; id++) {
        Game g;
        GameConfig cfg = default_cfg(id);
        Input in[MAX_HUMANS];
        int f, i;
        const int max_frames = 60 * 240;
        int lapped = 0;

        game_init(&g, &cfg);
        idle_inputs(in);

        for (f = 0; f < max_frames && !lapped; f++) {
            game_update(&g, in, 1.0f / 60.0f);
            lapped = 1;
            for (i = 1; i < NUM_KARTS; i++) {
                Kart *k = &g.karts[i];
                CHECK(!isnan(k->x) && !isnan(k->speed) && !isnan(k->heading),
                      "AI %d NaN on track %d frame %d", i, id, f);
                CHECK(fabsf(k->lat) < g.track.wall_half + 2.0f,
                      "AI %d off-world on track %d (lat %.1f) frame %d",
                      i, id, k->lat, f);
                if (failures) return;
                if (k->total_progress < (float)g.track.n)
                    lapped = 0;
            }
        }
        CHECK(lapped, "AI karts did not complete a lap on track %d", id);
        if (lapped)
            printf("track %-8s: all AI completed a lap by t=%.0f s "
                   "(lap ~%.0f m)\n",
                   g.track.name, f / 60.0f, g.track.total_len);
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
    CHECK(done, "AI did not finish %d laps on classic", RACE_LAPS);
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

/* Steering sign convention: positive steer must turn the car left, so a
 * left key mapped to +1 actually goes left on screen. */
static void test_steer_sign(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_CLASSIC);
    Input in[MAX_HUMANS];
    float h0, h_left, h_right;

    game_init(&g, &cfg);
    g.state = STATE_RACING;
    idle_inputs(in);

    teleport(&g, &g.karts[0], 2, 20.0f);
    h0 = g.karts[0].heading;
    in[0].steer = 1.0f;
    game_update(&g, in, 1.0f / 60.0f);
    h_left = game_angle_wrap(g.karts[0].heading - h0);

    teleport(&g, &g.karts[0], 2, 20.0f);
    h0 = g.karts[0].heading;
    in[0].steer = -1.0f;
    game_update(&g, in, 1.0f / 60.0f);
    h_right = game_angle_wrap(g.karts[0].heading - h0);

    CHECK(h_left > 0.0f, "positive steer did not turn left (%.4f)", h_left);
    CHECK(h_right < 0.0f, "negative steer did not turn right (%.4f)",
          h_right);
}

int main(void)
{
    test_steering_filter();
    test_steer_sign();
    test_tracks_geometry();
    test_spec_stats();
    test_countdown_holds();
    test_braking_distance();
    test_gravity_grade();
    test_cornering_grip_cap();
    test_items();
    test_ai_laps_all_tracks();
    test_full_race_classic();

    if (failures) {
        printf("%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
