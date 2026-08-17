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

static void test_track_geometry(void)
{
    Track t;
    int i;
    track_init(&t);

    printf("track: %d samples, length %.1f units\n", t.n, t.total_len);
    CHECK(t.n >= 64 && t.n <= TRACK_MAX_POINTS, "sample count %d", t.n);
    CHECK(t.total_len > 300.0f && t.total_len < 2000.0f,
          "track length %.1f", t.total_len);

    for (i = 0; i < t.n; i++) {
        float dlen = sqrtf(t.dx[i] * t.dx[i] + t.dz[i] * t.dz[i]);
        CHECK(fabsf(dlen - 1.0f) < 1e-3f, "dir not unit at %d", i);
        CHECK(t.seg_len[i] > 0.05f && t.seg_len[i] < 30.0f,
              "odd segment length %.3f at %d", t.seg_len[i], i);
        CHECK(!isnan(t.px[i]) && !isnan(t.pz[i]), "NaN point at %d", i);
    }

    /* locate: a point exactly on the centerline has ~zero lateral */
    for (i = 0; i < t.n; i += 7) {
        int seg; float frac, lat;
        track_locate(&t, t.px[i], t.pz[i], -1, &seg, &frac, &lat);
        CHECK(fabsf(lat) < 0.5f, "lat %.2f on centerline at %d", lat, i);
    }

    /* locate: a point pushed left of the centerline reports positive lat */
    {
        int seg; float frac, lat;
        float lx = -t.dz[10], lz = t.dx[10];
        track_locate(&t, t.px[10] + lx * 3.0f, t.pz[10] + lz * 3.0f, -1,
                     &seg, &frac, &lat);
        CHECK(lat > 2.0f && lat < 4.0f, "left offset lat %.2f", lat);
    }
}

static void test_countdown_holds_karts(void)
{
    Game g;
    Input in;
    int i, f;

    game_init(&g);
    memset(&in, 0, sizeof(in));
    in.accel = 1;

    float x0 = g.karts[0].x, z0 = g.karts[0].z;
    for (f = 0; f < 60; f++)           /* 1 s of countdown */
        game_update(&g, &in, 1.0f / 60.0f);

    CHECK(g.state == STATE_COUNTDOWN, "state %d during countdown", g.state);
    CHECK(fabsf(g.karts[0].x - x0) < 1e-4f && fabsf(g.karts[0].z - z0) < 1e-4f,
          "player moved during countdown");
    for (i = 0; i < NUM_KARTS; i++)
        CHECK(g.karts[i].lap <= 0, "lap counted before start");
}

static void test_player_accelerates(void)
{
    Game g;
    Input in;
    int f;

    game_init(&g);
    memset(&in, 0, sizeof(in));
    in.accel = 1;

    /* The opening stretch curves, so a blind straight-line drive will
     * eventually leave the road; check the peak speed reached on it. */
    float vpeak = 0.0f;
    for (f = 0; f < 60 * 9; f++) {     /* countdown (3.2 s) + ~5.8 s drive */
        game_update(&g, &in, 1.0f / 60.0f);
        if (g.karts[0].speed > vpeak)
            vpeak = g.karts[0].speed;
    }

    CHECK(g.state != STATE_COUNTDOWN, "still counting down");
    CHECK(vpeak > 0.85f * KART_VMAX,
          "player peak speed %.1f under full throttle", vpeak);
    CHECK(!isnan(g.karts[0].x) && !isnan(g.karts[0].heading), "player NaN");
    CHECK(g.karts[0].total_progress > 0.0f, "no progress after driving");
}

static void test_ai_race_completes(void)
{
    Game g;
    Input in;
    int i, f;
    const float dt = 1.0f / 60.0f;
    const int max_frames = 60 * 240;   /* 4 minutes of sim */
    int all_done_at = -1;

    game_init(&g);
    memset(&in, 0, sizeof(in));        /* player just idles on the grid */

    for (f = 0; f < max_frames; f++) {
        game_update(&g, &in, dt);

        for (i = 1; i < NUM_KARTS; i++) {
            Kart *k = &g.karts[i];
            CHECK(!isnan(k->x) && !isnan(k->z) && !isnan(k->speed),
                  "AI %d NaN at frame %d", i, f);
            CHECK(fabsf(k->lat) < g.track.wall_half + 2.0f,
                  "AI %d far outside walls (lat %.1f) at frame %d",
                  i, k->lat, f);
            if (failures) return;
        }

        int done = 1;
        for (i = 1; i < NUM_KARTS; i++)
            if (!g.karts[i].finished)
                done = 0;
        if (done) { all_done_at = f; break; }
    }

    CHECK(all_done_at > 0, "AI karts did not finish %d laps in 4 min",
          RACE_LAPS);
    if (all_done_at > 0) {
        printf("AI race: all %d AI karts finished %d laps in %.1f s\n",
               NUM_KARTS - 1, RACE_LAPS, all_done_at / 60.0f);
        /* Plausible lap pace: 15..70 s per lap */
        float pace = (all_done_at / 60.0f) / RACE_LAPS;
        CHECK(pace > 15.0f && pace < 70.0f, "odd lap pace %.1f s", pace);
    }

    /* Finish order data is consistent */
    {
        int seen[NUM_KARTS + 1] = { 0 };
        for (i = 1; i < NUM_KARTS; i++) {
            Kart *k = &g.karts[i];
            CHECK(k->final_rank >= 1 && k->final_rank <= NUM_KARTS,
                  "bad final rank %d", k->final_rank);
            CHECK(!seen[k->final_rank], "duplicate final rank %d",
                  k->final_rank);
            seen[k->final_rank] = 1;
            CHECK(k->finish_time > 0.0f, "no finish time");
        }
    }
}

static void test_lap_counting_and_ranks(void)
{
    Game g;
    Input in;
    int f, i;

    game_init(&g);
    memset(&in, 0, sizeof(in));

    /* Drive the player straight through the start line: lap goes 0 ->
     * stays 0 until a full loop; total_progress crosses 0 upward. */
    in.accel = 1;
    int crossed = 0;
    for (f = 0; f < 60 * 10; f++) {
        float before = g.karts[0].total_progress;
        game_update(&g, &in, 1.0f / 60.0f);
        if (before < 0.0f && g.karts[0].total_progress >= 0.0f)
            crossed = 1;
    }
    CHECK(crossed, "player never crossed the start line");
    CHECK(g.karts[0].lap == 0, "lap should be 0 after crossing, got %d",
          g.karts[0].lap);

    /* Ranks are always a permutation of 1..NUM_KARTS */
    int seen[NUM_KARTS + 1] = { 0 };
    for (i = 0; i < NUM_KARTS; i++) {
        CHECK(g.karts[i].rank >= 1 && g.karts[i].rank <= NUM_KARTS,
              "bad rank %d", g.karts[i].rank);
        CHECK(!seen[g.karts[i].rank], "duplicate rank %d", g.karts[i].rank);
        seen[g.karts[i].rank] = 1;
    }
}

static void test_drift_gives_boost(void)
{
    Game g;
    Input in;
    int f;

    game_init(&g);
    memset(&in, 0, sizeof(in));
    in.accel = 1;

    /* get up to speed (countdown included) */
    for (f = 0; f < 60 * 8; f++)
        game_update(&g, &in, 1.0f / 60.0f);

    /* hold a drift for 2 s, then release */
    in.steer = 1.0f;
    in.hop = 1;
    for (f = 0; f < 120; f++)
        game_update(&g, &in, 1.0f / 60.0f);
    CHECK(g.karts[0].drifting != 0, "player not drifting");
    CHECK(g.karts[0].drift_charge > 0.8f, "drift charge %.2f too low",
          g.karts[0].drift_charge);

    in.hop = 0;
    in.steer = 0.0f;
    game_update(&g, &in, 1.0f / 60.0f);
    CHECK(g.karts[0].boost_t > 0.3f, "no mini-turbo after drift (boost %.2f)",
          g.karts[0].boost_t);
}

int main(void)
{
    test_track_geometry();
    test_countdown_holds_karts();
    test_player_accelerates();
    test_lap_counting_and_ranks();
    test_drift_gives_boost();
    test_ai_race_completes();

    if (failures) {
        printf("%d FAILURE(S)\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
