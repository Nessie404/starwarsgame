/*
 * WiiKart — an original arcade kart racer for the Nintendo Wii (homebrew).
 *
 * This header and its companions game.c / track.c are platform-independent
 * plain C99: they contain the whole simulation (track geometry, kart
 * physics, drifting, boost, AI, laps, ranking) and depend only on libm.
 * The Wii-specific rendering / input lives in main.c.
 */
#ifndef WIIKART_GAME_H
#define WIIKART_GAME_H

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Track                                                              */
/* ------------------------------------------------------------------ */

#define TRACK_MAX_POINTS 256
#define TRACK_MAX_PADS   8

typedef struct {
    int   n;                        /* number of centerline samples      */
    float px[TRACK_MAX_POINTS];     /* centerline points (x, z plane)    */
    float pz[TRACK_MAX_POINTS];
    float dx[TRACK_MAX_POINTS];     /* unit forward direction at point i */
    float dz[TRACK_MAX_POINTS];
    float seg_len[TRACK_MAX_POINTS];
    float total_len;
    float road_half;                /* half-width of the paved road      */
    float wall_half;                /* half-width to the invisible wall  */
    int   pad_seg[TRACK_MAX_PADS];  /* boost-pad segment indices         */
    int   n_pads;
    float min_x, max_x, min_z, max_z; /* bounding box (for minimap)      */
} Track;

void track_init(Track *t);

/* Locate (x,z) relative to the track. hint is the last known segment
 * (searched +/- a small window); pass -1 to search the whole track.
 * Outputs: seg = nearest segment index, frac = [0,1) along it,
 * lat = signed lateral offset (positive = left of driving direction). */
void track_locate(const Track *t, float x, float z, int hint,
                  int *seg, float *frac, float *lat);

int track_is_pad_seg(const Track *t, int seg);

/* ------------------------------------------------------------------ */
/* Karts / race                                                       */
/* ------------------------------------------------------------------ */

#define NUM_KARTS 5
#define RACE_LAPS 3

#define KART_VMAX        32.0f   /* top speed on road, units/s          */
#define KART_BOOST_MULT  1.32f
#define KART_OFFROAD_MULT 0.45f
#define KART_REVERSE_MAX  8.0f

typedef struct {
    float steer;   /* -1 .. 1 (positive turns left, math convention)    */
    int   accel;
    int   brake;
    int   hop;     /* held: drift button                                */
} Input;

typedef struct {
    float x, z;
    float heading;        /* radians; dir = (cos h, sin h) in (x, z)    */
    float speed;          /* signed scalar speed along heading          */
    float steer_vis;      /* smoothed steer for wheel/body visuals      */

    int   seg;            /* nearest track segment (hint)               */
    float prog_raw;       /* seg + frac, in [0, n)                      */
    float total_progress; /* continuous, crosses 0 at start line        */
    int   lap;            /* floor(total_progress / n); -1 before line  */
    float lat;            /* cached signed lateral offset               */

    int   drifting;       /* 0 = no, +1 / -1 = locked drift direction   */
    float drift_charge;   /* seconds of accumulated drift               */
    float boost_t;        /* seconds of boost remaining                 */

    int   is_player;
    float ai_line;        /* preferred lateral offset on the racing line */
    float ai_skill;       /* speed factor around 1.0                    */

    int   rank;           /* 1-based, updated every frame               */
    int   finished;
    float finish_time;
    int   final_rank;

    int   just_boosted;   /* event flags for the platform layer (rumble) */
    int   hit_wall;
} Kart;

enum {
    STATE_COUNTDOWN = 0,
    STATE_RACING    = 1,
    STATE_FINISHED  = 2   /* player is done; sim keeps running          */
};

typedef struct {
    Track track;
    Kart  karts[NUM_KARTS];   /* karts[0] is the player                 */
    int   state;
    float countdown;          /* seconds until green light              */
    float race_t;             /* seconds since green light              */
    int   finish_count;
} Game;

void game_init(Game *g);
void game_update(Game *g, const Input *player_in, float dt);

/* small shared helpers (also used by rendering / tests) */
float game_angle_wrap(float a);
float game_clampf(float v, float lo, float hi);

#ifdef __cplusplus
}
#endif

#endif /* WIIKART_GAME_H */
