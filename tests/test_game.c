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
        const int max_frames = 60 * 400;
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
                CHECK(fabsf(k->lat) < g.track.wall_half + 2.0f,
                      "AI %d outside the barriers on track %d (lat %.1f)",
                      i, id, k->lat);
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
               RACE_LAPS, best_lap, g.track.total_len);
        CHECK(finished == NUM_KARTS - 1,
              "only %d of %d AI finished on track %d", finished,
              NUM_KARTS - 1, id);
        /* a plausible pace for the distance: 8..32 m/s average */
        CHECK(best_lap > g.track.total_len / 32.0f &&
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

/* Eleven AI on seven strategy sheets must actually drive differently:
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

        lines[i] = k->ai_line;
        for (j = 1; j < i; j++)
            if (fabsf(lines[j] - k->ai_line) < 0.05f)
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

        if (k->strategy == AI_LATE || k->strategy == AI_CHARGER) {
            /* started believing it could beat the grip limit */
            CHECK(mean < st->conf_start - 0.02f,
                  "%s never learned to brake earlier (%.3f from %.3f)",
                  st->name, mean, st->conf_start);
            CHECK(k->mistakes > 0, "%s made no mistakes to learn from",
                  st->name);
            checked_bold = 1;
        }
        if (k->strategy == AI_CRUISER) {
            CHECK(mean > st->conf_start + 0.02f,
                  "%s never found extra pace (%.3f from %.3f)",
                  st->name, mean, st->conf_start);
            checked_timid = 1;
        }
    }
    CHECK(checked_bold && checked_timid,
          "did not exercise both a bold and a timid strategy");
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


/* AI must actually use the nitro they collect. Berthoud has no boost
 * pads and the AI never drift, so any boost seen there came from an
 * item being deliberately spent. */
static void test_ai_uses_nitro(void)
{
    Game g;
    GameConfig cfg = default_cfg(TRACK_BERTHOUD);
    Input in[MAX_HUMANS];
    int f, i, boosts = 0, held_seen = 0;

    game_init(&g, &cfg);
    idle_inputs(in);
    for (f = 0; f < 60 * 120; f++) {
        game_update(&g, in, 1.0f / 60.0f);
        for (i = 1; i < NUM_KARTS; i++) {
            if (g.karts[i].item_held)
                held_seen = 1;
            if (g.karts[i].just_boosted)
                boosts++;
        }
    }
    printf("AI nitro: %d canisters spent over 2 minutes\n", boosts);
    CHECK(held_seen, "no AI ever picked up an item");
    CHECK(boosts > 0, "AI never spent their nitro");
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
    test_corner_segmentation();
    test_full_grid_fits();
    test_ai_races_all_tracks();
    test_full_race_classic();
    test_ai_strategies_differ();
    test_ai_learns_from_mistakes();
    test_ai_adapts_to_player();
    test_ai_uses_nitro();
    test_player_model_learns();

    if (failures) {
        printf("%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
