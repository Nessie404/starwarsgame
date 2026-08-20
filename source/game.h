/*
 * WiiKart — an original kart/mountain racer for the Nintendo Wii (homebrew).
 *
 * This header and its companions game.c / track.c are platform-independent
 * plain C99: they contain the whole simulation (3D track geometry with
 * elevation, physically-based vehicle dynamics, AI, laps, ranking) and
 * depend only on libm. The Wii-specific rendering / input / audio lives
 * in main.c.
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

typedef struct GameSettings GameSettings;

/* ------------------------------------------------------------------ */
/* Tracks                                                             */
/* ------------------------------------------------------------------ */

#define TRACK_MAX_POINTS 440
#define TRACK_MAX_CORNERS 72
#define TRACK_MAX_WEATHER_ZONES 8
#define TRACK_MAX_CHECKPOINTS 40
#define CHECKPOINT_SPACING 12      /* samples between checkpoints      */

enum {
    TRACK_CLASSIC  = 0,   /* flat speedway, barriered                 */
    TRACK_BERTHOUD = 1,   /* stylized Berthoud Pass (US-40, Colorado) */
    TRACK_LOVELAND = 2,   /* stylized Loveland Pass (US-6)            */
    TRACK_KENOSHA  = 3,   /* stylized Kenosha Pass (US-285), long/wide */
    TRACK_MONARCH  = 4,   /* stylized Monarch Pass (US-50), the hardest */
    TRACK_BREAKNECK = 5,  /* short, steep, abrupt, no barriers          */
    TRACK_GUANELLA = 6,   /* stylized Guanella Pass, switchback after
                           * switchback, unguarded                      */
    TRACK_BERTHOUD2 = 7,  /* Berthoud Pass, drawn from its own elevation
                           * profile: a stack of hairpins up one side, a
                           * summit, a second stack down the other, a
                           * valley loop-back, and a gentle climb home   */
    TRACK_COUNT    = 8
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
    float curv_signed[TRACK_MAX_POINTS]; /* curvature 1/m, + = bends
                                          * toward positive lat (right),
                                          * same smoothing window as curv */
    float seg_len[TRACK_MAX_POINTS];/* ground-plane segment length       */
    float total_len;
    float road_half;                /* nominal half-width, paved road    */
    float wall_half;                /* nominal half-width to the barrier */
    /*
     * Width varies along the lap. A hairpin can stay narrow and punishing
     * while another tight corner is opened out enough to hold two lines,
     * which is a road-design decision rather than something curvature
     * should decide on its own. The scalars above are the mean, kept for
     * menus and anything that only wants one number; anything that knows
     * which piece of road it is on asks for that piece.
     */
    float road_half_seg[TRACK_MAX_POINTS];
    float wall_half_seg[TRACK_MAX_POINTS];
    int   alpine;                   /* 1 = mountain theme (rock skirts)  */
    int   has_walls;                /* 0 = unguarded drop off the edge   */
    int   laps;                     /* race distance, set from length    */

    /* Checkpoints, for putting a car that went over the edge back on the
     * road at the last one it passed. */
    int   checkpoint_seg[TRACK_MAX_CHECKPOINTS];
    int   n_checkpoints;

    /* Corners, found by walking the curvature profile. AI drivers learn
     * per corner rather than per sample, so "that hairpin" is a thing
     * they can remember between laps. */
    int   corner_id[TRACK_MAX_POINTS];   /* corner index, -1 = straight  */
    int   n_corners;
    float corner_peak[TRACK_MAX_CORNERS];/* peak curvature, 1/m         */
    int   corner_entry[TRACK_MAX_CORNERS];

    float min_x, max_x, min_z, max_z, min_y, max_y;

    /* Weather zones (CLASSIC only — see track_init). weather_zone[i] is
     * which zone sample i sits in, or -1 for bare pavement. Each zone
     * starts as snow at the green flag and melts through ice into a
     * puddle at its own pace, offset by weather_zone_offset so patches
     * don't all turn over in lockstep; track_weather_at resolves a
     * segment + race clock into the weather actually under the car. */
    int   weather_zone[TRACK_MAX_POINTS];
    int   n_weather_zones;
    float weather_zone_offset[TRACK_MAX_WEATHER_ZONES];
} Track;

void track_init(Track *t, int track_id);
void track_init_with_settings(Track *t, int track_id,
                              const GameSettings *settings);
const char *track_name(int track_id);

/* Locate (x,z) relative to the track. hint = last known segment
 * (searched +/- a small window); -1 searches everywhere. Outputs
 * nearest segment, fraction along it, signed lateral offset
 * (positive = right of travel) and surface elevation at that point. */
void track_locate(const Track *t, float x, float z, int hint,
                  int *seg, float *frac, float *lat, float *y);

/* half-width of the road, and of the barrier line, at one segment */
float track_road_half(const Track *t, int seg);
float track_wall_half(const Track *t, int seg);

/* current weather (WEATHER_CLEAR..WEATHER_PUDDLE) at one segment, given
 * how long the race clock has been running. settings may be NULL, which
 * uses the same snow/ice/puddle timings as game_settings_defaults(). */
int track_weather_at(const Track *t, int seg, float race_t,
                     const GameSettings *settings);

/* ------------------------------------------------------------------ */
/* Vehicle specs (real-world performance parameters)                  */
/* ------------------------------------------------------------------ */

#define DEFAULT_SPEC_COUNT 11
/* Kept as the built-in roster size for old tests and source users. Runtime
 * code must use kart_spec_count: cars.json can grow the garage. The
 * compiled-in roster mirrors the shipped cars.json exactly, so the full
 * garage is there even with no filesystem to read the JSON from (a DOL
 * opened directly in Dolphin with no virtual SD card, for instance) —
 * see HANDOFF.md for why that used to leave most of the garage empty. */
#define SPEC_COUNT DEFAULT_SPEC_COUNT
#define MAX_KART_SPECS 16
#define KART_NAME_LEN   16
#define MAX_GEARS       6

/*
 * Gearing. Each car has a gearbox whose ratios are expressed as the road
 * speed at which that gear hits the rev limiter. Engine output then
 * depends on where in the gear you are: bogging below the torque band
 * costs power, so does bouncing off the limiter, and the limiter itself
 * caps speed until you shift up. Shifting takes a moment during which
 * drive is cut, so short-shifting a hairpin exit is a real decision.
 */
typedef struct {
    char  name[KART_NAME_LEN]; /* short uppercase garage label          */
    float mass_kg;           /* curb mass incl. driver                  */
    float power_hp;          /* engine power                            */
    float brake_dist_100;    /* stopping distance 100-0 km/h, meters    */
    float lat_g;             /* max lateral acceleration, in g          */
    float cd_a;              /* drag area Cd*A, m^2                     */
    float wheelbase;         /* m, sets steering geometry               */
    float offroad_grip;      /* fraction of grip/power kept off road    */
    /*
     * Which axle(s) put power down, set from an optional "drivetrain"
     * block in cars.json (config.c); a car with no block is RWD. The
     * driven axle spends some of its own grip on traction rather than
     * cornering: a front-driven car understeers under power (the same
     * tires steer and drive), a rear-driven one gets looser and easier
     * to rotate instead (kart_step). All-wheel drive splits the demand
     * across both axles, which both softens that trade-off in whichever
     * direction awd_front_bias leans and leaves more of the grip circle
     * free for accelerating without breaking traction in the first
     * place — see DRIVETRAIN_* below.
     */
    int   drivetrain;
    float awd_front_bias;    /* AWD only: 0 = rear-biased .. 1 = front-biased */
    int   n_gears;
    float gear_top[MAX_GEARS];  /* m/s at the limiter in each gear      */
    /*
     * Where the automatic box changes gear, as a fraction of the gear it
     * is in. One pair per gear, so a car can short-shift out of first and
     * hold second to the limiter; cars.json may set them per car or per
     * gear, and anything it leaves out keeps the defaults.
     */
    float auto_up[MAX_GEARS];
    float auto_down[MAX_GEARS];
} KartSpec;

enum { DRIVETRAIN_FWD = 0, DRIVETRAIN_RWD = 1, DRIVETRAIN_AWD = 2 };

#define COOLDOWN_SECONDS 11.0f /* slowing-down lap: flag to a standstill */
#define SHIFT_TIME    0.18f   /* seconds of cut drive while shifting    */
#define AUTO_UP_FRAC   0.95f  /* default automatic upshift point        */
#define AUTO_DOWN_FRAC 0.38f  /* default automatic downshift point      */
#define BOG_FRACTION  0.34f   /* below this much of the gear, it bogs   */

/* engine output multiplier for being at `frac` of the current gear's
 * band; shared by the simulation, the AI and the tests */
float gear_power_scale(float frac);

/* customisation the player picks in the garage */
enum { GEARBOX_AUTO = 0, GEARBOX_MANUAL = 1, GEARBOX_MODES = 2 };
enum { TIRE_MEDIUM = 0, TIRE_SOFT = 1, TIRE_HARD = 2, TIRE_COMPOUNDS = 3 };

const char *gearbox_name(int mode);
const char *tire_name(int compound);
float tire_grip_mult(int compound);
float tire_drag_mult(int compound);

/*
 * Weather: patches of the road surface that start as snow, melt into
 * ice, and finally melt again into a puddle, each stage handing a
 * different tire compound the advantage — see track_weather_at and
 * weather_tire_grip_mult in game.c. Only CLASSIC carries any zones;
 * every other track's weather_zone entries stay -1 (clear) forever.
 */
enum { WEATHER_CLEAR = 0, WEATHER_SNOW = 1, WEATHER_ICE = 2,
       WEATHER_PUDDLE = 3 };
const char *weather_name(int weather);
float weather_tire_grip_mult(const GameSettings *settings, int weather,
                             int compound);

extern KartSpec kart_specs[MAX_KART_SPECS];
extern int kart_spec_count;
void kart_specs_reset_defaults(void);

/* fill in any shift point a car did not specify */
void kart_spec_default_shifts(KartSpec *s);

/* ------------------------------------------------------------------ */
/* Race                                                               */
/* ------------------------------------------------------------------ */

#define MAX_HUMANS 4
#define NUM_KARTS  12
/* Fallback lap count; each circuit sets its own in Track.laps so that a
 * 2 km mountain pass is not the same number of laps as a 555 m speedway
 * — races then come out at a similar length whichever you pick. */
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

/* ------------------------------------------------------------------ */
/* User-tunable simulation settings                                   */
/* ------------------------------------------------------------------ */

/* Defaults match the values shipped in config/settings.json. A GameConfig
 * may point at an edited copy; game_init() snapshots it so a running race
 * cannot change underneath itself. */
struct GameSettings {
    /* race and circuit construction */
    float countdown_seconds;
    float target_race_distance_m;
    int   min_laps;
    int   max_laps;
    float track_width_mult[TRACK_COUNT];
    float track_scale_mult[TRACK_COUNT];
    float track_elevation_mult[TRACK_COUNT];
    int   track_laps[TRACK_COUNT];       /* 0 = choose from distance */

    /* vehicle model and steering */
    float rolling_resistance;
    float drivetrain_efficiency;
    float shift_seconds;
    float bog_fraction;
    float steer_rate_on;
    float steer_rate_center;
    float steer_speed_fade;
    float steer_curve;
    float tire_grip_mult[TIRE_COMPOUNDS];      /* peak grip, when warm    */
    float tire_drag_mult[TIRE_COMPOUNDS];      /* aero/rolling penalty    */
    /*
     * Tires as something that happens over a race rather than a label.
     * A compound has a temperature it wants to be at and a window either
     * side of it, heats up with work and cools with speed, and wears out
     * — losing grip as it goes. The soft compound is quickest when it is
     * in its window and fresh, which is not the whole race.
     */
    float tire_rolling_mult[TIRE_COMPOUNDS];   /* rolling resistance      */
    float tire_wear_rate[TIRE_COMPOUNDS];      /* wear per second of work */
    float tire_wear_grip_loss[TIRE_COMPOUNDS]; /* grip lost when worn out */
    float tire_temp_optimal[TIRE_COMPOUNDS];   /* degrees C               */
    float tire_temp_window[TIRE_COMPOUNDS];    /* half-width, degrees     */
    float tire_heat_rate[TIRE_COMPOUNDS];      /* degrees per second of work */
    float tire_cool_rate[TIRE_COMPOUNDS];      /* fraction of the gap per s */
    float tire_off_window_grip[TIRE_COMPOUNDS];/* grip well outside it    */
    float tire_ambient_c;

    /*
     * Weather (CLASSIC only). A snow patch is fresh footing for the soft
     * compound and treacherous for the hard one; by the time it has
     * melted into a puddle that relationship has flipped. The medium
     * compound is never the best or the worst tire for any of it.
     */
    float weather_snow_to_ice_s;
    float weather_ice_to_puddle_s;
    float weather_snow_grip[TIRE_COMPOUNDS];
    float weather_ice_grip[TIRE_COMPOUNDS];
    float weather_puddle_grip[TIRE_COMPOUNDS];

    /*
     * Boost: a meter that fills while the engine is turning fast, along
     * a curve rather than a flat rate, spent all at once on the use
     * button for an instant speed bump, and wiped out by the next shift
     * — so working it means holding a gear near the limiter on purpose
     * instead of just driving normally.
     */
    float boost_build_rate;        /* meter/second at redline (curve=1) */
    float boost_max_speed_bonus_mps; /* bonus speed at a full meter     */

    /* AI. overcommit_chance is checked once on each sufficiently tight,
     * unguarded corner and is scaled by the strategy's attack rating. */
    float ai_skill_mult;
    float ai_brake_mult;
    float ai_unguarded_line_room;
    float ai_overcommit_chance;
    float ai_overcommit_min_curvature;
    float ai_overcommit_seconds;
    float ai_overcommit_overshoot_m;

    /*
     * Chase camera. Distances are meters, rates per second, and every
     * "smoothing" is the fraction of the remaining error still left one
     * second later (small = quick, 0 = snap).
     */
    float cam_distance_m;
    float cam_height_m;
    float cam_min_height_m;          /* clearance over the ground below  */
    float cam_look_ahead_m;
    float cam_look_ahead_per_mps;    /* extra look-ahead with speed      */
    float cam_look_ahead_max_m;
    float cam_look_height_m;
    float cam_look_min_height_m;     /* keeps the aim off the pavement   */
    float cam_follow_smoothing;
    float cam_look_smoothing;
    float cam_reverse_deadzone_mps;  /* below this, reversing is ignored */
    float cam_reverse_full_mps;      /* fully swung round by this speed  */
    float cam_reverse_orbit_rate_dps;
    float cam_reverse_smoothing;
    float cam_pitch_influence;       /* 0 = level, 1 = follows the road  */
    float cam_pitch_smoothing;
    float cam_pitch_min_deg;         /* negative = allowed to look up    */
    float cam_pitch_max_deg;

    /* hills. The gravity component along a road at angle theta is
     * g*sin(theta) and the load pressing the tires down is m*g*cos(theta);
     * both multipliers are here so a circuit can be made to feel more
     * mountainous than it is without breaking the model. */
    float grade_gravity_mult;
    float grade_load_effect;     /* 0 = ignore load loss, 1 = full cos  */

    /* instruments: the sim has no crankshaft, so the tachometer maps
     * where the car is in its gear onto a readable rev range */
    float tacho_idle_rpm;
    float tacho_redline_rpm;

    /* cliff recovery */
    float fall_seconds;
    float respawn_black_seconds;
    float respawn_fade_seconds;
    float invincible_seconds;
    float invincible_flash_hz;

    /* the wrong-way marshal: how long a human can drive back down the
     * circuit before the helicopter arrives, and how much engine it
     * leaves them once it has */
    float wrong_way_seconds;
    float wrong_way_power;
};

void game_settings_defaults(GameSettings *s);
int  game_settings_validate(GameSettings *s, char *error, int error_cap);

float tire_grip_mult_with_settings(const GameSettings *settings,
                                   int compound);

/*
 * What a tire is worth right now, given its compound, how hot it is and
 * how worn: peak grip scaled by both. Exposed so the HUD and the tests
 * can ask the same question the simulation does.
 */
float tire_condition_grip(const GameSettings *settings, int compound,
                          float temp_c, float wear);
float tire_drag_mult_with_settings(const GameSettings *settings,
                                   int compound);

typedef struct {
    float steer;     /* -1..1; negative = left, positive = right        */
    int   accel;
    int   brake;
    int   hop;       /* handbrake                                       */
    int   gear_up;   /* upshift  (edge-detected by the sim)             */
    int   gear_down; /* downshift                                       */
    int   boost;     /* "use": spend the boost meter (edge-detected)    */
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
float steer_axis_update_with_settings(SteerAxis *a, float target,
                                      float speed, float dt,
                                      const GameSettings *settings);

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
    AI_CHARGER  = 4,   /* dives for every gap, brakes late               */
    AI_DRAFTER  = 5,   /* sits in your mirrors, waits to pounce          */
    AI_CRUISER  = 6,   /* cautious, smooth, wide lines                  */
    AI_YOLO     = 7,   /* no guts, no glory — commits to everything,
                        * rarely defends, and rides every gear to the
                        * limiter regardless of the cost               */
    AI_STRATEGY_COUNT  = 8
};

typedef struct {
    const char *name;      /* 7-segment-safe                           */
    float conf_start;      /* belief in the grip limit on lap one       */
    float conf_max;        /* ceiling once it has learned               */
    float learn_up;        /* gain per clean corner                     */
    float learn_down;      /* loss per botched corner                   */
    float line_bias;       /* apex commitment, 0 = stays near centerline
                            * through a corner, 1 = clips it hard; never
                            * negative — see ai_tactical_line             */
    float defend;          /* 0..1 tendency to cover a chasing human    */
    float attack;          /* 0..1 tendency to dive for an overtake     */
    float power;           /* engine trim                              */
    /* Shifting style: where in the rev band this driver changes up, how
     * early it grabs a lower gear on the way into a corner, and how
     * crisply it does it. A short-shifter rides the torque; a driver who
     * hangs on to the limiter gets the top end but loses time shifting. */
    float shift_up_frac;   /* fraction of the gear band to upshift at   */
    float shift_down_frac; /* below this fraction, take a lower gear    */
    float shift_delay;     /* extra reaction time before shifting, s    */
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
    int   gear;           /* 0-based index into the spec's gears        */
    float shift_t;        /* seconds of shift left (drive cut)          */
    int   gearbox;        /* GEARBOX_AUTO or GEARBOX_MANUAL             */
    int   tire;           /* TIRE_* compound                            */
    float rev_frac;       /* 0..1+ position in the current gear band    */
    int   prev_up_btn, prev_down_btn;

    /* boost: builds with revs, spent all at once on the use button,
     * wiped by the next shift — see kart_step */
    float boost_meter;    /* 0..1                                       */
    int   prev_boost_btn;

    /* going over the edge, and getting put back on the road           */
    int   last_checkpoint;
    float fall_t;         /* >0 while falling off an unguarded edge     */
    float respawn_t;      /* black hold + fade; car is frozen           */
    float invincible_t;   /* contact immunity after the recovery        */
    int   respawned;      /* one-frame flag for the platform layer      */
    int   falls;          /* completed cliff falls (AI telemetry/tests) */
    int   drifting;       /* handbrake locked in, +1/-1 = direction     */
    float tire_wear;      /* 0 = fresh, 1 = worn out                    */
    float tire_temp;      /* degrees C                                  */
    float tire_grip_now;  /* what the rubber is actually worth, 0..1+   */

    /* role / livery */
    int   human;          /* -1 = AI, else human player index          */
    int   driver_no;      /* AI grid slot, for ai_driver()              */
    float tire_care;      /* driver's effect on tire wear, 1 = neutral  */
    float consistency;    /* 0 ragged .. 1 never puts a wheel wrong     */
    float aggression;     /* 0 follows .. 1 dives up the inside         */
    int   paint_idx;      /* index into the platform layer's palette   */
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
    float overcommit_t;                      /* deliberate AI overreach  */
    float overcommit_line;                   /* risky outside line, m    */
    int   risk_corner;                       /* last corner risk-tested  */
    int   overcommits;                       /* times they went for it   */
    unsigned int rng_state;                  /* deterministic local PRNG */

    /* timing: every driver's own stopwatch, humans and AI alike */
    float lap_start_t;    /* race clock when the current lap began      */
    float last_lap_time;  /* the lap just completed, 0 if none yet      */
    float best_lap_time;  /* fastest so far, 0 if none yet              */
    int   laps_done;      /* completed laps, counted at the line        */
    int   lap_event;      /* one-frame: crossed the line                */
    int   lap_best_event; /* one-frame: ...and it was a personal best   */

    /* the slowing-down lap after the flag */
    float cooldown_t;     /* seconds since the chequered flag           */
    float cooldown_v0;    /* speed it crossed the line at               */
    int   parked;         /* stopped after the flag, holding on the brake */

    /*
     * Going the wrong way. `wrong_way_t` counts how long the car has been
     * pointing back down the circuit; past a moment of it a marshal
     * helicopter drops in, and the engine is cut until the car turns
     * round — which is what `wrong_way_power` is for.
     */
    float wrong_way_t;
    int   wrong_way;      /* 1 once the helicopter is overhead           */

    /* results */
    int   rank;
    int   finished;
    float finish_time;
    int   final_rank;

    /* one-frame event flags for the platform layer */
    int   hit_wall;
} Kart;

enum {
    STATE_COUNTDOWN = 0,
    STATE_RACING    = 1,
    STATE_FINISHED  = 2   /* all humans done; sim keeps running        */
};

#define PAINT_COUNT 8

/*
 * Difficulty preset — SCAFFOLDING ONLY. This is the data shape a future
 * "Easy / Normal / Hard" menu choice would set in one step instead of
 * tuning lap count, AI aggression, AI car choice and guardrails one at
 * a time; nothing reads GameConfig.difficulty or difficulty_presets[]
 * yet, so picking a preset today has no effect on a race. See TODO.md
 * for what actually wiring it up needs to touch (game_init's lap and
 * AI setup, and — for guardrails — track_init's has_walls, which is
 * currently fixed per circuit rather than overridable per race).
 */
enum {
    DIFFICULTY_EASY   = 0,
    DIFFICULTY_NORMAL = 1,
    DIFFICULTY_HARD   = 2,
    DIFFICULTY_PRESET_COUNT = 3
};

/* which cars the AI is allowed onto the grid, once ai_car_choice means
 * something: ANY = today's behavior (ai_no % kart_spec_count), MATCHED
 * = only cars in the same performance class as the human's, UNDERDOG =
 * biased toward slower cars than the human's */
enum {
    DIFFICULTY_CARS_ANY     = 0,
    DIFFICULTY_CARS_MATCHED = 1,
    DIFFICULTY_CARS_UNDERDOG = 2
};

/* whether a race overrides a circuit's own has_walls; TRACK_DEFAULT
 * leaves it alone */
enum {
    DIFFICULTY_GUARDRAILS_TRACK_DEFAULT = 0,
    DIFFICULTY_GUARDRAILS_ON            = 1,
    DIFFICULTY_GUARDRAILS_OFF           = 2
};

typedef struct {
    const char *name;
    int   laps;               /* 0 = use the circuit's automatic count */
    float ai_aggressiveness;  /* multiplier on top of driver aggression */
    int   ai_car_choice;      /* DIFFICULTY_CARS_*                      */
    int   guardrails;         /* DIFFICULTY_GUARDRAILS_*                */
} DifficultyPreset;

extern const DifficultyPreset difficulty_presets[DIFFICULTY_PRESET_COUNT];
const char *difficulty_preset_name(int preset);

typedef struct {
    int track_id;
    int n_humans;                 /* 1..MAX_HUMANS                     */
    int spec[MAX_HUMANS];         /* chosen car spec per human         */
    int paint[MAX_HUMANS];        /* chosen paint index per human      */
    int gearbox[MAX_HUMANS];      /* GEARBOX_AUTO / GEARBOX_MANUAL     */
    int tire[MAX_HUMANS];         /* TIRE_* compound                    */
    const GameSettings *settings; /* NULL = compiled defaults           */
    int laps_override;            /* 0 = use the circuit's own count    */
    /* Selected difficulty preset (DIFFICULTY_*). Not yet read by
     * game_init or anything downstream of it — see DifficultyPreset
     * above. Note for whoever wires this up: zero-initializing a
     * GameConfig (the usual pattern everywhere it's built) leaves this
     * at DIFFICULTY_EASY, not DIFFICULTY_NORMAL — decide the intended
     * default explicitly rather than relying on the zero value. */
    int difficulty;
} GameConfig;

typedef struct {
    Track track;
    GameConfig cfg;
    GameSettings settings;
    Kart  karts[NUM_KARTS];       /* karts[0..n_humans-1] are human    */
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

/* checkpoint helper, shared with the tests */
int track_checkpoint_for(const Track *t, int seg);

/* AI helpers exposed for the HUD and the tests */
const char *ai_strategy_name(int strategy);

/*
 * Every AI car is a driver with a name, not "AI 7". The name is fixed per
 * grid slot so the same rival is the same rival between races, and it is
 * drawn with the HUD's own font, so it stays inside the alphabet that
 * font can render.
 */
const char *ai_driver_name(int grid_slot);

/*
 * A driver, as distinct from a driving style.
 *
 * The strategy sheets say *how* a car is driven — where it brakes, which
 * line it takes, when it shifts. They were doing double duty as the
 * drivers themselves, which left a field of eleven people who were all
 * roughly as good as each other. Skill, consistency, aggression and how
 * hard someone is on their tires are separate things: a wild driver can
 * be fast or hopeless, and a careful one can be either as well.
 *
 * Each entry is fixed to a grid slot, so the same rival is the same rival
 * between races.
 */
typedef struct {
    const char *name;
    int   strategy;      /* which sheet they drive to                   */
    float skill;         /* pace: scales the grip they dare to use      */
    float consistency;   /* 0 = ragged, 1 = never puts a wheel wrong    */
    float aggression;    /* 0 = happy to follow, 1 = dives up the inside */
    float tire_care;     /* multiplies their tire wear; <1 is kind      */
    int   paint;         /* their colour, so a rival is recognisable     */
    const char *trait;   /* one word for the results screen             */
} AIDriver;

const AIDriver *ai_driver(int grid_slot);
int ai_driver_count(void);
float       ai_corner_conf(const Kart *k, const Track *t, int seg);

/* derived stats for menus: 0-100 km/h time (s) and top speed (km/h) */
float spec_accel_time(const KartSpec *s);
float spec_top_speed(const KartSpec *s);
float spec_accel_time_with_settings(const KartSpec *s,
                                    const GameSettings *settings);
float spec_top_speed_with_settings(const KartSpec *s,
                                   const GameSettings *settings);

#ifdef __cplusplus
}
#endif

#endif /* WIIKART_GAME_H */
