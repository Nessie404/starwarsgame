/*
 * WiiHaul — yards: the maneuvering courses a rig is scored against.
 *
 * A Yard is deliberately not a racetrack. It is a flat (mostly) lot
 * scattered with cones, boundary lines, parked obstacles and one target
 * zone, closely modeled on the maneuvers a real commercial driver's
 * license skills test actually asks for: straight-line backing, offset
 * backing left and right, parallel parking, alley docking, plus a
 * forward cone slalom and a couple of longer haul routes that combine
 * cornering with a final dock. Geometry is compiled in (yard_init), the
 * same way WiiKart's circuits are — see track_init in the sibling
 * project for the pattern this follows.
 *
 * Plain C99, no platform dependency, host-testable — see
 * tests/test_yard.c.
 */
#ifndef WIIHAUL_YARD_H
#define WIIHAUL_YARD_H

#include "truck.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Oriented bounding box on the ground plane: center, heading (radians,
 * same convention as Rig.heading), and half-extents along the forward
 * and lateral axes. Shared by the rig's own collision boxes and every
 * obstacle in a yard. */
typedef struct {
    float cx, cz;
    float heading;
    float half_fwd;
    float half_lat;
} OBB;

int obb_overlap(const OBB *a, const OBB *b);
int obb_circle_overlap(const OBB *a, float cx, float cz, float radius);
/* Signed distance from (x,z) to the box's own footprint: negative
 * inside, the gap to the nearest edge outside. Used for "did the rig
 * settle inside the target zone" checks. */
float obb_signed_distance(const OBB *b, float x, float z);

/* The tractor's own OBB, plus one per attached trailer, in that order.
 * Returns the count written (1 + spec->n_trailers). */
int rig_obbs(const Rig *r, const RigSpec *spec, OBB *out, int max_out);

#define YARD_MAX_OBSTACLES 24
#define YARD_MAX_CONES     40
#define YARD_MAX_BOUNDARIES 16
#define YARD_NAME_LEN 24
#define YARD_BLURB_LEN 96

enum {
    OBSTACLE_TRAILER  = 0,   /* another rig parked, blocking the space   */
    OBSTACLE_BUILDING = 1,
    OBSTACLE_CURB     = 2,
    OBSTACLE_DUMPSTER = 3,
    OBSTACLE_POLE     = 4,
    OBSTACLE_KIND_COUNT = 5
};

typedef struct {
    OBB   box;
    float height;     /* rendering only */
    int   kind;
} YardObstacle;

/* A boundary line is a "do not cross" — the alley-dock and offset-
 * backing lines a real skills test scores against. Touching one costs
 * points; it is not a wall, driving through is entirely possible and
 * exactly what should be scored. */
typedef struct {
    float ax, az, bx, bz;   /* segment endpoints                        */
    float nx, nz;           /* unit normal pointing to the legal side   */
} YardBoundary;

typedef struct {
    float cx, cz, heading;
    float half_fwd, half_lat;
    float heading_tolerance_deg;
    /* how long the rig must sit inside, stopped, within tolerance,
     * before the attempt is scored a pass */
    float hold_seconds;
} YardTarget;

enum { GRADE_NONE = 0, GRADE_RAMP = 1 };

/* A single straight grade band along the world X axis: flat below x0,
 * flat above x1, linear ramp from y0 to y1 in between — enough for one
 * hill-start scenario without a full elevation-profile system. */
typedef struct {
    int   kind;
    float x0, x1;
    float y0, y1;
} YardGrade;

enum {
    SCENARIO_STRAIGHT_BACK    = 0,
    SCENARIO_OFFSET_BACK_LEFT = 1,
    SCENARIO_OFFSET_BACK_RIGHT = 2,
    SCENARIO_PARALLEL_PARK    = 3,
    SCENARIO_ALLEY_DOCK       = 4,
    SCENARIO_SLALOM_PULL      = 5,
    SCENARIO_HILL_START       = 6,
    SCENARIO_HAUL_ROUTE       = 7,
    SCENARIO_DOUBLES_DOCK     = 8,
    SCENARIO_COUNT            = 9
};

typedef struct {
    char  name[YARD_NAME_LEN];
    char  blurb[YARD_BLURB_LEN];    /* one line, shown before the run   */
    int   id;

    float bounds_half_x, bounds_half_z; /* for the camera and minimap   */

    int          n_obstacles;
    YardObstacle obstacles[YARD_MAX_OBSTACLES];

    int   n_cones;
    float cone_x[YARD_MAX_CONES];
    float cone_z[YARD_MAX_CONES];

    int          n_boundaries;
    YardBoundary boundaries[YARD_MAX_BOUNDARIES];

    YardTarget target;
    YardGrade  grade;

    float start_x, start_z, start_heading;

    /* Real skills tests allow a handful of "pull-ups" (stopping and
     * pulling forward to reposition) before they start costing points;
     * beyond max_pullups_free each extra one costs a point. */
    int   max_pullups_free;
    float par_time_s;         /* informational, for a star rating        */
    int   difficulty_stars;   /* 1..4, menu display                      */
    int   min_trailers;       /* 1, or 2 to require a doubles combo       */
} Yard;

void yard_init(int scenario_id, Yard *y);
const char *yard_name(int scenario_id);

float yard_ground_at(const Yard *y, float x, float z);

/* ------------------------------------------------------------------ */
/* Attempt scoring                                                    */
/* ------------------------------------------------------------------ */

enum {
    ATTEMPT_IN_PROGRESS = 0,
    ATTEMPT_PASS        = 1,
    ATTEMPT_FAIL        = 2
};

enum {
    GRADE_FAIL_MARK = 0,
    GRADE_BRONZE    = 1,
    GRADE_SILVER    = 2,
    GRADE_GOLD      = 3
};

typedef struct {
    int   cone_hit[YARD_MAX_CONES];
    int   cone_hits;
    int   boundary_touching[YARD_MAX_BOUNDARIES]; /* edge-detect state   */
    int   boundary_touches;
    int   obstacle_hit;      /* 1 = solid collision, an automatic fail   */
    int   in_target;         /* 1 while every rig OBB sits in the zone,
                              * heading aligned, and the rig is stopped  */
    float in_target_t;
    int   result;            /* ATTEMPT_*                                */
    int   grade;             /* GRADE_* once result != IN_PROGRESS       */
    float elapsed_t;
    int   score;             /* 0..100, see yard_attempt_update           */
    /* one-frame event flags for the platform layer (HUD flash / beep)   */
    int   cone_event;
    int   boundary_event;
} YardAttempt;

void yard_attempt_reset(YardAttempt *a);

/* Advance the score/result for one frame. r/spec describe the rig right
 * now; call every frame regardless of Rig.speed so pull-ups and dwell
 * time inside the target are tracked correctly. Does nothing once
 * a->result is no longer ATTEMPT_IN_PROGRESS. */
void yard_attempt_update(YardAttempt *a, const Yard *y, const Rig *r,
                         const RigSpec *spec, float dt);

const char *yard_grade_name(int grade);

#ifdef __cplusplus
}
#endif

#endif /* WIIHAUL_YARD_H */
