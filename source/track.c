/*
 * WiiKart tracks: closed circuits sampled from Catmull-Rom splines over
 * hand-placed 3D control points (x, z ground plane; y elevation).
 *
 * Two circuits are stylized versions of real Colorado mountain passes:
 *
 *  - BERTHOUD (US-40, Berthoud Pass): a stack of tight switchbacks up
 *    the west face, a short summit ridge, and a flowing descent down
 *    the far side back to the valley.
 *  - LOVELAND (US-6, Loveland Pass): fast sweepers low down, paired
 *    hairpins just under the summit, and a quick downhill return with
 *    one more hairpin.
 *
 * Geometry is compressed to lap scale but keeps the character of the
 * roads: 6-10%% grades, ~15 m hairpin radii, two-lane width.
 */
#include <math.h>
#include "game.h"

#define SAMPLES_PER_CP 8

typedef struct {
    const char *name;
    const float (*cp)[3];      /* control points x, z, y */
    int   n_cp;
    float road_half, wall_half;
    int   alpine;
    /* boost pads / item rows given as fractions of the lap [0..1) */
    const float *pad_frac;   int n_pads;
    const float *item_frac;  int n_items;
} TrackDef;

/* ---------------- CLASSIC: flat speedway ---------------- */
static const float CP_CLASSIC[][3] = {
    {   0,   0, 0 }, {  40,   0, 0 }, {  80,   5, 0 }, { 110,  25, 0 },
    { 120,  60, 0 }, { 100,  90, 0 }, {  60,  95, 0 }, {  30,  80, 0 },
    {   0,  90, 0 }, { -30, 110, 0 }, { -70, 105, 0 }, { -95,  75, 0 },
    { -90,  40, 0 }, { -60,  25, 0 }, { -40,   5, 0 },
};
static const float PADS_CLASSIC[]  = { 0.150f, 0.158f, 0.650f };
static const float ITEMS_CLASSIC[] = { 0.09f, 0.83f };

/* ---------------- BERTHOUD PASS ---------------- */
static const float CP_BERTHOUD[][3] = {
    {    0,   0,  0 },  {  60,   2,  2 },  { 120,  10,  6 },
    { 170,  30, 11 },
    { 200,  55, 15 },  { 215,  75, 17 },  { 195,  92, 20 },   /* hairpin 1 */
    { 130, 100, 26 },  {  70, 108, 32 },
    {  30, 118, 35 },  {  12, 138, 38 },  {  32, 158, 41 },   /* hairpin 2 */
    { 100, 166, 47 },  { 160, 174, 53 },
    { 205, 186, 57 },  { 222, 206, 60 },  { 200, 224, 63 },   /* hairpin 3 */
    { 130, 234, 68 },  {  60, 242, 74 },
    {  10, 260, 78 },                                          /* summit */
    { -40, 285, 74 },  { -90, 260, 63 },  { -120, 210, 51 },
    { -135, 150, 38 }, { -120,  90, 25 }, {  -90,  45, 13 },
    {  -50,  12,  4 },
};
static const float ITEMS_BERTHOUD[] = { 0.05f, 0.42f, 0.86f };

/* ---------------- LOVELAND PASS ---------------- */
static const float CP_LOVELAND[][3] = {
    {    0,   0,  0 },  {  80,  -8,  3 },  { 160,   0,  9 },
    { 220,  30, 16 },  { 245,  90, 24 },  { 230, 150, 32 },
    { 240, 190, 38 },  { 252, 212, 41 },  { 232, 230, 44 },   /* hairpin A */
    { 170, 240, 50 },  { 110, 248, 56 },  {  55, 258, 62 },
    {  18, 270, 66 },  {   0, 292, 69 },  {  22, 312, 72 },   /* hairpin B */
    {  80, 322, 78 },                                          /* summit */
    {   0, 330, 74 },  { -80, 315, 64 },  { -130, 260, 52 },
    { -150, 215, 45 }, { -168, 195, 42 }, { -150, 175, 39 },  /* hairpin C */
    { -100, 130, 27 }, {  -60,  75, 14 }, {  -30,  30,  5 },
};
static const float ITEMS_LOVELAND[] = { 0.05f, 0.40f, 0.90f };

static const TrackDef track_defs[TRACK_COUNT] = {
    { "CLASSIC",
      CP_CLASSIC,  (int)(sizeof(CP_CLASSIC)  / sizeof(CP_CLASSIC[0])),
      5.0f, 13.0f, 0,
      PADS_CLASSIC, 3, ITEMS_CLASSIC, 2 },
    { "BERTHOUD",
      CP_BERTHOUD, (int)(sizeof(CP_BERTHOUD) / sizeof(CP_BERTHOUD[0])),
      4.2f,  8.5f, 1,
      0, 0, ITEMS_BERTHOUD, 3 },
    { "LOUELAND",   /* 'V' cannot be drawn on 7-segment glyphs */
      CP_LOVELAND, (int)(sizeof(CP_LOVELAND) / sizeof(CP_LOVELAND[0])),
      4.2f,  8.5f, 1,
      0, 0, ITEMS_LOVELAND, 3 },
};

const char *track_name(int track_id)
{
    if (track_id < 0 || track_id >= TRACK_COUNT) track_id = 0;
    return track_defs[track_id].name;
}

static void catmull_rom(const float p0[3], const float p1[3],
                        const float p2[3], const float p3[3],
                        float t, float out[3])
{
    float t2 = t * t;
    float t3 = t2 * t;
    int i;
    for (i = 0; i < 3; i++) {
        out[i] = 0.5f * ((2.0f * p1[i]) +
                         (-p0[i] + p2[i]) * t +
                         (2.0f * p0[i] - 5.0f * p1[i] + 4.0f * p2[i] - p3[i]) * t2 +
                         (-p0[i] + 3.0f * p1[i] - 3.0f * p2[i] + p3[i]) * t3);
    }
}

void track_init(Track *t, int track_id)
{
    const TrackDef *d;
    int i, s, k;
    float heading[TRACK_MAX_POINTS];

    if (track_id < 0 || track_id >= TRACK_COUNT) track_id = 0;
    d = &track_defs[track_id];

    t->id = track_id;
    t->name = d->name;
    t->n = d->n_cp * SAMPLES_PER_CP;
    if (t->n > TRACK_MAX_POINTS)
        t->n = TRACK_MAX_POINTS;

    k = 0;
    for (i = 0; i < d->n_cp && k < t->n; i++) {
        const float *p0 = d->cp[(i - 1 + d->n_cp) % d->n_cp];
        const float *p1 = d->cp[i];
        const float *p2 = d->cp[(i + 1) % d->n_cp];
        const float *p3 = d->cp[(i + 2) % d->n_cp];
        for (s = 0; s < SAMPLES_PER_CP && k < t->n; s++, k++) {
            float pt[3];
            catmull_rom(p0, p1, p2, p3, (float)s / (float)SAMPLES_PER_CP, pt);
            t->px[k] = pt[0];
            t->pz[k] = pt[1];
            t->py[k] = pt[2];
        }
    }

    t->total_len = 0.0f;
    t->min_x = t->max_x = t->px[0];
    t->min_z = t->max_z = t->pz[0];
    t->min_y = t->max_y = t->py[0];
    for (i = 0; i < t->n; i++) {
        int ip = (i - 1 + t->n) % t->n;
        int in = (i + 1) % t->n;
        float ddx = t->px[in] - t->px[ip];
        float ddz = t->pz[in] - t->pz[ip];
        float len = sqrtf(ddx * ddx + ddz * ddz);
        if (len < 1e-6f) len = 1e-6f;
        t->dx[i] = ddx / len;
        t->dz[i] = ddz / len;
        heading[i] = atan2f(t->dz[i], t->dx[i]);

        ddx = t->px[in] - t->px[i];
        ddz = t->pz[in] - t->pz[i];
        t->seg_len[i] = sqrtf(ddx * ddx + ddz * ddz);
        if (t->seg_len[i] < 1e-4f) t->seg_len[i] = 1e-4f;
        t->total_len += t->seg_len[i];
        t->slope[i] = (t->py[in] - t->py[i]) / t->seg_len[i];

        if (t->px[i] < t->min_x) t->min_x = t->px[i];
        if (t->px[i] > t->max_x) t->max_x = t->px[i];
        if (t->pz[i] < t->min_z) t->min_z = t->pz[i];
        if (t->pz[i] > t->max_z) t->max_z = t->pz[i];
        if (t->py[i] < t->min_y) t->min_y = t->py[i];
        if (t->py[i] > t->max_y) t->max_y = t->py[i];
    }

    /* curvature = heading change per meter, smoothed over 5 samples */
    for (i = 0; i < t->n; i++) {
        float acc = 0.0f, lensum = 0.0f;
        int j;
        for (j = -2; j <= 2; j++) {
            int a = ((i + j) % t->n + t->n) % t->n;
            int b = (a + 1) % t->n;
            acc += fabsf(game_angle_wrap(heading[b] - heading[a]));
            lensum += t->seg_len[a];
        }
        t->curv[i] = acc / (lensum > 0.1f ? lensum : 0.1f);
    }

    t->road_half = d->road_half;
    t->wall_half = d->wall_half;
    t->alpine = d->alpine;

    t->n_pads = d->n_pads;
    for (i = 0; i < d->n_pads && i < TRACK_MAX_PADS; i++)
        t->pad_seg[i] = (int)(d->pad_frac[i] * (float)t->n) % t->n;
    t->n_items = d->n_items;
    for (i = 0; i < d->n_items && i < TRACK_MAX_ITEMS; i++)
        t->item_seg[i] = (int)(d->item_frac[i] * (float)t->n) % t->n;
}

int track_is_pad_seg(const Track *t, int seg)
{
    int i;
    for (i = 0; i < t->n_pads; i++)
        if (t->pad_seg[i] == seg)
            return 1;
    return 0;
}

int track_item_row(const Track *t, int seg)
{
    int i;
    for (i = 0; i < t->n_items; i++)
        if (t->item_seg[i] == seg)
            return i;
    return -1;
}

void track_locate(const Track *t, float x, float z, int hint,
                  int *seg, float *frac, float *lat, float *y)
{
    int lo, span, j;
    int best = 0;
    float best_frac = 0.0f, best_d2 = 1e30f;
    float best_px = 0.0f, best_pz = 0.0f;

    if (hint < 0) {
        lo = 0;
        span = t->n;
    } else {
        lo = hint - 8;
        span = 17;
    }

    for (j = 0; j < span; j++) {
        int i = ((lo + j) % t->n + t->n) % t->n;
        int in = (i + 1) % t->n;
        float ax = t->px[i],  az = t->pz[i];
        float bx = t->px[in], bz = t->pz[in];
        float abx = bx - ax, abz = bz - az;
        float ab2 = abx * abx + abz * abz;
        float tt = 0.0f;
        float qx, qz, ddx, ddz, d2;

        if (ab2 > 1e-9f)
            tt = ((x - ax) * abx + (z - az) * abz) / ab2;
        if (tt < 0.0f) tt = 0.0f;
        if (tt > 0.999f) tt = 0.999f;

        qx = ax + abx * tt;
        qz = az + abz * tt;
        ddx = x - qx;
        ddz = z - qz;
        d2 = ddx * ddx + ddz * ddz;
        if (d2 < best_d2) {
            best_d2 = d2;
            best = i;
            best_frac = tt;
            best_px = qx;
            best_pz = qz;
        }
    }

    *seg = best;
    *frac = best_frac;
    {
        float lx = -t->dz[best];
        float lz =  t->dx[best];
        *lat = (x - best_px) * lx + (z - best_pz) * lz;
    }
    {
        int in = (best + 1) % t->n;
        *y = t->py[best] + (t->py[in] - t->py[best]) * best_frac;
    }
}
