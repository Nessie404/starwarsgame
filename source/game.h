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
#define TRACK_MAX_PADS   8
#define TRACK_MAX_ITEMS  6

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
    int   pad_seg[TRACK_MAX_PADS];  /* boost pad segments                */
    int   n_pads;
    int   item_seg[TRACK_MAX_ITEMS];/* item box rows                     */
    int   n_items;
    int   alpine;                   /* 1 = mountain theme (rock skirts)  */
    float min_x, max_x, min_z, max_z, min_y, max_y;
} Track;

void track_init(Track *t, int track_id);
const char *track_name(int track_id);

/* Locate (x,z) relative to the track. hint = last known segment
 * (searched +/- a small window); -1 searches everywhere. Outputs
 * nearest segment, fraction along it, signed lateral offset
 * (positive = left of travel) and surface elevation at that point. */
void track_locate(const Track *t, float x, float z, int hint,
                  int *seg, float *frac, float *lat, float *y);

int track_is_pad_seg(const Track *t, int seg);
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
#define NUM_KARTS  6
#define RACE_LAPS  3

typedef struct {
    float steer;   /* -1..1, positive = turn left                       */
    int   accel;
    int   brake;
    int   hop;     /* handbrake / drift                                 */
    int   item;    /* use held item (edge-detected by the sim)          */
} Input;

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
    float lat;

    /* driving state */
    int   spec;           /* index into kart_specs                      */
    int   drifting;
    float drift_charge;
    float boost_t;        /* nitro seconds remaining                    */
    int   item_held;      /* 0 = none, 1 = nitro canister              */
    int   prev_item_btn;

    /* role */
    int   human;          /* -1 = AI, else human player index          */
    float ai_line;
    float ai_skill;

    /* results */
    int   rank;
    int   finished;
    float finish_time;
    int   final_rank;

    /* one-frame event flags for the platform layer */
    int   just_boosted;
    int   hit_wall;
    int   got_item;
} Kart;

enum {
    STATE_COUNTDOWN = 0,
    STATE_RACING    = 1,
    STATE_FINISHED  = 2   /* all humans done; sim keeps running        */
};

typedef struct {
    int track_id;
    int n_humans;                 /* 1..MAX_HUMANS                     */
    int spec[MAX_HUMANS];         /* chosen kart spec per human        */
} GameConfig;

typedef struct {
    Track track;
    GameConfig cfg;
    Kart  karts[NUM_KARTS];       /* karts[0..n_humans-1] are human    */
    float item_respawn[TRACK_MAX_ITEMS][3];  /* per row, 3 boxes across */
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

/* derived stats for menus: 0-100 km/h time (s) and top speed (km/h) */
float spec_accel_time(const KartSpec *s);
float spec_top_speed(const KartSpec *s);

#ifdef __cplusplus
}
#endif

#endif /* WIIKART_GAME_H */
