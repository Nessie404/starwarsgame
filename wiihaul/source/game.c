/* WiiHaul top-level game state. Plain C99. */
#include <string.h>
#include "game.h"

void career_reset(CareerState *cs)
{
    int i;
    memset(cs, 0, sizeof(*cs));
    for (i = 0; i < SCENARIO_COUNT; i++)
        cs->best_grade[i] = GRADE_FAIL_MARK;
}

int career_scenario_unlocked(const CareerState *cs, int scenario_id)
{
    if (scenario_id <= 0) return 1;
    if (scenario_id >= SCENARIO_COUNT) return 0;
    return cs->best_grade[scenario_id - 1] >= GRADE_BRONZE;
}

void career_record_attempt(CareerState *cs, int scenario_id, int grade)
{
    if (scenario_id < 0 || scenario_id >= SCENARIO_COUNT) return;
    cs->attempted[scenario_id] = 1;
    if (grade > cs->best_grade[scenario_id])
        cs->best_grade[scenario_id] = grade;
}

float game_ground_at(float x, float z, void *ctx)
{
    return yard_ground_at((const Yard *)ctx, x, z);
}

void game_init(Game *g, const GameConfig *cfg)
{
    memset(g, 0, sizeof(*g));
    g->cfg = *cfg;
    if (cfg->settings)
        g->settings = *cfg->settings;
    else
        haul_settings_defaults(&g->settings);

    yard_init(cfg->scenario_id, &g->yard);
    g->spec = rig_specs[cfg->rig_idx];

    rig_reset(&g->rig, &g->spec, g->yard.start_x, g->yard.start_z,
             game_ground_at(g->yard.start_x, g->yard.start_z, &g->yard),
             g->yard.start_heading);
    camera_reset(&g->cam, &g->settings, &g->rig, &g->spec, game_ground_at,
                &g->yard);
    yard_attempt_reset(&g->attempt);

    g->state = STATE_COUNTDOWN;
    g->countdown = COUNTDOWN_SECONDS;
}

void game_update(Game *g, const Input *in, float dt)
{
    int was_jackknifed;

    g->cone_event = 0;
    g->boundary_event = 0;
    g->jackknife_event = 0;
    g->pass_event = 0;
    g->fail_event = 0;

    if (g->state == STATE_COUNTDOWN) {
        g->countdown -= dt;
        camera_update(&g->cam, &g->settings, &g->rig, &g->spec,
                     game_ground_at, &g->yard, dt);
        if (g->countdown <= 0.0f)
            g->state = STATE_ATTEMPT;
        return;
    }

    if (g->state == STATE_ATTEMPT) {
        was_jackknifed = g->rig.jackknifed;
        rig_step(&g->rig, &g->spec, in, &g->settings, dt, game_ground_at,
                &g->yard);
        if (g->rig.jackknifed && !was_jackknifed)
            g->jackknife_event = 1;

        yard_attempt_update(&g->attempt, &g->yard, &g->rig, &g->spec, dt);
        camera_update(&g->cam, &g->settings, &g->rig, &g->spec,
                     game_ground_at, &g->yard, dt);

        g->cone_event = g->attempt.cone_event;
        g->boundary_event = g->attempt.boundary_event;

        if (g->attempt.result == ATTEMPT_PASS) {
            g->state = STATE_FINISHED;
            g->pass_event = 1;
        } else if (g->attempt.result == ATTEMPT_FAIL) {
            g->state = STATE_FINISHED;
            g->fail_event = 1;
        }
        return;
    }

    /* STATE_FINISHED: sim is done, but the camera keeps easing so the
     * result screen isn't looking at a frozen, jittery frame */
    camera_update(&g->cam, &g->settings, &g->rig, &g->spec, game_ground_at,
                 &g->yard, dt);
}
