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
    int   has_walls;          /* 0 = unguarded: the edge is a drop      */
    float scale;              /* uniform scale on the control points    */
    float y_scale;            /* extra scale on elevation only           */
    /* power-up panel rows, as fractions of the lap [0..1) */
    const float *item_frac;  int n_items;
} TrackDef;

/* ---------------- CLASSIC: flat speedway ---------------- */
static const float CP_CLASSIC[][3] = {
    {   0,   0, 0 }, {  40,   0, 0 }, {  80,   5, 0 }, { 110,  25, 0 },
    { 120,  60, 0 }, { 100,  90, 0 }, {  60,  95, 0 }, {  30,  80, 0 },
    {   0,  90, 0 }, { -30, 110, 0 }, { -70, 105, 0 }, { -95,  75, 0 },
    { -90,  40, 0 }, { -60,  25, 0 }, { -40,   5, 0 },
};
static const float ITEMS_CLASSIC[] = { 0.09f, 0.52f, 0.83f };

/* ---------------- BERTHOUD PASS ---------------- */
/*
 * Berthoud is the technical one: an opening chicane in the valley, four
 * switchbacks up the east face broken up by a double-apex sweeper and a
 * fast kink, a set of esses onto the summit, then a descent that starts
 * fast and tightens as it drops back to the valley floor.
 */
static const float CP_BERTHOUD[][3] = {
    {    0,   0,  0 },  {  46,   0,  2 },
    {  78,  12,  4 },  { 104,  -2,  6 },                  /* chicane      */
    { 142,   6,  9 },  { 178,  28, 12 },
    { 202,  56, 15 },  { 216,  78, 17 },  { 192,  96, 20 },/* switchback 1 */
    { 140,  98, 24 },  { 104, 112, 28 },  {  66, 106, 31 },/* double apex  */
    {  30, 118, 34 },  {  10, 140, 37 },  {  34, 160, 40 },/* switchback 2 */
    {  92, 164, 45 },  { 138, 180, 49 },  { 176, 172, 52 },/* fast kink    */
    { 208, 190, 56 },  { 224, 212, 59 },  { 198, 228, 62 },/* switchback 3 */
    { 148, 226, 66 },  { 108, 244, 70 },  {  56, 240, 74 },/* esses        */
    {  14, 258, 78 },                                      /* summit       */
    { -36, 284, 75 },  { -86, 262, 66 },  { -112, 222, 56 },
    { -128, 176, 46 }, { -150, 140, 39 },                  /* tightening   */
    { -128, 104, 30 }, { -104,  66, 20 }, {  -74,  34, 10 },
    {  -36,  10,  3 },
};
static const float ITEMS_BERTHOUD[] = { 0.04f, 0.30f, 0.55f, 0.88f };

/* ---------------- LOVELAND PASS ---------------- */
/*
 * Loveland, widened and stretched out: a longer run up the valley, more
 * road between the switchbacks, and no guardrails anywhere — the shoulder
 * is the edge.
 */
static const float CP_LOVELAND[][3] = {
    {    0,   0,  0 },  {  90,  -10,  3 }, { 180,   0,  8 },
    { 260,  26, 14 },  { 300,  80, 21 },  { 292, 148, 28 },
    { 300, 196, 34 },  { 326, 226, 38 },  { 286, 254, 42 },   /* hairpin A */
    { 220, 262, 48 },  { 150, 268, 54 },  {  84, 278, 60 },
    {  34, 288, 64 },  {  -4, 316, 68 },  {  36, 342, 71 },   /* hairpin B */
    /* summit turnaround: the road climbs east, swings round the top and
     * comes back west, so it needs a rounded loop rather than a spike */
    {  86, 350, 75 },  { 112, 372, 78 },  {  84, 394, 77 },
    {  20, 390, 74 },  { -70, 366, 64 },  { -150, 300, 52 },
    /* hairpin C: a rounded switchback back down toward the valley */
    { -164, 250, 45 }, { -200, 226, 42 }, { -204, 190, 40 },
    { -162, 166, 36 }, { -116, 128, 26 }, {  -68,  74, 13 },
    {  -32,  30,  5 },
};
static const float ITEMS_LOVELAND[] = { 0.05f, 0.28f, 0.55f, 0.86f };

/* ---------------- KENOSHA PASS ----------------
 * The long one. US-285 over Kenosha is a broad, sweeping road across open
 * park country, so this is the widest and longest circuit in the game:
 * lots of fast curvature, a couple of genuinely tight moments, and much
 * less climbing than the high passes. Guardrails present.
 */
static const float CP_KENOSHA[][3] = {
    {    0,    0,  0 },  {  84,  -16,  2 },  { 150,   6,  4 },
    { 214,  -12,  6 },  { 286,   14,  9 },  { 330,   64, 12 },  /* esses  */
    { 306,  106, 15 },  { 344,  142, 17 },  { 402,  152, 19 },
    { 430,  200, 22 },  { 396,  244, 25 },  { 336,  246, 27 },  /* kinks  */
    { 292,  282, 29 },  { 306,  330, 31 },  { 262,  360, 33 },
    { 200,  348, 35 },  { 152,  376, 37 },  { 158,  428, 39 },
    { 112,  456, 41 },  {  62,  440, 43 },  {  30,  482, 45 },
    {  56,  528, 47 },  { 116,  538, 48 },  { 168,  514, 49 },
    { 224,  536, 50 },  { 258,  582, 51 },  { 226,  626, 52 },
    { 166,  632, 52 },  { 118,  664, 51 },  {  56,  654, 50 },
    {   8,  686, 48 },  { -50,  672, 46 },  { -96,  702, 44 },
    { -152, 676, 41 },  { -166, 620, 39 },  { -128, 578, 37 },
    { -166, 540, 34 },  { -222, 552, 32 },  { -252, 504, 30 },
    { -222, 460, 28 },  { -256, 414, 25 },  { -262, 356, 23 },
    { -226, 314, 20 },  { -256, 268, 18 },  { -232, 222, 15 },
    { -178, 214, 13 },  { -146, 170, 11 },  { -178, 124,  9 },
    { -164,  74,  6 },  { -110,  56,  4 },  {  -66,  16,  2 },
};
static const float ITEMS_KENOSHA[] = { 0.04f, 0.22f, 0.40f, 0.58f,
                                       0.74f, 0.90f };

/* ---------------- MONARCH PASS ----------------
 * The hard one. US-50 over Monarch is a narrow shelf road with switchback
 * after switchback and a very long way down; here it has no guardrails at
 * all, the narrowest road surface, the steepest grades and the tightest
 * corners of any circuit.
 */
static const float CP_MONARCH[][3] = {
    {    0,   0,   0 },  {  60,  -8,   4 },  { 120,  12,   9 },
    { 164,  50,  15 },  { 178,  96,  20 },
    /* switchback 1: a genuinely tight hairpin, ~13 m of radius */
    { 150, 128,  25 },  { 112, 132,  28 },  {  96, 152,  31 },
    { 114, 172,  34 },  { 152, 176,  37 },
    { 196, 200,  41 },  { 208, 240,  45 },
    /* switchback 2 */
    { 186, 272,  49 },  { 148, 278,  52 },  { 132, 300,  55 },
    { 150, 320,  58 },  { 188, 324,  61 },
    { 214, 358,  65 },  { 200, 398,  69 },
    /* switchback 3, just under the summit */
    { 162, 420,  73 },  { 124, 416,  76 },  { 108, 436,  79 },
    { 126, 456,  82 },  { 164, 462,  85 },
    { 182, 500,  88 },  { 146, 528,  91 },  {  92, 528,  93 },
    {  44, 508,  94 },  {   0, 526,  92 },  { -44, 556,  89 },
    /* the descent: three more tight ones on the way down */
    { -96, 548,  84 },  { -126, 512, 79 },  { -108, 476, 74 },
    { -142, 452,  69 },  { -166, 412, 64 },  { -138, 378, 59 },
    { -168, 344,  54 },  { -180, 300, 49 },  { -146, 268, 44 },
    { -170, 228,  38 },  { -158, 184, 33 },  { -116, 168, 28 },
    { -132, 126,  22 },  { -150,  84, 16 },  { -116,  46, 10 },
    {  -66,  22,   4 },
};
static const float ITEMS_MONARCH[] = { 0.06f, 0.32f, 0.62f, 0.88f };

static const TrackDef track_defs[TRACK_COUNT] = {
    { "CLASSIC",
      CP_CLASSIC,  (int)(sizeof(CP_CLASSIC)  / sizeof(CP_CLASSIC[0])),
      5.0f, 13.0f, 0, 1, 1.00f, 1.00f,
      ITEMS_CLASSIC, 3 },
    { "BERTHOUD",
      CP_BERTHOUD, (int)(sizeof(CP_BERTHOUD) / sizeof(CP_BERTHOUD[0])),
      4.2f,  8.5f, 1, 1, 1.00f, 0.62f,   /* grades down to ~15%          */
      ITEMS_BERTHOUD, 4 },
    { "LOUELAND",   /* 'V' cannot be drawn on 7-segment glyphs */
      CP_LOVELAND, (int)(sizeof(CP_LOVELAND) / sizeof(CP_LOVELAND[0])),
      5.4f,  6.6f, 1, 0, 1.00f, 0.78f, /* wider, unguarded, ~14% grades   */
      ITEMS_LOVELAND, 4 },
    { "HENOSHA",    /* 'K' cannot be drawn on 7-segment glyphs either    */
      CP_KENOSHA,  (int)(sizeof(CP_KENOSHA)  / sizeof(CP_KENOSHA[0])),
      6.0f, 11.0f, 1, 1, 0.72f, 1.55f, /* widest and longest, barriered   */
      ITEMS_KENOSHA, 6 },
    { "NONARCH",    /* 'M' cannot be drawn on 7-segment glyphs           */
      CP_MONARCH,  (int)(sizeof(CP_MONARCH)  / sizeof(CP_MONARCH[0])),
      3.8f,  4.8f, 1, 0, 0.78f, 1.00f, /* narrowest, unguarded, steepest  */
      ITEMS_MONARCH, 4 },
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
            /* a uniform scale lets a circuit be tuned for lap length
             * without redrawing it; radii and grades scale with it */
            t->px[k] = pt[0] * d->scale;
            t->pz[k] = pt[1] * d->scale;
            t->py[k] = pt[2] * d->scale * d->y_scale;
        }
    }

    /*
     * Smooth the elevation profile. The plan view is drawn by hand at a
     * compressed scale, which can leave a couple of samples with a 24%
     * grade — a wall, not a road. A few passes of a 3-tap filter keep the
     * overall climb but bring local gradients back to something a car
     * could actually drive.
     */
    {
        float tmp[TRACK_MAX_POINTS];
        int pass;
        for (pass = 0; pass < 3; pass++) {
            for (i = 0; i < t->n; i++) {
                int ip = (i - 1 + t->n) % t->n;
                int in2 = (i + 1) % t->n;
                tmp[i] = 0.25f * t->py[ip] + 0.5f * t->py[i] +
                         0.25f * t->py[in2];
            }
            for (i = 0; i < t->n; i++)
                t->py[i] = tmp[i];
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

    /*
     * Group the curvature profile into corners the AI can learn about.
     * A run of curvature above a modest threshold opens a corner, short
     * straight-ish gaps inside a bend do not close it, and a long run is
     * chopped every CORNER_SPAN meters so that a whole curvy third of a
     * lap does not become one giant "corner" — the point is to attribute
     * a mistake to a specific piece of road. Runs may wrap past the
     * start line.
     */
    {
        const float CORNER_MIN_CURV = 0.012f;   /* ~80 m radius or tighter */
        const float CORNER_SPAN     = 45.0f;    /* max arc length, meters  */
        const int   GAP_TOLERANCE   = 2;
        int in_corner = 0, gap = 0, cur = -1;
        float span = 0.0f;

        for (i = 0; i < t->n; i++)
            t->corner_id[i] = -1;
        t->n_corners = 0;

        for (i = 0; i < t->n; i++) {
            int cornerish = (t->curv[i] >= CORNER_MIN_CURV);

            if (cornerish && in_corner && span >= CORNER_SPAN)
                in_corner = 0;               /* long bend: start a new one */

            if (cornerish) {
                if (!in_corner) {
                    if (t->n_corners >= TRACK_MAX_CORNERS)
                        break;
                    cur = t->n_corners++;
                    t->corner_entry[cur] = i;
                    t->corner_peak[cur] = t->curv[i];
                    in_corner = 1;
                    span = 0.0f;
                }
                if (t->curv[i] > t->corner_peak[cur])
                    t->corner_peak[cur] = t->curv[i];
                t->corner_id[i] = cur;
                span += t->seg_len[i];
                gap = 0;
            } else if (in_corner) {
                if (++gap > GAP_TOLERANCE) {
                    in_corner = 0;
                } else {
                    t->corner_id[i] = cur;   /* still the same bend */
                    span += t->seg_len[i];
                }
            }
        }

        /* a corner straddling the start line is one corner, not two */
        if (t->n_corners > 1 && t->corner_id[0] == 0 &&
            t->corner_id[t->n - 1] == t->n_corners - 1) {
            int last = t->n_corners - 1;
            for (i = 0; i < t->n; i++)
                if (t->corner_id[i] == last)
                    t->corner_id[i] = 0;
            if (t->corner_peak[last] > t->corner_peak[0])
                t->corner_peak[0] = t->corner_peak[last];
            t->corner_entry[0] = t->corner_entry[last];
            t->n_corners--;
        }
    }

    t->road_half = d->road_half;
    t->wall_half = d->wall_half;
    t->alpine = d->alpine;
    t->has_walls = d->has_walls;

    /* Lap count from circuit length, so every race covers roughly the
     * same ground: four laps of the little speedway, two of a pass. */
    {
        int laps = (int)((2600.0f / t->total_len) + 0.5f);
        if (laps < 2) laps = 2;
        if (laps > 4) laps = 4;
        t->laps = laps;
    }

    /* Checkpoints every CHECKPOINT_SPACING samples: where a car that went
     * over an unguarded edge gets put back on the road. */
    t->n_checkpoints = 0;
    for (i = 0; i < t->n && t->n_checkpoints < TRACK_MAX_CHECKPOINTS;
         i += CHECKPOINT_SPACING)
        t->checkpoint_seg[t->n_checkpoints++] = i;

    t->n_items = d->n_items;
    for (i = 0; i < d->n_items && i < TRACK_MAX_ITEMS; i++)
        t->item_seg[i] = (int)(d->item_frac[i] * (float)t->n) % t->n;
}

/* index of the last checkpoint at or before `seg` */
int track_checkpoint_for(const Track *t, int seg)
{
    int cp;
    if (t->n_checkpoints <= 0)
        return -1;
    cp = seg / CHECKPOINT_SPACING;
    if (cp < 0) cp = 0;
    if (cp >= t->n_checkpoints) cp = t->n_checkpoints - 1;
    return cp;
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
        /* the lateral basis is the chase camera's right axis, so a
         * positive result means "to the right of the road" on screen */
        float lx = -t->dz[best];
        float lz =  t->dx[best];
        *lat = (x - best_px) * lx + (z - best_pz) * lz;
    }
    {
        int in = (best + 1) % t->n;
        *y = t->py[best] + (t->py[in] - t->py[best]) * best_frac;
    }
}
