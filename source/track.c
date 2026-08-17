/*
 * WiiKart track: a closed circuit sampled from a Catmull-Rom spline
 * over hand-placed control points. Platform-independent C99.
 */
#include <math.h>
#include "game.h"

/* Control points of the circuit centerline, on the (x, z) plane.
 * The start/finish line sits at the first point, racing toward +x. */
static const float CP[][2] = {
    {   0.0f,   0.0f },
    {  40.0f,   0.0f },
    {  80.0f,   5.0f },
    { 110.0f,  25.0f },
    { 120.0f,  60.0f },
    { 100.0f,  90.0f },
    {  60.0f,  95.0f },
    {  30.0f,  80.0f },
    {   0.0f,  90.0f },
    { -30.0f, 110.0f },
    { -70.0f, 105.0f },
    { -95.0f,  75.0f },
    { -90.0f,  40.0f },
    { -60.0f,  25.0f },
    { -40.0f,   5.0f },
};
#define N_CP ((int)(sizeof(CP) / sizeof(CP[0])))
#define SAMPLES_PER_CP 8

static void catmull_rom(const float p0[2], const float p1[2],
                        const float p2[2], const float p3[2],
                        float t, float out[2])
{
    float t2 = t * t;
    float t3 = t2 * t;
    int i;
    for (i = 0; i < 2; i++) {
        out[i] = 0.5f * ((2.0f * p1[i]) +
                         (-p0[i] + p2[i]) * t +
                         (2.0f * p0[i] - 5.0f * p1[i] + 4.0f * p2[i] - p3[i]) * t2 +
                         (-p0[i] + 3.0f * p1[i] - 3.0f * p2[i] + p3[i]) * t3);
    }
}

void track_init(Track *t)
{
    int i, s, k;

    t->n = N_CP * SAMPLES_PER_CP;
    if (t->n > TRACK_MAX_POINTS)
        t->n = TRACK_MAX_POINTS;

    k = 0;
    for (i = 0; i < N_CP && k < t->n; i++) {
        const float *p0 = CP[(i - 1 + N_CP) % N_CP];
        const float *p1 = CP[i];
        const float *p2 = CP[(i + 1) % N_CP];
        const float *p3 = CP[(i + 2) % N_CP];
        for (s = 0; s < SAMPLES_PER_CP && k < t->n; s++, k++) {
            float pt[2];
            catmull_rom(p0, p1, p2, p3, (float)s / (float)SAMPLES_PER_CP, pt);
            t->px[k] = pt[0];
            t->pz[k] = pt[1];
        }
    }

    /* Forward direction at each point (central difference, normalized)
     * and segment lengths. */
    t->total_len = 0.0f;
    t->min_x = t->max_x = t->px[0];
    t->min_z = t->max_z = t->pz[0];
    for (i = 0; i < t->n; i++) {
        int ip = (i - 1 + t->n) % t->n;
        int in = (i + 1) % t->n;
        float ddx = t->px[in] - t->px[ip];
        float ddz = t->pz[in] - t->pz[ip];
        float len = sqrtf(ddx * ddx + ddz * ddz);
        if (len < 1e-6f) len = 1e-6f;
        t->dx[i] = ddx / len;
        t->dz[i] = ddz / len;

        ddx = t->px[in] - t->px[i];
        ddz = t->pz[in] - t->pz[i];
        t->seg_len[i] = sqrtf(ddx * ddx + ddz * ddz);
        t->total_len += t->seg_len[i];

        if (t->px[i] < t->min_x) t->min_x = t->px[i];
        if (t->px[i] > t->max_x) t->max_x = t->px[i];
        if (t->pz[i] < t->min_z) t->min_z = t->pz[i];
        if (t->pz[i] > t->max_z) t->max_z = t->pz[i];
    }

    t->road_half = 5.0f;
    t->wall_half = 13.0f;

    /* Boost pads: a pair on the opening straight's exit and one on the
     * back straight. Indices are into the sampled centerline. */
    t->n_pads = 3;
    t->pad_seg[0] = 18;
    t->pad_seg[1] = 19;
    t->pad_seg[2] = 78;
}

int track_is_pad_seg(const Track *t, int seg)
{
    int i;
    for (i = 0; i < t->n_pads; i++)
        if (t->pad_seg[i] == seg)
            return 1;
    return 0;
}

void track_locate(const Track *t, float x, float z, int hint,
                  int *seg, float *frac, float *lat)
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
    /* signed lateral: positive on the left of the driving direction */
    {
        float lx = -t->dz[best];
        float lz =  t->dx[best];
        *lat = (x - best_px) * lx + (z - best_pz) * lz;
    }
}
