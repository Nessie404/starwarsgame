/*
 * WiiHaul — tractor-trailer vehicle model.
 *
 * Plain C99, platform-independent (depends only on libm), so the whole
 * physics model runs in the host test suite exactly like WiiKart's
 * game.c/track.c do — see tests/test_truck.c and docs/HANDOFF.md.
 *
 * The model: a tractor is a standard bicycle-model car (steering angle,
 * wheelbase, rear-axle reference point) with an engine/gearbox scaled for
 * truck mass and torque. Each trailer behind it is an independent rigid
 * body connected by a pivoting hitch; its heading is governed by the
 * classic kinematic "car with N trailers" ODE (Altafini 2001 and
 * standard robotics motion-planning texts):
 *
 *     omega_i = (v_{i-1} / L_i) * sin(theta_{i-1} - theta_i)
 *             - (C_i / L_i) * cos(theta_{i-1} - theta_i) * omega_{i-1}
 *
 * where L_i is trailer i's hitch-to-axle length, C_i is the offset of
 * its hitch pivot behind unit (i-1)'s own reference point (the kingpin
 * setback on the tractor, or a converter dolly's tongue length between
 * two trailers), v_{i-1} is unit (i-1)'s own forward speed and
 * omega_{i-1} its yaw rate. Everything downstream of the tractor is a
 * deterministic geometric function of the tractor's own integrated
 * position and the chain of heading angles — see rig_step in truck.c —
 * so nothing but the tractor's reference point ever accumulates
 * integration drift.
 *
 * This one ODE is the entire reason backing a trailer feels the way it
 * does: it is not a special case bolted on for reverse, it falls straight
 * out of running the same equation with v < 0. Off-tracking on a forward
 * turn (the trailer cutting inside the tractor's own path) falls out of
 * it too. Nothing about "steer opposite when backing" is hand-coded
 * anywhere in this file.
 */
#ifndef WIIHAUL_TRUCK_H
#define WIIHAUL_TRUCK_H

#ifdef __cplusplus
extern "C" {
#endif

#define GRAVITY 9.81f

/*
 * STEERING SIGN CONVENTION — same as WiiKart's, and for the same reason:
 * the chase camera (camera.c) is built with guLookAt(eye, up=+Y, look)
 * along the rig's heading, so its right-hand axis is
 *
 *     right = cross(forward, up) = (-sin h, 0, cos h)
 *
 * which is exactly the lateral basis this simulation uses (lx = -dz,
 * lz = dx). So: increasing `heading` turns the tractor toward the RIGHT
 * of the screen, steer > 0 steers RIGHT, positive lateral offset is to
 * the RIGHT of the direction of travel. test_steer_sign() in
 * tests/test_truck.c re-derives the camera's right axis and checks the
 * rig actually moves that way.
 */
#define STEER_LEFT  (-1.0f)
#define STEER_RIGHT (+1.0f)

#define MAX_TRAILERS   2
#define MAX_GEARS      10
#define RIG_NAME_LEN   20
#define MAX_TRUCK_SPECS    8
#define MAX_TRAILER_SPECS 12
#define MAX_RIG_SPECS     16

/* ------------------------------------------------------------------ */
/* Specs (real-world-scaled performance parameters)                    */
/* ------------------------------------------------------------------ */

typedef struct {
    char  name[RIG_NAME_LEN];
    float mass_kg;          /* tractor unladen (bobtail) mass            */
    float power_hp;
    float wheelbase;        /* front axle to drive axle, m               */
    float body_length;      /* bumper to the back of the frame rail, m —
                             * for collision boxes and drawing, distinct
                             * from wheelbase (an axle-to-axle figure)    */
    float body_width;
    float hitch_setback;    /* drive axle to kingpin/5th wheel pivot, m —
                             * this is C_0 in the ODE above              */
    float max_steer_deg;    /* front wheel lock, each direction          */
    float reverse_top_mps;  /* reverse gear is a single low, torquey
                             * ratio in real trucks — one top speed is
                             * enough, no ladder needed                  */
    float brake_decel_ref;  /* m/s^2 at ref_mass_kg, full brakes         */
    float ref_mass_kg;      /* the mass brake_decel_ref was tuned at     */
    float cd_a;             /* drag area Cd*A, m^2                       */
    int   n_gears;
    float gear_top[MAX_GEARS]; /* m/s at the limiter, each gear          */
    float nominal_rpm;      /* fraction 0..1 of the way to redline where
                             * peak torque sits — diesels peak low        */
} TruckSpec;

enum {
    TRAILER_BOX     = 0,   /* dry van, the everyday rectangle            */
    TRAILER_FLATBED = 1,   /* open deck, stacked/strapped cargo          */
    TRAILER_TANKER  = 2,   /* liquid load that surges under braking      */
    TRAILER_LOWBOY  = 3,   /* low deck, an overhung oversize load        */
    TRAILER_PUP     = 4,   /* short trailer for a doubles combination    */
    TRAILER_TYPE_COUNT = 5
};
const char *trailer_type_name(int type);

typedef struct {
    char  name[RIG_NAME_LEN];
    int   type;              /* TRAILER_*                                */
    float length;            /* hitch pivot to axle group, m — L_i        */
    /* C_i: prior unit's reference point to THIS trailer's hitch pivot,
     * meters. Only meaningful when this trailer is chained behind
     * another trailer (the second slot of a doubles combo) — a converter
     * dolly's tongue length. When a trailer sits directly behind the
     * tractor (the first slot, always) the tractor's own
     * TruckSpec.hitch_setback plays this role instead (a trailer does
     * not know or care which tractor pulls it), and this field is
     * ignored — see rig_step. */
    float hitch_offset;
    float width;
    float height;
    float empty_mass_kg;
    float max_cargo_kg;
    float jackknife_limit_deg; /* structural limit on |theta_{i-1}-theta_i|
                                * before the trailer frame contacts the
                                * unit ahead of it                        */
    /* Tanker only: how hard an unfinished load surges the trailer's own
     * effective yaw when braking/accelerating hard — 0 for every other
     * type. See rig_step's slosh handling. */
    float slosh_strength;
} TrailerSpec;

typedef struct {
    char  name[RIG_NAME_LEN];
    int   truck_idx;               /* index into truck_specs             */
    int   n_trailers;               /* 1 or 2                             */
    int   trailer_idx[MAX_TRAILERS]; /* index into trailer_specs         */
    float cargo_mass_kg;            /* current load, added to each
                                     * trailer's empty mass pro-rated by
                                     * its own max_cargo_kg share         */
    /* Difficulty color, purely informational for menus (1 = easiest) */
    int   difficulty_stars;
} RigSpec;

extern TruckSpec   truck_specs[MAX_TRUCK_SPECS];
extern int         truck_spec_count;
extern TrailerSpec trailer_specs[MAX_TRAILER_SPECS];
extern int         trailer_spec_count;
extern RigSpec      rig_specs[MAX_RIG_SPECS];
extern int          rig_spec_count;

void truck_specs_reset_defaults(void);

/* Combined mass of a rig instance: tractor + every attached trailer,
 * empty + its share of cargo_mass_kg. */
float rig_spec_total_mass(const RigSpec *rs);

/* Overall nose-to-tail length, tractor bumper to the last trailer's
 * tail — for camera framing and menu display. */
float rig_spec_total_length(const RigSpec *rs);

/* ------------------------------------------------------------------ */
/* Tunable simulation settings                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    /* steering feel: how fast the wheel winds on / self-centers, deg/s,
     * and how much that rate fades with road speed */
    float steer_rate_on_dps;
    float steer_rate_center_dps;
    float steer_speed_fade;

    /* engine / drivetrain */
    float rolling_resistance;
    float drivetrain_efficiency;
    float shift_seconds;

    /* braking: brake_decel_ref scales by (ref_mass / total_mass), same
     * shape as a real air-brake system losing stopping performance to
     * load; this floor keeps a fully loaded rig from taking forever */
    float brake_mass_floor;

    /* jackknife: once |articulation| passes limit_warn_frac of a joint's
     * jackknife_limit_deg the HUD gauge turns amber, past
     * limit_danger_frac it turns red; at 1.0 the joint locks           */
    float jackknife_warn_frac;
    float jackknife_danger_frac;

    /* tanker slosh: how quickly the liquid's lateral shift chases the
     * lateral acceleration driving it, and how much yaw it feeds back */
    float slosh_response;      /* fraction of the gap closed per second   */
    float slosh_yaw_feedback;  /* rad/s^2 per unit of slosh offset, scaled
                                * by slosh_strength                       */

    /* chase camera — see camera.c */
    float cam_distance_base_m;   /* plus a per-meter-of-rig-length term   */
    float cam_distance_per_rig_length;
    float cam_height_m;
    float cam_min_height_m;
    float cam_look_height_m;
    float cam_follow_smoothing;
    float cam_look_smoothing;
    float cam_reverse_deadzone_mps;
    float cam_reverse_full_mps;
    float cam_reverse_orbit_rate_dps;
    float cam_reverse_smoothing;
    float cam_reverse_tail_bias;  /* 0 = aim stays on the tractor, 1 = aim
                                   * is pulled all the way to the last
                                   * trailer's tail while backing         */
    float cam_corner_lean;
} HaulSettings;

void haul_settings_defaults(HaulSettings *s);

/* ------------------------------------------------------------------ */
/* Rig instance (dynamic state)                                       */
/* ------------------------------------------------------------------ */

/*
 * accel and reverse are two separate throttles, not one pedal that
 * doubles up once you're stopped: a game whose entire point is
 * sustained backing has to let the player hold reverse and steer for as
 * long as a maneuver takes, the way accel already works forward. brake
 * is a third, independent input that always decelerates whichever way
 * the rig currently happens to be moving (and holds it against a grade
 * with parking_brake) — it never by itself creates motion. Real backing
 * physics does not care which button put speed below zero, only that it
 * is negative — see rig_step.
 */
typedef struct {
    float steer;   /* -1..1; negative = left, positive = right          */
    int   accel;
    int   reverse;
    int   brake;
    int   parking_brake;
    int   gear_up;
    int   gear_down;
    int   horn;
} Input;

enum { GEARBOX_AUTO = 0, GEARBOX_MANUAL = 1 };

typedef struct {
    /* tractor pose — this is the only position state that is actually
     * integrated; every trailer's world position is derived from this
     * plus the heading chain (see rig_step) */
    float x, z, y;
    float heading;
    float speed;          /* m/s, signed: >0 forward, <0 reverse         */

    float steer_cmd;       /* smoothed wheel position, -1..1              */
    float steer_angle;     /* radians, derived from steer_cmd             */

    int   gear;
    float shift_t;
    int   gearbox;
    float rev_frac;
    int   prev_up_btn, prev_down_btn;  /* edge-detect manual shifts       */

    int   n_trailers;
    float trailer_heading[MAX_TRAILERS];
    float trailer_omega[MAX_TRAILERS];

    /* jackknife: once tripped the rig is held at zero drive until the
     * player eases forward and straightens out past the recovery
     * threshold (see rig_step) */
    int   jackknifed;
    int   jackknife_joint;    /* which joint tripped it, -1 if none      */

    /* tanker liquid surge, -1..1 lateral shift; 0 for non-tankers */
    float slosh;

    /* scoring/telemetry, reset per attempt by the caller */
    int   pullups;         /* direction reversals (a "pull-up" in a real
                            * skills test)                                */
    int   prev_dir;        /* -1/0/+1, last nonzero speed's sign          */
    float odometer_m;      /* distance driven this attempt                */
    float sim_t;           /* seconds since the attempt started           */

    int   stalled_on_grade; /* one-frame flag: the rig is at a standstill
                             * on a real grade with neither pedal nor the
                             * parking brake holding it, so it is about
                             * to roll — see rig_step                    */
} Rig;

/* Articulation angle at joint i (0 = tractor-to-first-trailer), radians,
 * wrapped to (-pi, pi]. Positive means the trailer has swung to the
 * right of the unit pulling it. */
float rig_articulation(const Rig *r, int joint);

/* World position of trailer i's own axle-group reference point (i.e.
 * the point trailer_heading[i] pivots around at the far end) — purely
 * geometric, see the file comment. */
void rig_trailer_pos(const Rig *r, const RigSpec *spec, int i,
                     float *x, float *z);

/* World position of the hitch pivot immediately behind unit i (i = -1
 * is the tractor's own kingpin, i.e. the pivot trailer 0 hangs off). */
void rig_hitch_pos(const Rig *r, const RigSpec *spec, int i,
                   float *x, float *z);

void rig_reset(Rig *r, const RigSpec *spec, float x, float z, float y,
              float heading);

/* Advance one frame. ground_y, if non-NULL, is asked for the terrain
 * height under the tractor's own reference point (x,z) so the rig can
 * sit on a grade; NULL keeps y at 0 (a flat yard). */
typedef float (*GroundFn)(float x, float z, void *ctx);

void rig_step(Rig *r, const RigSpec *spec, const Input *in,
             const HaulSettings *settings, float dt,
             GroundFn ground_fn, void *ground_ctx);

float haul_angle_wrap(float a);
float haul_clampf(float v, float lo, float hi);

/* Engine torque-curve shape: multiplier 0..1 at rev_frac (0..1, this
 * gear's own fraction toward its limiter) given where nominal_frac
 * (TruckSpec.nominal_rpm) puts the torque peak. Diesels are torquier and
 * flatter off-peak than the shape WiiKart's cars use, but the falling-
 * off-both-ends idea is the same and it is exposed for the same reason:
 * the HUD tachometer and the tests read the identical curve the engine
 * itself drives from. */
float truck_power_scale(float rev_frac, float nominal_frac);

/* Derived stats for menus. */
float rig_spec_top_speed_mps(const RigSpec *rs);

#ifdef __cplusplus
}
#endif

#endif /* WIIHAUL_TRUCK_H */
