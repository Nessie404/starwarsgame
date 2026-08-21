/*
 * WiiHaul — top-level game state.
 *
 * Ties the physics (truck.h), the course/scoring (yard.h) and the
 * camera (camera.h) together into one thing main.c can drive: pick a
 * rig and a scenario, run one attempt, grade it. Unlike WiiKart there
 * is no AI field and no split screen — this is a solo skills test, so
 * the whole thing is much flatter than a race. Plain C99, host-testable
 * — see tests/test_game.c.
 */
#ifndef WIIHAUL_GAME_H
#define WIIHAUL_GAME_H

#include "truck.h"
#include "yard.h"
#include "camera.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COUNTDOWN_SECONDS 2.5f

enum {
    STATE_COUNTDOWN = 0,
    STATE_ATTEMPT   = 1,
    STATE_FINISHED  = 2    /* attempt resolved; sim frozen, showing the
                            * result until the platform layer moves on   */
};

typedef struct {
    int scenario_id;   /* SCENARIO_*                                    */
    int rig_idx;        /* index into rig_specs                          */
    const HaulSettings *settings; /* NULL = compiled defaults            */
} GameConfig;

/*
 * Progress through the campaign: one best grade per scenario, and
 * whether it has been attempted at all. Scenario 0 is always unlocked;
 * each later one unlocks once the previous has been passed at least
 * once (see career_scenario_unlocked) — a straight line of skills
 * building on each other, the same shape as a real yard test. Lives
 * only as long as the process does, same limitation WiiKart's
 * CareerState has and for the same reason: no SD-card I/O here either.
 */
typedef struct {
    int attempted[SCENARIO_COUNT];
    int best_grade[SCENARIO_COUNT];   /* GRADE_*, GRADE_FAIL_MARK if none */
} CareerState;

void career_reset(CareerState *cs);
int  career_scenario_unlocked(const CareerState *cs, int scenario_id);
void career_record_attempt(CareerState *cs, int scenario_id, int grade);

typedef struct {
    Yard         yard;
    GameConfig   cfg;
    RigSpec      spec;        /* snapshot — see game_init                */
    HaulSettings settings;
    Rig          rig;
    CameraState  cam;
    YardAttempt  attempt;
    int          state;
    float        countdown;

    /* one-frame event flags for the platform layer (HUD flash / beep) */
    int   cone_event;
    int   boundary_event;
    int   jackknife_event;    /* just tripped this frame                 */
    int   pass_event;
    int   fail_event;
} Game;

void game_init(Game *g, const GameConfig *cfg);
void game_update(Game *g, const Input *in, float dt);

/* Ground height for the camera and rig_step, reading g->yard — matches
 * the GroundFn signature both expect. */
float game_ground_at(float x, float z, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* WIIHAUL_GAME_H */
