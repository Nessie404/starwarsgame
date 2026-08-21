/*
 * Host-side tests for WiiHaul's top-level game state.
 *
 *   gcc -std=c99 -O2 -Wall -Werror -Isource \
 *       tests/test_game.c source/truck.c source/yard.c source/camera.c \
 *       source/game.c -lm -o wiihaul-test
 */
#include <math.h>
#include <stdio.h>
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

static void test_countdown_then_attempt(void)
{
    GameConfig cfg;
    Game g;
    Input in;
    float dt = 1.0f / 60.0f;
    int i;

    memset(&cfg, 0, sizeof(cfg));
    cfg.scenario_id = SCENARIO_STRAIGHT_BACK;
    cfg.rig_idx = 1;
    game_init(&g, &cfg);
    CHECK(g.state == STATE_COUNTDOWN, "should start in countdown");

    memset(&in, 0, sizeof(in));
    for (i = 0; i < (int)(COUNTDOWN_SECONDS / dt) + 5 &&
        g.state == STATE_COUNTDOWN; i++)
        game_update(&g, &in, dt);
    CHECK(g.state == STATE_ATTEMPT,
         "countdown should end in the attempt state (state=%d)", g.state);
    CHECK(g.rig.x == g.yard.start_x,
         "the rig should not have moved during the countdown");
}

/* Fully driving a straight-backing attempt end to end through Game
 * (not just Yard directly) should reach STATE_FINISHED with a pass. */
static void test_full_attempt_passes(void)
{
    GameConfig cfg;
    Game g;
    Input in;
    float dt = 1.0f / 60.0f;
    int i;

    memset(&cfg, 0, sizeof(cfg));
    cfg.scenario_id = SCENARIO_STRAIGHT_BACK;
    cfg.rig_idx = 1;
    game_init(&g, &cfg);

    memset(&in, 0, sizeof(in));
    for (i = 0; i < 300; i++) game_update(&g, &in, dt); /* countdown */

    memset(&in, 0, sizeof(in));
    in.reverse = 1;
    for (i = 0; i < 60 * 20 && g.state == STATE_ATTEMPT; i++) {
        OBB boxes[1 + MAX_TRAILERS];
        int n = rig_obbs(&g.rig, &g.spec, boxes, 1 + MAX_TRAILERS);
        if (boxes[n - 1].cx <= g.yard.target.cx) break;
        game_update(&g, &in, dt);
    }
    memset(&in, 0, sizeof(in));
    in.brake = 1;
    for (i = 0; i < 300 && g.state == STATE_ATTEMPT; i++)
        game_update(&g, &in, dt);

    CHECK(g.state == STATE_FINISHED, "the attempt should resolve "
         "(state=%d)", g.state);
    CHECK(g.pass_event, "backing gently into the target should pass");
    CHECK(g.attempt.result == ATTEMPT_PASS, "attempt.result should "
         "agree with pass_event");
}

static void test_career_progression(void)
{
    CareerState cs;

    career_reset(&cs);
    CHECK(career_scenario_unlocked(&cs, SCENARIO_STRAIGHT_BACK),
         "the first scenario should always be unlocked");
    CHECK(!career_scenario_unlocked(&cs, SCENARIO_OFFSET_BACK_LEFT),
         "the second scenario should be locked before any pass");

    career_record_attempt(&cs, SCENARIO_STRAIGHT_BACK, GRADE_FAIL_MARK);
    CHECK(!career_scenario_unlocked(&cs, SCENARIO_OFFSET_BACK_LEFT),
         "a failed attempt should not unlock the next scenario");

    career_record_attempt(&cs, SCENARIO_STRAIGHT_BACK, GRADE_BRONZE);
    CHECK(career_scenario_unlocked(&cs, SCENARIO_OFFSET_BACK_LEFT),
         "a bronze-or-better pass should unlock the next scenario");

    career_record_attempt(&cs, SCENARIO_STRAIGHT_BACK, GRADE_FAIL_MARK);
    CHECK(career_scenario_unlocked(&cs, SCENARIO_OFFSET_BACK_LEFT),
         "a later failed attempt should not re-lock a scenario already "
         "unlocked by an earlier best result");
}

static void test_all_rigs_and_scenarios_combine(void)
{
    int rig_idx;
    for (rig_idx = 0; rig_idx < rig_spec_count; rig_idx++) {
        GameConfig cfg;
        Game g;
        Input in;
        int scen;
        for (scen = 0; scen < SCENARIO_COUNT; scen++) {
            Yard y;
            yard_init(scen, &y);
            if (y.min_trailers > rig_specs[rig_idx].n_trailers)
                continue;  /* e.g. DOUBLES_DOCK needs a two-trailer rig */
            memset(&cfg, 0, sizeof(cfg));
            cfg.scenario_id = scen;
            cfg.rig_idx = rig_idx;
            game_init(&g, &cfg);
            memset(&in, 0, sizeof(in));
            game_update(&g, &in, 1.0f / 60.0f);
            CHECK(g.state == STATE_COUNTDOWN || g.state == STATE_ATTEMPT,
                 "rig %d in scenario %d should start cleanly", rig_idx,
                 scen);
        }
    }
}

int main(void)
{
    truck_specs_reset_defaults();

    test_countdown_then_attempt();
    test_full_attempt_passes();
    test_career_progression();
    test_all_rigs_and_scenarios_combine();

    if (failures) {
        printf("%d test(s) FAILED\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
