/*
 * WiiKart tracks: closed circuits sampled from Catmull-Rom splines over
 * hand-placed 3D control points (x, z ground plane; y elevation).
 *
 * Four circuits are stylized versions of real Colorado mountain passes:
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
#include <stddef.h>
#include "game.h"

#define SAMPLES_PER_CP 8

typedef struct {
    const char *name;
    const float (*cp)[3];      /* control points x, z, y */
    int   n_cp;
    float road_half, wall_half;
    /* optional per-control-point width multipliers, one per cp; NULL
     * keeps the circuit an even width all the way round */
    const float *cp_width;
    int   alpine;
    int   has_walls;          /* 0 = unguarded: the edge is a drop      */
    float scale;              /* uniform scale on the control points    */
    float y_scale;            /* extra scale on elevation only           */
    /* 1 = draw grandstands along the straights (main.c); every existing
     * entry below leaves this unspecified, which zero-initializes it —
     * see the note on Track.grandstands in game.h */
    int   grandstands;
    /* how strongly Track.bank follows curvature (radians of bank per
     * radian/m of curv_signed), before the hard cap in track_init. Left
     * unspecified (0) by every mountain pass, which gets a small
     * universal default instead — see track_init_with_settings — so
     * only a track that wants to override that default sets this. */
    float bank_mult;
} TrackDef;

/* ---------------- CLASSIC: flat speedway ---------------- */
static const float CP_CLASSIC[][3] = {
    {   0,   0, 0 }, {  40,   0, 0 }, {  80,   5, 0 }, { 110,  25, 0 },
    { 120,  60, 0 }, { 100,  90, 0 }, {  60,  95, 0 }, {  30,  80, 0 },
    {   0,  90, 0 }, { -30, 110, 0 }, { -70, 105, 0 }, { -95,  75, 0 },
    { -90,  40, 0 }, { -60,  25, 0 }, { -40,   5, 0 },
};

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
    { 208, 190, 56 },  { 224, 212, 59 },  { 198, 228, 62 },/* switchback 3,
      the loop back near the summit: the tightest turn on the mountain and
      the closest the road comes to itself anywhere on the lap. Left as
      drawn — see the track's scale factor below, which is what actually
      opens this (and every other corner) out */
    { 148, 226, 66 },  { 108, 244, 70 },  {  56, 240, 74 },/* esses        */
    {  14, 258, 78 },                                      /* summit       */
    { -36, 284, 75 },  { -86, 262, 66 },  { -112, 222, 56 },
    { -128, 176, 46 }, { -150, 140, 39 },                  /* tightening   */
    { -128, 104, 30 }, { -104,  66, 20 }, {  -74,  34, 10 },
    {  -36,  10,  3 },
};

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
    /* extended summit shelf: it keeps climbing after the old turnaround,
     * then folds back through two linked, exposed hairpins */
    { 182, 500,  88 },  { 166, 538,  93 },  { 126, 566,  98 },
    {  72, 560, 102 },  {  28, 582, 106 },  { -10, 620, 108 },
    { -58, 646, 105 },  { -112, 638, 100 }, { -152, 604, 94 },
    { -146, 564,  88 }, { -184, 536,  83 },
    /* the longer descent: tightly linked bends leave little recovery
     * room, and none of them has a guardrail */
    { -214, 500, 77 },  { -198, 458, 71 },  { -158, 444, 68 },
    { -184, 408, 63 },  { -216, 370, 58 },  { -204, 326, 52 },
    { -160, 302, 47 },  { -190, 264, 42 },  { -202, 220, 37 },
    { -166, 188, 32 },  { -122, 180, 29 },  { -150, 144, 24 },
    { -170, 102, 18 },  { -142,  62, 12 },  {  -92,  34,  6 },
    {  -42,  18,  3 },
};

/* ---------------- BREAKNECK PASS ---------------- */
static const float CP_BREAKNECK[][3] = {
    {   119,     0,    0 }, {   140,    28,   11 }, {   138,    59,   22 }, {   110,    82,   26 },
    {    69,    89,   26 }, {    31,    85,   36 }, {     0,    82,   50 }, {   -31,    85,   63 },
    {   -69,    89,   77 }, {  -110,    82,   79 }, {  -138,    59,   79 }, {  -140,    28,   90 },
    {  -119,     0,  103 }, {   -93,   -18,  115 }, {   -79,   -34,  120 }, {   -76,   -56,  120 },
    {   -69,   -89,  110 }, {   -43,  -120,   94 }, {     0,  -133,   78 }, {    43,  -120,   66 },
    {    69,   -89,   66 }, {    76,   -56,   52 }, {    79,   -34,   34 }, {    93,   -18,   17 },
};

static const float CP_GUANELLA[][3] = {
    {   -70,  -100,    0 }, {   -23,  -100,    2 }, {    23,  -100,    5 }, {    70,  -100,    7 },
    {    94,   -90,    8 }, {    87,   -78,    9 }, {    70,   -70,    9 }, {    23,   -70,   12 },
    {   -23,   -70,   14 }, {   -70,   -70,   16 }, {   -94,   -60,   18 }, {   -87,   -48,   19 },
    {   -70,   -40,   19 }, {   -23,   -40,   21 }, {    23,   -40,   23 }, {    70,   -40,   26 },
    {    94,   -30,   27 }, {    87,   -18,   28 }, {    70,   -10,   28 }, {    23,   -10,   31 },
    {   -23,   -10,   33 }, {   -70,   -10,   35 }, {   -94,     0,   36 }, {   -87,    12,   38 },
    {   -70,    20,   38 }, {   -23,    20,   40 }, {    23,    20,   42 }, {    70,    20,   45 },
    {    94,    30,   46 }, {    87,    42,   47 }, {    70,    50,   47 }, {    23,    50,   49 },
    {   -23,    50,   52 }, {   -70,    50,   54 }, {   -94,    60,   55 }, {   -87,    72,   56 },
    {   -70,    80,   56 }, {   -23,    80,   59 }, {    23,    80,   61 }, {    70,    80,   63 },
    {   105,   118,   60 }, {   150,    80,   52 }, {   165,    25,   41 }, {   158,   -35,   30 },
    {   128,   -95,   19 }, {    70,  -140,   11 }, {   -10,  -158,    5 }, {   -85,  -145,    1 },
    {  -125,  -125,   -1 },
};

/*
 * Width profiles, one multiplier per control point, eased between them
 * with the same Catmull-Rom curve as the road itself (see `cp_width` in
 * track_init_with_settings), so a wide or narrow section arrives
 * gradually rather than as a step.
 *
 * BERTHOUD and LOVELAND follow the current blueprint: every named
 * switchback/hairpin apex gets a wide multiplier (1.32) - room to brake
 * deep on the inside and carry the exit out to the outside instead of
 * pinching the one line a hairpin usually forces; every control point
 * whose own 3-point circumradius reads as a genuine straight (>= 150 m)
 * tapers a little (0.85), so the road pulls in between corners instead
 * of holding one constant width the whole lap; everything in between -
 * moderate bends, esses, chicanes, fast kinks - keeps the circuit's
 * nominal width (1.00). This isn't just cosmetic: v1.27.0's combined
 * friction circle already rewards braking on the way in and getting
 * back to power on the way out, and a wide switchback with a narrowing
 * approach is what gives a driver the physical room on the road to
 * actually do that.
 *
 * MONARCH, BREAKNECK and GUANELLA below still run the older width
 * philosophy this replaces (the tightest corners stay narrow, a couple
 * of others are opened out, straights stay nominal) - see TODO.md for
 * carrying this blueprint over to them.
 */
/* W_BERTHOUD: wide at each named switchback's apex (7, 13, 19); tapers
 * on the straights (0, 6, 15, 24, 27, 28, 30-32); nominal everywhere
 * else, including the opening chicane (2-3) and the late tightening
 * bend (29) - both genuinely tight, but neither a switchback. */
static const float W_BERTHOUD[] = {
   0.85f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0.85f, 1.32f,
   1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.32f, 1.00f, 0.85f,
   1.00f, 1.00f, 1.00f, 1.32f, 1.00f, 1.00f, 1.00f, 1.00f,
   0.85f, 1.00f, 1.00f, 0.85f, 0.85f, 1.00f, 0.85f, 0.85f,
   0.85f, 1.00f,
};

/* W_LOVELAND: wide at each named hairpin/turnaround's apex (7, 13, 16,
 * 22); tapers on the straights (1, 2, 5, 9-11, 18, 19, 24-27); nominal
 * everywhere else. */
static const float W_LOVELAND[] = {
   1.00f, 0.85f, 0.85f, 1.00f, 1.00f, 0.85f, 1.00f, 1.32f,
   1.00f, 0.85f, 0.85f, 0.85f, 1.00f, 1.32f, 1.00f, 1.00f,
   1.32f, 1.00f, 0.85f, 0.85f, 1.00f, 1.00f, 1.32f, 1.00f,
   0.85f, 0.85f, 0.85f, 0.85f,
};

/* W_MONARCH: tightest control points 45(R9) 44(R9) 37(R10) 36(R10) 41(R13) 40(R13) */
static const float W_MONARCH[] = {
   1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 
   1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 
   1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 
   1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 
   1.00f, 1.00f, 1.00f, 1.00f, 1.32f, 0.78f, 1.00f, 1.00f, 
   1.00f, 1.32f, 1.00f, 1.00f, 0.78f, 0.78f, 1.00f, 1.00f, 
   1.00f, 1.00f, 1.00f
};

/* W_BREAKNECK: tightest control points 13(R29) 22(R29) 21(R29) 14(R29) 1(R39) 10(R39) */
static const float W_BREAKNECK[] = {
   1.00f, 1.28f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 
   1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0.80f, 1.28f, 1.00f, 
   1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 0.80f, 0.80f, 1.00f
   
};

/* W_GUANELLA: tightest control points 4(R6) 10(R6) 16(R6) 22(R6) 28(R6) 34(R6) */
static const float W_GUANELLA[] = {
   1.00f, 1.00f, 1.00f, 1.00f, 0.82f, 1.00f, 1.00f, 1.00f, 
   1.00f, 1.00f, 0.82f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 
   0.82f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.34f, 1.00f, 
   1.00f, 1.00f, 1.00f, 1.00f, 1.34f, 1.00f, 1.00f, 1.00f, 
   1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 
   1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 1.00f, 
   1.00f
};

/* ---------------- BERTHOUD PASS 2.0 ----------------
 * Drawn from the real elevation profile rather than stylized from
 * memory: three ramp-and-hairpin switchbacks climbing one side of the
 * pass, a summit, four more descending the other side — each ramp a
 * real straight-ish run wide apart from the next before the road folds
 * back on itself, the way an actual switchback road is built rather
 * than reversing every few meters — down to a valley floor, a flat
 * loop-back through the valley that turns the road around, and a
 * gentle climb back up to the start. The start and finish sits at the
 * lap's own middle elevation: below the summit, above the valley floor
 * it drops to.
 *
 * This is the original v1.15.0 layout. v1.17.0 replaced it with a
 * smooth sine-wiggle shape to fix real self-intersection (pieces of
 * road passing within 1-2 m of each other, hairpins turning up to
 * 161° at a single point) — which did fix that, but also flattened out
 * the actual switchback character that made the circuit worth driving.
 * v1.19.1 brings the hairpins back and fixes the clipping the way
 * every other tight track on the roster got fixed the same release: a
 * bigger `scale` in TrackDef, which spreads every ramp and every
 * hairpin apart together without smoothing any of them out. Real
 * hairpins, more room between them — not carved into gentle curves.
 */
static const float CP_BERTHOUD2[][3] = {
    {    0.0,    0.0,  126.0}, {  -70.0,   24.0,  137.1}, {    5.0,   24.0,  148.3},
    {   80.0,   24.0,  159.5}, {  113.0,   55.9,  166.4}, {   80.0,   82.0,  172.7},
    {    5.0,   82.0,  184.0}, {  -70.0,   82.0,  195.2}, { -103.0,  113.9,  202.1},
    {  -70.0,  140.0,  208.4}, {    5.0,  140.0,  219.6}, {   80.0,  140.0,  230.8},
    {  113.0,  171.9,  237.7}, {   80.0,  198.0,  244.0}, {  120.0,  228.0,  242.0},
    {  162.0,  248.0,  244.0}, {  148.0,  214.0,  241.0}, {  142.0,  240.0,  232.2},
    {  103.0,  240.0,  219.4}, {   64.0,  240.0,  206.6}, {   46.8,  275.2,  193.7},
    {   64.0,  304.0,  182.7}, {  103.0,  304.0,  169.8}, {  142.0,  304.0,  157.0},
    {  159.2,  339.2,  144.1}, {  142.0,  368.0,  133.1}, {  103.0,  368.0,  120.3},
    {   64.0,  368.0,  107.4}, {   46.8,  403.2,   94.6}, {   64.0,  432.0,   83.5},
    {  103.0,  432.0,   70.7}, {  142.0,  432.0,   57.9}, {  159.2,  467.2,   45.0},
    {  142.0,  496.0,   34.0}, {  132.0,  530.0,   22.3}, {  120.0,  562.0,   13.2},
    {  112.0,  592.0,    8.0}, {   82.0,  618.0,    8.0}, {   66.0,  586.0,    9.0},
    {   88.0,  550.0,    8.0}, {  134.0,  546.0,    9.0}, {  164.0,  578.0,    8.0},
    {  159.7,  495.4,   24.9}, {  151.5,  412.9,   41.7}, {  136.6,  330.3,   58.6},
    {  113.2,  247.7,   75.4}, {   81.3,  165.1,   92.3}, {   42.5,   82.6,  109.1},
};

/* ---------------- BULLRING ----------------
 * The one flat, wide oval in the roster: two 400 m straights (doubled
 * from the original 200 m in v1.24.0 — a short track grown into a
 * proper speedway) joined by two constant-radius, swept turns (radius
 * 70 m) rather than anything hand-drawn — a true "donut" shape has no
 * business having a kink in it anywhere, so this is generated from the
 * geometry directly instead of authored by eye like the mountain
 * passes. No elevation change anywhere on the lap; the turns carry a
 * gentle bank instead (Track.bank, see track_init_with_settings — this
 * is the one circuit with real banking, not just the small universal
 * road-crown every circuit gets). Grandstands (main.c, gated on
 * Track.grandstands) run the length of both straights.
 */
static const float CP_BULLRING[][3] = {
    { -200.00,  -70.00, 0 }, { -150.00,  -70.00, 0 }, { -100.00,  -70.00, 0 },
    {  -50.00,  -70.00, 0 }, {    0.00,  -70.00, 0 }, {   50.00,  -70.00, 0 },
    {  100.00,  -70.00, 0 }, {  150.00,  -70.00, 0 }, {  200.00,  -70.00, 0 },
    {  223.94,  -65.78, 0 }, {  245.03,  -53.62, 0 }, {  260.62,  -35.00, 0 },
    {  268.94,  -12.16, 0 }, {  268.94,   12.16, 0 }, {  260.62,   35.00, 0 },
    {  245.03,   53.62, 0 }, {  223.94,   65.78, 0 },
    {  200.00,   70.00, 0 }, {  150.00,   70.00, 0 }, {  100.00,   70.00, 0 },
    {   50.00,   70.00, 0 }, {    0.00,   70.00, 0 }, {  -50.00,   70.00, 0 },
    { -100.00,   70.00, 0 }, { -150.00,   70.00, 0 }, { -200.00,   70.00, 0 },
    { -223.94,   65.78, 0 }, { -245.03,   53.62, 0 }, { -260.62,   35.00, 0 },
    { -268.94,   12.16, 0 }, { -268.94,  -12.16, 0 }, { -260.62,  -35.00, 0 },
    { -245.03,  -53.62, 0 }, { -223.94,  -65.78, 0 },
};

/*
 * Pavement-to-guardrail gap: a car that runs wide past the paved edge
 * used to have 4-8 m of only lightly-penalized shoulder before it
 * actually hit anything, which is enough room to straight-line a corner
 * by cutting across it. Barriered tracks now keep the rail close enough
 * to the road surface (about a 1.2-1.4 m curb) that there's nowhere
 * meaningful left to cut through. Unguarded tracks (the edge is a real
 * drop, not a rail) were already this tight and are unchanged.
 */
static const TrackDef track_defs[TRACK_COUNT] = {
    { "CLASSIC",
      CP_CLASSIC,  (int)(sizeof(CP_CLASSIC)  / sizeof(CP_CLASSIC[0])),
      5.6f, 7.0f, NULL, 0, 1, 1.00f, 1.00f },
    { "BERTHOUD",
      CP_BERTHOUD, (int)(sizeof(CP_BERTHOUD) / sizeof(CP_BERTHOUD[0])),
      4.8f,  6.2f, W_BERTHOUD, 1, 1, 1.30f, 0.62f /* grades stay ~15%;
        scaled up 30% so every corner (the summit switchbacks especially)
        opens out into a wider, longer sweep instead of a tight kink   */ },
    { "LOVELAND",
      CP_LOVELAND, (int)(sizeof(CP_LOVELAND) / sizeof(CP_LOVELAND[0])),
      6.0f,  7.2f, W_LOVELAND, 1, 0, 1.15f, 0.78f /* unguarded, ~14%   */ },
    { "KENOSHA",
      CP_KENOSHA,  (int)(sizeof(CP_KENOSHA)  / sizeof(CP_KENOSHA[0])),
      6.6f,  8.0f, NULL, 1, 1, 0.72f, 1.55f /* longest, barriered      */ },
    { "MONARCH",
      CP_MONARCH,  (int)(sizeof(CP_MONARCH)  / sizeof(CP_MONARCH[0])),
      4.3f,  5.3f, W_MONARCH, 1, 0, 1.08f, 1.16f /* high, unguarded    */ },
    { "BREAKNECK",
      CP_BREAKNECK, (int)(sizeof(CP_BREAKNECK) / sizeof(CP_BREAKNECK[0])),
      3.7f,  4.5f, W_BREAKNECK, 1, 0, 1.15f, 0.32f /* steep, unguarded */ },
    { "GUANELLA",
      CP_GUANELLA,  (int)(sizeof(CP_GUANELLA)  / sizeof(CP_GUANELLA[0])),
      3.9f,  4.8f, W_GUANELLA, 1, 0, 1.20f, 1.00f /* switchbacks       */ },
    { "BERTHOUD 2.0",
      CP_BERTHOUD2, (int)(sizeof(CP_BERTHOUD2) / sizeof(CP_BERTHOUD2[0])),
      6.6f,  8.0f, NULL, 1, 1, 1.50f, 0.24f /* big, guarded, real profile;
        scale up from the original 1.30/0.28 for more room everywhere
        while keeping the ~86 m of climb the real elevation data gives it */ },
    { "BULLRING",
      CP_BULLRING, (int)(sizeof(CP_BULLRING) / sizeof(CP_BULLRING[0])),
      7.5f,  9.0f, NULL, 0, 1, 1.00f, 1.00f, /* flat, wide, barriered */
      1, 8.0f /* real (if gentle) banking, well above every mountain
                 pass's small universal crown */ },
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
    track_init_with_settings(t, track_id, NULL);
}

void track_init_with_settings(Track *t, int track_id,
                              const GameSettings *settings)
{
    const TrackDef *d;
    GameSettings defaults;
    GameSettings checked;
    char error[32];
    int i, s, k;
    float heading[TRACK_MAX_POINTS];
    float width_mult[TRACK_MAX_POINTS];
    float scale, elevation_scale;

    if (track_id < 0 || track_id >= TRACK_COUNT) track_id = 0;
    d = &track_defs[track_id];
    if (!settings) {
        game_settings_defaults(&defaults);
        settings = &defaults;
    } else {
        checked = *settings;
        if (!game_settings_validate(&checked, error, (int)sizeof(error))) {
            game_settings_defaults(&defaults);
            settings = &defaults;
        } else {
            settings = &checked;
        }
    }
    scale = d->scale * settings->track_scale_mult[track_id];
    elevation_scale = d->y_scale *
                      settings->track_elevation_mult[track_id];

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
            float u = (float)s / (float)SAMPLES_PER_CP;
            catmull_rom(p0, p1, p2, p3, u, pt);
            /* width is authored per control point and eased between them
             * with the same curve as the road itself, so a narrow section
             * arrives gradually rather than as a step */
            if (d->cp_width) {
                float w0 = d->cp_width[(i - 1 + d->n_cp) % d->n_cp];
                float w1 = d->cp_width[i];
                float w2 = d->cp_width[(i + 1) % d->n_cp];
                float w3 = d->cp_width[(i + 2) % d->n_cp];
                float a0[3], a1[3], a2[3], a3[3], out[3];
                a0[0] = w0; a0[1] = a0[2] = 0.0f;
                a1[0] = w1; a1[1] = a1[2] = 0.0f;
                a2[0] = w2; a2[1] = a2[2] = 0.0f;
                a3[0] = w3; a3[1] = a3[2] = 0.0f;
                catmull_rom(a0, a1, a2, a3, u, out);
                width_mult[k] = out[0];
            } else {
                width_mult[k] = 1.0f;
            }
            /* a uniform scale lets a circuit be tuned for lap length
             * without redrawing it; radii grow while grades stay stable */
            t->px[k] = pt[0] * scale;
            t->pz[k] = pt[1] * scale;
            t->py[k] = pt[2] * scale * elevation_scale;
        }
    }

    /*
     * Soften tight turns. The hand-placed control points give every
     * circuit its character, but a few corners — Berthoud's summit
     * switchbacks especially — come out sharp enough that hugging the
     * inside and running out onto the shoulder is a faster line than the
     * actual apex. Relax each point toward a wider neighborhood's
     * average for a few passes — the same trick already used below for
     * the elevation profile, but over five points instead of three: a
     * narrow 3-tap kernel barely moves a hairpin's curvature no matter
     * how many times it's repeated (it converges to removing sample-
     * level noise, not to opening out a real direction change), while a
     * single wider pass measurably eases the rate a corner's curvature
     * ramps up and back down — noticeably less abrupt entering and
     * exiting a tight bend, without erasing the apex itself.
     *
     * MONARCH keeps the original narrow kernel. It is the one circuit
     * where this was tried and made a real difference for the worse:
     * unguarded, narrowest, and already right at the edge of what the
     * YOLO strategy sheet can survive (see the AI/YOLO note on
     * TRACK_MONARCH's TrackDef entry below) — the wider kernel
     * perturbed its tightest hairpin just enough that an AI_YOLO driver
     * stopped ever recovering from a fall there. Every other track
     * passed the same full AI field on every circuit before and after.
     */
    {
        float tx[TRACK_MAX_POINTS], tz[TRACK_MAX_POINTS];
        int pass;
        if (track_id == TRACK_MONARCH) {
            for (pass = 0; pass < 3; pass++) {
                for (i = 0; i < t->n; i++) {
                    int ip = (i - 1 + t->n) % t->n;
                    int in2 = (i + 1) % t->n;
                    tx[i] = 0.25f * t->px[ip] + 0.5f * t->px[i] +
                            0.25f * t->px[in2];
                    tz[i] = 0.25f * t->pz[ip] + 0.5f * t->pz[i] +
                            0.25f * t->pz[in2];
                }
                for (i = 0; i < t->n; i++) {
                    t->px[i] = tx[i];
                    t->pz[i] = tz[i];
                }
            }
        } else {
            for (pass = 0; pass < 3; pass++) {
                for (i = 0; i < t->n; i++) {
                    int im2 = (i - 2 + t->n) % t->n;
                    int ip = (i - 1 + t->n) % t->n;
                    int in2 = (i + 1) % t->n;
                    int ip2 = (i + 2) % t->n;
                    tx[i] = 0.10f * t->px[im2] + 0.20f * t->px[ip] +
                            0.40f * t->px[i]   + 0.20f * t->px[in2] +
                            0.10f * t->px[ip2];
                    tz[i] = 0.10f * t->pz[im2] + 0.20f * t->pz[ip] +
                            0.40f * t->pz[i]   + 0.20f * t->pz[in2] +
                            0.10f * t->pz[ip2];
                }
                for (i = 0; i < t->n; i++) {
                    t->px[i] = tx[i];
                    t->pz[i] = tz[i];
                }
            }
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

    /* curvature = heading change per meter, smoothed over 5 samples.
     * The signed version keeps which way the road bends instead of just
     * how sharply — positive means it bends toward positive lat (right),
     * matching track_locate's lateral sign — which is what lets the AI's
     * racing line lean into the actual apex instead of a fixed side. */
    for (i = 0; i < t->n; i++) {
        float acc = 0.0f, sacc = 0.0f, lensum = 0.0f;
        int j;
        for (j = -2; j <= 2; j++) {
            int a = ((i + j) % t->n + t->n) % t->n;
            int b = (a + 1) % t->n;
            float dh = game_angle_wrap(heading[b] - heading[a]);
            acc += fabsf(dh);
            sacc += dh;
            lensum += t->seg_len[a];
        }
        t->curv[i] = acc / (lensum > 0.1f ? lensum : 0.1f);
        t->curv_signed[i] = sacc / (lensum > 0.1f ? lensum : 0.1f);
    }

    /*
     * Road cant: every circuit crowns a little into its own curvature
     * (real roads do this too), tracks that don't ask for more get a
     * small universal default rather than none at all — that is the
     * "every map" half of this. Deriving it straight from curv_signed
     * means it ramps in and out exactly as the turn itself does, no
     * separate transition logic needed, and the hard cap keeps a wild
     * combination (a strong bank_mult over a genuine hairpin) from
     * producing something absurd.
     */
    {
        const float BANK_MULT_DEFAULT = 0.3f;
        const float BANK_MAX = 0.22f;      /* ~12.6 degrees, hard cap  */
        float mult = d->bank_mult > 0.0f ? d->bank_mult : BANK_MULT_DEFAULT;
        for (i = 0; i < t->n; i++)
            t->bank[i] = game_clampf(t->curv_signed[i] * mult, -BANK_MAX,
                                     BANK_MAX);
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

    {
        float base_road = d->road_half * settings->track_width_mult[track_id];
        float base_wall = d->wall_half * settings->track_width_mult[track_id];
        float sum_road = 0.0f, sum_wall = 0.0f;

        for (i = 0; i < t->n; i++) {
            /* a width multiplier is a road-design choice, but it still has
             * to leave a road: never narrower than a car, never absurd */
            float w = width_mult[i];
            if (!(w >= 0.35f)) w = 0.35f;     /* also catches NaN */
            if (w > 3.0f) w = 3.0f;
            t->road_half_seg[i] = base_road * w;
            t->wall_half_seg[i] = base_wall * w;
            sum_road += t->road_half_seg[i];
            sum_wall += t->wall_half_seg[i];
        }
        t->road_half = sum_road / (float)t->n;
        t->wall_half = sum_wall / (float)t->n;
    }
    t->alpine = d->alpine;
    t->has_walls = d->has_walls;
    t->grandstands = d->grandstands;

    /* Lap count from circuit length, so every race covers roughly the
     * same ground: four laps of the little speedway, two of a pass. */
    {
        int laps = settings->track_laps[track_id];
        if (laps <= 0) {
            laps = (int)((settings->target_race_distance_m / t->total_len) +
                         0.5f);
            if (laps < settings->min_laps) laps = settings->min_laps;
            if (laps > settings->max_laps) laps = settings->max_laps;
        }
        t->laps = laps;
    }

    /* Checkpoints every CHECKPOINT_SPACING samples: where a car that went
     * over an unguarded edge gets put back on the road. */
    t->n_checkpoints = 0;
    for (i = 0; i < t->n && t->n_checkpoints < TRACK_MAX_CHECKPOINTS;
         i += CHECKPOINT_SPACING)
        t->checkpoint_seg[t->n_checkpoints++] = i;

    /*
     * Weather: three patches spread evenly around every circuit's lap,
     * each on its own melt schedule (see the offsets below) so the whole
     * track is never in lockstep — snow in one zone, ice in the next,
     * already a puddle in the third. Every track gets this, derived from
     * nothing but its own point count, so a longer or shorter lap simply
     * spreads the same three zones over more or less road; whether any
     * of it actually shows up in a race is GameConfig.weather, not
     * anything decided here (see game_init).
     */
    for (i = 0; i < t->n; i++)
        t->weather_zone[i] = -1;
    t->n_weather_zones = 0;
    {
        int zone_start[3];
        int zone_len = t->n / 9;
        int z;
        zone_start[0] = t->n * 1 / 12;
        zone_start[1] = t->n * 5 / 12;
        zone_start[2] = t->n * 9 / 12;
        for (z = 0; z < 3; z++) {
            /* zone 0 starts fresh at the green flag; each zone after it
             * is already further along its own melt clock */
            t->weather_zone_offset[z] = -18.0f * (float)z;
            for (i = 0; i < zone_len; i++)
                t->weather_zone[(zone_start[z] + i) % t->n] = z;
        }
        t->n_weather_zones = 3;
    }
}

float track_road_half(const Track *t, int seg)
{
    if (seg < 0 || seg >= t->n)
        return t->road_half;
    return t->road_half_seg[seg];
}

float track_wall_half(const Track *t, int seg)
{
    if (seg < 0 || seg >= t->n)
        return t->wall_half;
    return t->wall_half_seg[seg];
}

int track_weather_at(const Track *t, int seg, float race_t,
                     const GameSettings *settings)
{
    GameSettings defaults;
    int zone;
    float age, snow_to_ice, ice_to_puddle;

    if (seg < 0 || seg >= t->n)
        return WEATHER_CLEAR;
    zone = t->weather_zone[seg];
    if (zone < 0 || zone >= t->n_weather_zones)
        return WEATHER_CLEAR;

    if (!settings) {
        game_settings_defaults(&defaults);
        settings = &defaults;
    }
    snow_to_ice = settings->weather_snow_to_ice_s;
    ice_to_puddle = settings->weather_ice_to_puddle_s;

    age = race_t + t->weather_zone_offset[zone];
    if (age < 0.0f) age = 0.0f;
    if (age < snow_to_ice) return WEATHER_SNOW;
    if (age < ice_to_puddle) return WEATHER_ICE;
    return WEATHER_PUDDLE;
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
