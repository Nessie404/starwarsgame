/*
 * WiiKart — an original kart/mountain racer for the Nintendo Wii (homebrew).
 *
 * This header and its companions game.c / track.c are platform-independent
 * plain C99: they contain the whole simulation (3D track geometry with
 * elevation, physically-based vehicle dynamics, AI, items, laps, ranking)
 * and depend only on libm. The Wii-specific rendering / input / audio
 * lives in main.c.
 *
 * Physics works in SI units: meters, seconds, kilograms. Vehicle
 * behaviour is derived from real-world performance parameters (power,
 * mass, 100-0 km/h stopping distance, lateral grip, drag area), with
 * gravity acting along the road grade.
 */
#ifndef WIIKART_GAME_H
#define WIIKART_GAME_H

#ifdef __cplusplus
extern "C" {
#endif

#define GRAVITY 9.81f

/* ------------------------------------------------------------------ */
/* Tracks                                                             */
/* ------------------------------------------------------------------ */

#define TRACK_MAX_POINTS 320
#define TRACK_MAX_ITEMS  6
#define TRACK_MAX_CORNERS 48

enum {
    TRACK_CLASSIC  = 0,   /* flat speedway with boost pads            */
    TRACK_BERTHOUD = 1,   /* stylized Berthoud Pass (US-40, Colorado) */
    TRACK_LOVELAND = 2,   /* stylized Loveland Pass (US-6, Colorado)  */
    TRACK_COUNT    = 3
};

typedef struct {
    int   id;
    const char *name;               /* 7-segment-safe uppercase          */
    int   n;                        /* number of centerline samples      */
    float px[TRACK_MAX_POINTS];     /* centerline (x, z ground plane)    */
    float pz[TRACK_MAX_POINTS];
    float py[TRACK_MAX_POINTS];     /* elevation                         */
    float dx[TRACK_MAX_POINTS];     /* unit forward (ground plane)       */
    float dz[TRACK_MAX_POINTS];
    float slope[TRACK_MAX_POINTS];  /* dy/dlen along direction of travel */
    float curv[TRACK_MAX_POINTS];   /* |curvature| 1/m, smoothed         */
    float seg_len[TRACK_MAX_POINTS];/* ground-plane segment length       */
    float total_len;
    float road_half;                /* half-width of the paved road      */
    float wall_half;                /* half-width to the hard barrier    */
    int   item_seg[TRACK_MAX_ITEMS];/* power-up panel rows               */
    int   n_items;
    int   alpine;                   /* 1 = mountain theme (rock skirts)  */

    /* Corners, found by walking the curvature profile. AI drivers learn
     * per corner rather than per sample, so "that hairpin" is a thing
     * they can remember between laps. */
    int   corner_id[TRACK_MAX_POINTS];   /* corner index, -1 = straight  */
    int   n_corners;
    float corner_peak[TRACK_MAX_CORNERS];/* peak curvature, 1/m         */
    int   corner_entry[TRACK_MAX_CORNERS];

    float min_x, max_x, min_z, max_z, min_y, max_y;
} Track;

void track_init(Track *t, int track_id);
const char *track_name(int track_id);

/* Locate (x,z) relative to the track. hint = last known segment
 * (searched +/- a small window); -1 searches everywhere. Outputs
 * nearest segment, fraction along it, signed lateral offset
 * (positive = right of travel) and surface elevation at that point. */
void track_locate(const Track *t, float x, float z, int hint,
                  int *seg, float *frac, float *lat, float *y);

int track_item_row(const Track *t, int seg);   /* -1 or item row index */

/* ------------------------------------------------------------------ */
/* Vehicle specs (real-world performance parameters)                  */
/* ------------------------------------------------------------------ */

#define SPEC_COUNT 4

typedef struct {
    const char *name;        /* 7-segment-safe                          */
    float mass_kg;           /* curb mass incl. driver                  */
    float power_hp;          /* engine power                            */
    float brake_dist_100;    /* stopping distance 100-0 km/h, meters    */
    float lat_g;             /* max lateral acceleration, in g          */
    float cd_a;              /* drag area Cd*A, m^2                     */
    float wheelbase;         /* m, sets steering geometry               */
    float offroad_grip;      /* fraction of grip/power kept off road    */
} KartSpec;

extern const KartSpec kart_specs[SPEC_COUNT];

/* ------------------------------------------------------------------ */
/* Race                                                               */
/* ------------------------------------------------------------------ */

#define MAX_HUMANS 4
#define NUM_KARTS  12
#define RACE_LAPS  3

/*
 * STEERING SIGN CONVENTION — read before touching any input code.
 *
 * The chase camera is built with guLookAt(eye, up=+Y, look) pointing
 * along the car's heading, so its right-hand axis is
 *
 *     right = cross(forward, up) = (-sin h, 0, cos h)
 *
 * which is exactly the lateral basis this simulation uses everywhere
 * (lx = -dz, lz = dx). Two consequences follow, and both are the
 * opposite of what the code used to claim:
 *
 *   - increasing `heading` turns the car toward the RIGHT of the screen,
 *     and the physics does `heading += f(steer)`, so
 *         steer > 0 steers RIGHT,  steer < 0 steers LEFT
 *   - positive `lat` is to the RIGHT of the direction of travel
 *
 * Input code must therefore map a "left" control to STEER_LEFT and a
 * "right" control to STEER_RIGHT instead of hand-writing signs. The host
 * test test_steer_sign() re-derives the camera's right axis and checks
 * the car actually moves that way, so an inverted stick cannot slip
 * through again.
 */
#define STEER_LEFT  (-1.0f)
#define STEER_RIGHT (+1.0f)

/*
 * Power-ups, kept inside what a real racing car can do rather than
 * borrowing from karting: a bounded engine overtake boost of the
 * push-to-pass kind, and a spell of fresh rubber that raises grip. Both
 * are collected from roadside panels, held in reserve, and deployed by
 * the driver — there are no projectiles, no floor boosters and no free
 * speed for sliding the car about.
 */
enum {
    POWER_NONE  = 0,
    POWER_PUSH  = 1,   /* push-to-pass: +PUSH_POWER engine for a while  */
    POWER_TIRES = 2,   /* fresh rubber: +TIRE_GRIP lateral grip         */
    POWER_TYPES = 2
};

#define PUSH_POWER    1.13f   /* ~+11%, in the region of IndyCar P2P    */
#define PUSH_SECONDS  4.0f
#define TIRE_GRIP     1.10f
#define TIRE_SECONDS  8.0f

const char *power_name(int power);

typedef struct {
    float steer;   /* -1..1; negative = left, positive = right          */
    int   accel;
    int   brake;
    int   hop;     /* handbrake / drift                                 */
    int   item;    /* use held item (edge-detected by the sim)          */
} Input;

/* ------------------------------------------------------------------ */
/* Steering feel                                                      */
/* ------------------------------------------------------------------ */

/*
 * A steering wheel has mass and a driver's hands have a speed limit, so
 * raw button presses are turned into a "virtual analog stick": the
 * command ramps toward the target instead of snapping, self-centers
 * faster than it winds on, slows down as speed rises, and passes
 * through a progressive curve so small inputs are gentle and full lock
 * still reaches full lock. Keyboards and D-pads therefore feel like a
 * real stick rather than an on/off switch.
 *
 * Analog sticks are fed through the same filter (their raw deflection
 * is the target), which keeps every device consistent and stops
 * flick-steering at speed.
 */
typedef struct {
    float value;      /* current wheel position, -1..1                 */
} SteerAxis;

void  steer_axis_reset(SteerAxis *a);
float steer_axis_update(SteerAxis *a, float target, float speed, float dt);

/* ------------------------------------------------------------------ */
/* AI drivers                                                         */
/* ------------------------------------------------------------------ */

/*
 * Every AI runs the same driving code but a different strategy sheet, so
 * the field behaves like a grid of people rather than one robot copied
 * eleven times: some brake late and scruff their tires, some flow wide
 * and carry exit speed, some hound your bumper waiting for a mistake.
 *
 * Strategies also decide how a driver learns. conf_start is how much of
 * the theoretical cornering limit it believes on lap one; a mistake
 * knocks that belief down by learn_down and a clean corner nudges it up
 * by learn_up, up to conf_max. A late-braker therefore starts over the
 * limit, makes a mess of a hairpin, and is measurably tidier next lap,
 * while a cruiser starts cautious and creeps up to a decent pace.
 */
enum {
    AI_BALANCED = 0,   /* textbook lines, average everything            */
    AI_LATE     = 1,   /* brakes far too late, then learns better       */
    AI_INSIDE   = 2,   /* hugs the inside, tight and defensive          */
    AI_DEFENDER = 3,   /* covers the line you like to pass on           */
    AI_CHARGER  = 4,   /* dives for overtakes, spends nitro at once     */
    AI_DRAFTER  = 5,   /* sits in your mirrors, saves nitro to pounce   */
    AI_CRUISER  = 6,   /* cautious, smooth, wide lines                  */
    AI_STRATEGY_COUNT  = 7
};

typedef struct {
    const char *name;      /* 7-segment-safe                           */
    float conf_start;      /* belief in the grip limit on lap one       */
    float conf_max;        /* ceiling once it has learned               */
    float learn_up;        /* gain per clean corner                     */
    float learn_down;      /* loss per botched corner                   */
    float line_bias;       /* preferred line, fraction of road half     */
    float defend;          /* 0..1 tendency to cover a chasing human    */
    float attack;          /* 0..1 tendency to dive for an overtake     */
    float power_wait;      /* seconds it holds a power-up before using  */
    float power;           /* engine trim                              */
} AIStrategy;

extern const AIStrategy ai_strategies[AI_STRATEGY_COUNT];

/*
 * What an AI has worked out about a human. Passes teach it which side
 * you like to come down (so defenders can cover it), your pace relative
 * to the field decides how hard the whole grid dares to push, and
 * contact teaches them to leave you more room.
 */
typedef struct {
    float pass_side;   /* -1 = you pass on their left, +1 = their right */
    float pace;        /* your progress rate vs the leading AI, ~1.0    */
    int   passes;      /* completed overtakes on AI cars                */
    int   contacts;    /* panel-rubbing incidents with AI cars          */
} PlayerModel;

typedef struct {
    /* pose */
    float x, z, y;
    float heading;        /* radians; ground dir = (cos h, sin h)       */
    float speed;          /* m/s along heading, >= small reverse        */
    float slip;           /* visual/audio slide amount 0..1             */
    float steer_vis;

    /* track relation */
    int   seg;
    float prog_raw;
    float total_progress;
    int   lap;
    float lat;            /* signed offset, + = right of travel        */

    /* driving state */
    int   spec;           /* index into kart_specs                      */
    int   drifting;       /* handbrake locked in, +1/-1 = direction     */
    float push_t;         /* push-to-pass seconds remaining             */
    float grip_t;         /* fresh-rubber seconds remaining             */
    int   power_held;     /* POWER_* currently in reserve               */
    int   prev_item_btn;

    /* role / livery */
    int   human;          /* -1 = AI, else human player index          */
    int   paint_idx;      /* index into the platform layer's palette   */
    float ai_line;        /* base racing-line offset, meters           */
    float ai_skill;
    float prev_progress;  /* last frame's progress, for pass detection */

    /* AI strategy and what it has learned */
    int   strategy;                          /* index into ai_strategies */
    float corner_conf[TRACK_MAX_CORNERS];    /* learned per-corner nerve */
    int   cur_corner;                        /* corner being taken, -1   */
    int   corner_fault;                      /* botched the current one  */
    int   learn_events;                      /* adaptations made so far  */
    int   mistakes;                          /* corners actually botched */
    float line_target;                       /* smoothed tactical line   */
    float power_timer;                       /* how long it has held one */

    /* results */
    int   rank;
    int   finished;
    float finish_time;
    int   final_rank;

    /* one-frame event flags for the platform layer */
    int   power_fired;    /* deployed a power-up this frame             */
    int   hit_wall;
    int   got_item;       /* collected a power-up this frame            */
} Kart;

enum {
    STATE_COUNTDOWN = 0,
    STATE_RACING    = 1,
    STATE_FINISHED  = 2   /* all humans done; sim keeps running        */
};

#define PAINT_COUNT 8

typedef struct {
    int track_id;
    int n_humans;                 /* 1..MAX_HUMANS                     */
    int spec[MAX_HUMANS];         /* chosen car spec per human         */
    int paint[MAX_HUMANS];        /* chosen paint index per human      */
} GameConfig;

typedef struct {
    Track track;
    GameConfig cfg;
    Kart  karts[NUM_KARTS];       /* karts[0..n_humans-1] are human    */
    float item_respawn[TRACK_MAX_ITEMS][3];  /* per row, 3 boxes across */
    PlayerModel pmodel[MAX_HUMANS];
    int   state;
    float countdown;
    float race_t;
    int   finish_count;
    int   humans_done;
} Game;

void game_init(Game *g, const GameConfig *cfg);
void game_update(Game *g, const Input inputs[MAX_HUMANS], float dt);

/* helpers shared with rendering / tests */
float game_angle_wrap(float a);
float game_clampf(float v, float lo, float hi);

/* AI helpers exposed for the HUD and the tests */
const char *ai_strategy_name(int strategy);
float       ai_corner_conf(const Kart *k, const Track *t, int seg);

/* derived stats for menus: 0-100 km/h time (s) and top speed (km/h) */
float spec_accel_time(const KartSpec *s);
float spec_top_speed(const KartSpec *s);

#ifdef __cplusplus
}
#endif

#endif /* WIIKART_GAME_H */
