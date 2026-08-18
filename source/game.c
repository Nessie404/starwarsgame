/*
 * WiiKart simulation — physically-based vehicle dynamics, AI drivers,
 * items, laps and ranking. Platform-independent C99 (libm only).
 *
 * The model works in SI units and derives behaviour from each vehicle's
 * real-world spec sheet:
 *
 *  - acceleration from engine power (F = P/v, traction-capped)
 *  - aerodynamic drag from Cd*A, rolling resistance, so top speed is
 *    emergent rather than scripted
 *  - braking from the quoted 100-0 km/h stopping distance
 *  - cornering from a bicycle steering model whose yaw rate is capped
 *    by the tire's lateral grip (v^2/r <= mu*g) — exceed it and the
 *    car understeers wide, scrubbing speed
 *  - gravity acting along the road grade, so climbs cost speed and
 *    descents give it back
 *
 * A light handbrake-drift with a small mini-turbo is kept as the one
 * arcade nod.
 */
#include <math.h>
#include <string.h>
#include "game.h"

#define PI_F 3.14159265358979f

#define RHO_AIR    1.225f
#define CRR        0.015f      /* rolling resistance coefficient       */
#define DRIVE_EFF  0.85f       /* drivetrain efficiency                */
#define HP_TO_W    745.7f
#define V100       27.78f      /* 100 km/h in m/s                      */
/* Power-up strengths live in game.h (PUSH_POWER / TIRE_GRIP). */

const KartSpec kart_specs[SPEC_COUNT] = {
    /* name      mass    hp   brake  lat_g  CdA   wheelbase offroad
     *   gears, and the road speed (m/s) at the limiter in each          */
    { "RACER",   260.f,  48.f, 30.f, 1.30f, 0.45f, 1.05f,   0.30f,
      4, { 14.f, 24.f, 35.f, 47.f } },
    { "SPORT",   950.f, 150.f, 37.f, 0.95f, 0.66f, 2.45f,   0.45f,
      5, { 13.f, 22.f, 33.f, 45.f, 60.f } },
    { "RALLY",  1180.f, 220.f, 40.f, 0.88f, 0.70f, 2.60f,   0.72f,
      6, { 12.f, 20.f, 29.f, 40.f, 53.f, 67.f } },
    { "TOURER", 1350.f, 310.f, 34.f, 1.02f, 0.60f, 2.70f,   0.35f,
      6, { 15.f, 25.f, 37.f, 50.f, 64.f, 79.f } },
};

/*
 * Where you are in a gear matters. Below the torque band the engine bogs,
 * the middle of the band is where the power is, and past peak revs output
 * tails off until the limiter cuts it entirely — which is what stops a
 * gear from pulling past its top speed.
 */
float gear_power_scale(float frac)
{
    if (frac < 0.0f)
        frac = 0.0f;
    if (frac < BOG_FRACTION)
        return 0.45f + 1.6f * frac;          /* bogging: 0.45 .. 0.99   */
    if (frac <= 0.92f)
        return 1.0f;                          /* in the band            */
    if (frac <= 1.0f)
        return 1.0f - 2.4f * (frac - 0.92f);  /* past peak revs         */
    return 0.0f;                              /* on the limiter         */
}

const char *gearbox_name(int mode)
{
    return (mode == GEARBOX_MANUAL) ? "SHIFT" : "AUTO";
}

const char *tire_name(int compound)
{
    switch (compound) {
    case TIRE_SOFT: return "SOFT";
    case TIRE_HARD: return "HARD";
    default:        return "STD";
    }
}

/* Softer rubber grips harder and drags a little more; hard rubber gives
 * some grip back for a slipperier, faster car. */
float tire_grip_mult(int compound)
{
    switch (compound) {
    case TIRE_SOFT: return 1.08f;
    case TIRE_HARD: return 0.94f;
    default:        return 1.00f;
    }
}

float tire_drag_mult(int compound)
{
    switch (compound) {
    case TIRE_SOFT: return 1.04f;
    case TIRE_HARD: return 0.97f;
    default:        return 1.00f;
    }
}

/* Strategy sheets. conf_start over 1.0 means the driver begins the race
 * believing it can beat the grip limit: it will run wide, learn, and
 * settle down. Under 1.0 means it starts cautious and works up. */
const AIStrategy ai_strategies[AI_STRATEGY_COUNT] = {
/*   name        conf_start conf_max learn_up learn_down line   defend attack wait  trim
 *                                                        up    down  shift delay     */
  { "BALANCED",   0.97f,   1.05f,   0.014f,  0.10f,   0.00f,  0.35f, 0.40f, 1.0f, 1.000f,
                                                             0.93f, 0.42f, 0.04f },
  { "LATE",       1.12f,   1.14f,   0.010f,  0.17f,  -0.10f,  0.25f, 0.70f, 0.3f, 1.015f,
                                                             0.99f, 0.34f, 0.02f },
  { "INSIDE",     0.99f,   1.06f,   0.013f,  0.11f,  -0.55f,  0.55f, 0.45f, 1.2f, 0.995f,
                                                             0.90f, 0.40f, 0.05f },
  { "DEFENDER",   0.95f,   1.02f,   0.011f,  0.09f,   0.10f,  0.95f, 0.25f, 2.2f, 0.990f,
                                                             0.87f, 0.38f, 0.09f },
  { "CHARGER",    1.06f,   1.11f,   0.012f,  0.14f,  -0.25f,  0.30f, 0.95f, 0.0f, 1.020f,
                                                             0.97f, 0.38f, 0.02f },
  { "DRAFTER",    1.00f,   1.09f,   0.016f,  0.12f,   0.30f,  0.40f, 0.80f, 3.0f, 1.005f,
                                                             0.92f, 0.45f, 0.06f },
  { "CRUISER",    0.88f,   1.03f,   0.018f,  0.07f,   0.45f,  0.20f, 0.30f, 1.6f, 0.985f,
                                                             0.82f, 0.36f, 0.12f },
};

const char *power_name(int power)
{
    switch (power) {
    case POWER_PUSH:  return "PUSH";
    case POWER_TIRES: return "TIRES";
    default:          return "";
    }
}

const char *ai_strategy_name(int strategy)
{
    if (strategy < 0 || strategy >= AI_STRATEGY_COUNT)
        return "BALANCED";
    return ai_strategies[strategy].name;
}

/* what this driver currently believes about the corner at `seg` */
float ai_corner_conf(const Kart *k, const Track *t, int seg)
{
    int c;
    if (seg < 0 || seg >= t->n)
        return 1.0f;
    c = t->corner_id[seg];
    if (c < 0 || c >= t->n_corners)
        return 1.0f;
    return k->corner_conf[c];
}

/*
 * Clamping is the choke point every outside number passes through, so it
 * is also where a NaN has to die: `v < lo` and `v > hi` are both false
 * for one, and it would otherwise sail through into a position or a
 * heading and freeze the car (and the camera) for the rest of the race.
 */
float game_clampf(float v, float lo, float hi)
{
    if (!(v >= lo)) return lo;    /* false for NaN as well as for v < lo */
    if (v > hi) return hi;
    return v;
}

float game_angle_wrap(float a)
{
    if (!(a > -1.0e6f && a < 1.0e6f))
        return 0.0f;              /* NaN, infinity, or an angle so large
                                   * that one ulp exceeds 2*pi and the
                                   * loops below could never terminate */
    while (a >  PI_F) a -= 2.0f * PI_F;
    while (a < -PI_F) a += 2.0f * PI_F;
    return a;
}

/* ------------------------------------------------------------------ */
/* Steering feel: the virtual analog stick (see game.h)                */
/* ------------------------------------------------------------------ */

#define STEER_RATE_ON      2.6f   /* wind-on rate at a standstill, 1/s */
#define STEER_RATE_CENTER  6.0f   /* self-centering rate, 1/s          */
#define STEER_SPEED_FADE   0.035f /* how fast the wheel slows with v   */
#define STEER_CURVE        1.55f  /* >1 = gentle near center           */

void steer_axis_reset(SteerAxis *a)
{
    a->value = 0.0f;
}

float steer_axis_update(SteerAxis *a, float target, float speed, float dt)
{
    float rate, diff, mag;

    target = game_clampf(target, -1.0f, 1.0f);

    /* unwinding toward center (or crossing it) is quick, like letting a
     * real wheel spin back; winding on is slower, and slower still the
     * faster the car is going */
    if (fabsf(target) < fabsf(a->value) || target * a->value < 0.0f) {
        rate = STEER_RATE_CENTER;
    } else {
        rate = STEER_RATE_ON *
               (0.30f + 0.70f / (1.0f + fabsf(speed) * STEER_SPEED_FADE));
    }

    diff = target - a->value;
    if (diff >  rate * dt) diff =  rate * dt;
    if (diff < -rate * dt) diff = -rate * dt;
    a->value = game_clampf(a->value + diff, -1.0f, 1.0f);

    mag = powf(fabsf(a->value), STEER_CURVE);
    return (a->value < 0.0f) ? -mag : mag;
}

/* ------------------------------------------------------------------ */
/* Spec-derived display stats                                          */
/* ------------------------------------------------------------------ */

float spec_top_speed(const KartSpec *s)
{
    float P = s->power_hp * HP_TO_W * DRIVE_EFF;
    float v = 40.0f;
    float geared;
    int i;
    for (i = 0; i < 40; i++) {
        float resist = 0.5f * RHO_AIR * s->cd_a * v * v +
                       CRR * s->mass_kg * GRAVITY;
        v = 0.5f * (v + P / resist);
    }
    /* whichever runs out first: the air, or top gear */
    geared = s->gear_top[s->n_gears - 1];
    if (geared < v)
        v = geared;
    return v * 3.6f;
}

float spec_accel_time(const KartSpec *s)
{
    float P = s->power_hp * HP_TO_W * DRIVE_EFF;
    float v = 0.5f, t = 0.0f;
    while (v < V100 && t < 30.0f) {
        float a = P / (s->mass_kg * (v > 3.0f ? v : 3.0f));
        float cap = s->lat_g * GRAVITY;      /* traction limit */
        if (a > cap) a = cap;
        a -= (0.5f * RHO_AIR * s->cd_a * v * v) / s->mass_kg + CRR * GRAVITY;
        v += a * 0.01f;
        t += 0.01f;
    }
    return t;
}

/* ------------------------------------------------------------------ */

/*
 * Place a car on the starting grid. With a full twelve-car field the back
 * row sits ~40 m behind the line, which on a mountain pass is already
 * round a bend, so the grid is walked backwards along the centerline
 * rather than in a straight line from the start.
 */
static void kart_place_on_grid(Game *g, Kart *k, int grid_slot)
{
    const Track *t = &g->track;
    float side = (grid_slot % 2 == 0) ? 1.7f : -1.7f;
    float back = 7.0f + (float)(grid_slot / 2) * 6.4f;
    float remain = back;
    float along, cx, cz, lx, lz, frac;
    int seg = 0, guard, nxt;

    /* Walk back to the segment holding the target point, then interpolate
     * inside it: snapping to sample boundaries would collapse two rows
     * onto one another whenever the row spacing is close to the sample
     * spacing. */
    for (guard = 0; guard < t->n; guard++) {
        int prev = (seg - 1 + t->n) % t->n;
        if (remain <= t->seg_len[prev]) {
            seg = prev;
            break;
        }
        remain -= t->seg_len[prev];
        seg = prev;
    }
    along = 1.0f - remain / t->seg_len[seg];        /* 0..1 within seg */
    nxt = (seg + 1) % t->n;

    cx = t->px[seg] + (t->px[nxt] - t->px[seg]) * along;
    cz = t->pz[seg] + (t->pz[nxt] - t->pz[seg]) * along;
    lx = -t->dz[seg];
    lz =  t->dx[seg];

    k->x = cx + lx * side;
    k->z = cz + lz * side;
    k->heading = atan2f(t->dz[seg], t->dx[seg]);
    k->speed = 0.0f;

    track_locate(t, k->x, k->z, -1, &k->seg, &frac, &k->lat, &k->y);
    k->prog_raw = (float)k->seg + frac;
    k->total_progress = (k->prog_raw > (float)t->n * 0.5f)
                            ? k->prog_raw - (float)t->n
                            : k->prog_raw;
    k->lap = (int)floorf(k->total_progress / (float)t->n);
}

void game_init(Game *g, const GameConfig *cfg)
{
    int i, r;

    memset(g, 0, sizeof(*g));
    g->cfg = *cfg;
    if (g->cfg.n_humans < 1) g->cfg.n_humans = 1;
    if (g->cfg.n_humans > MAX_HUMANS) g->cfg.n_humans = MAX_HUMANS;

    track_init(&g->track, cfg->track_id);

    for (i = 0; i < NUM_KARTS; i++) {
        Kart *k = &g->karts[i];
        if (i < g->cfg.n_humans) {
            k->human = i;
            k->spec = g->cfg.spec[i] % SPEC_COUNT;
            if (k->spec < 0) k->spec = 0;
            k->paint_idx = ((g->cfg.paint[i] % PAINT_COUNT) + PAINT_COUNT)
                           % PAINT_COUNT;
            k->gearbox = (g->cfg.gearbox[i] == GEARBOX_MANUAL)
                             ? GEARBOX_MANUAL : GEARBOX_AUTO;
            k->tire = ((g->cfg.tire[i] % TIRE_COMPOUNDS) + TIRE_COMPOUNDS)
                      % TIRE_COMPOUNDS;
            /* A human never runs the AI driver, but give it sane values
             * anyway: memset leaves zero skill and zero confidence, which
             * read as "brake for everything" if anything ever asks. */
            k->ai_skill = 1.0f;
            k->cur_corner = -1;
            for (r = 0; r < TRACK_MAX_CORNERS; r++)
                k->corner_conf[r] = 1.0f;
        } else {
            int ai_no = i - g->cfg.n_humans;
            const AIStrategy *st;
            int c;

            k->human = -1;
            k->spec = ai_no % SPEC_COUNT;
            k->paint_idx = (i * 3 + 2) % PAINT_COUNT;
            k->strategy = ai_no % AI_STRATEGY_COUNT;
            st = &ai_strategies[k->strategy];
            k->ai_line = st->line_bias * g->track.road_half * 0.75f;
            k->line_target = k->ai_line;
            /* a little spread inside each strategy so two drivers on the
             * same sheet are still individuals */
            k->ai_skill = 0.95f + 0.02f * (float)((ai_no * 5) % 4);
            for (c = 0; c < TRACK_MAX_CORNERS; c++)
                k->corner_conf[c] = st->conf_start;
            k->cur_corner = -1;
            /* AI drive their own gearbox by hand, and pick rubber to suit
             * how they race: the aggressive sheets take softs */
            k->gearbox = GEARBOX_MANUAL;
            k->tire = (k->strategy == AI_LATE || k->strategy == AI_CHARGER)
                          ? TIRE_SOFT
                          : (k->strategy == AI_CRUISER ? TIRE_HARD
                                                       : TIRE_MEDIUM);
        }
        /* humans start at the back of the grid */
        kart_place_on_grid(g, k,
                           (k->human >= 0)
                               ? NUM_KARTS - g->cfg.n_humans + k->human
                               : i - g->cfg.n_humans);
        k->rank = i + 1;
        k->gear = 0;
        k->last_checkpoint = 0;
    }

    for (i = 0; i < MAX_HUMANS; i++) {
        g->pmodel[i].pass_side = 0.0f;
        g->pmodel[i].pace = 1.0f;
    }

    for (r = 0; r < g->track.n_items; r++)
        for (i = 0; i < 3; i++)
            g->item_respawn[r][i] = 0.0f;

    g->state = STATE_COUNTDOWN;
    g->countdown = 3.2f;
}

/* ------------------------------------------------------------------ */
/* AI drivers: strategy, learning, and reacting to the humans          */
/* ------------------------------------------------------------------ */

/*
 * Find the nearest rival within `window` meters ahead or behind. Progress
 * is kept in track-segment units, so it is converted to metres with the
 * track's mean segment length. Returns the kart index or -1;
 * `want_human` restricts the search to human-driven cars.
 */
static int nearest_rival(const Game *g, const Kart *k, int ahead,
                         int want_human, float window, float *gap_out)
{
    const Track *t = &g->track;
    float m_per_seg = t->total_len / (float)t->n;
    int best = -1, i;
    float best_gap = window;

    for (i = 0; i < NUM_KARTS; i++) {
        const Kart *o = &g->karts[i];
        float gap;
        if (o == k || o->finished)
            continue;
        if (want_human && o->human < 0)
            continue;
        gap = ahead ? (o->total_progress - k->total_progress)
                    : (k->total_progress - o->total_progress);
        gap *= m_per_seg;
        if (gap > 0.0f && gap < best_gap) {
            best_gap = gap;
            best = i;
        }
    }
    if (gap_out)
        *gap_out = best_gap;
    return best;
}

#define AI_DEFEND_RANGE 18.0f    /* metres: "someone is on my bumper"  */
#define AI_ATTACK_RANGE 15.0f    /* metres: "I can have a go at them"  */

/*
 * Decide where on the road this driver wants to be, as a signed offset
 * from the centerline (positive = right, see game.h). Base is the strategy's preferred line; on top of that
 * a driver being chased by a human covers the side that human keeps
 * passing on (learned in ai_observe_humans), and a driver hunting a car
 * ahead picks the opposite side to set up a run at it.
 */
static float ai_tactical_line(const Game *g, const Kart *k)
{
    const Track *t = &g->track;
    const AIStrategy *st = &ai_strategies[k->strategy];
    float line = k->ai_line;
    float room = t->road_half * 0.70f;

    /* With nothing but air past the shoulder, everyone drives closer to
     * the middle: the racing line is not worth a trip down the mountain */
    if (!t->has_walls)
        room *= 0.55f;
    float gap;
    int who;

    /* being hunted: cover the side of the road this human keeps using */
    who = nearest_rival(g, k, 0, 1, AI_DEFEND_RANGE, &gap);
    if (who >= 0) {
        const PlayerModel *pm = &g->pmodel[g->karts[who].human];
        float close = 1.0f - gap / AI_DEFEND_RANGE;   /* 1 = on the bumper */
        float side = pm->pass_side;
        if (fabsf(side) < 0.15f)                  /* no read yet: cover the
                                                   * side they are on now */
            side = (g->karts[who].lat > k->lat) ? 1.0f : -1.0f;
        line += side * st->defend * close * room;
    }

    /* hunting: line up on the opposite side of the car ahead */
    who = nearest_rival(g, k, 1, 0, AI_ATTACK_RANGE, &gap);
    if (who >= 0) {
        float side = (g->karts[who].lat > k->lat) ? -1.0f : 1.0f;
        float close = 1.0f - gap / AI_ATTACK_RANGE;
        line += side * st->attack * close * room;
    }

    return game_clampf(line, -room, room);
}

static void ai_control(const Game *g, const Kart *k, Input *in)
{
    const Track *t = &g->track;
    const KartSpec *s = &kart_specs[k->spec];
    const AIStrategy *st = &ai_strategies[k->strategy];
    float v = fabsf(k->speed);
    float mu = s->lat_g * k->ai_skill;
    float a_brk = 0.75f * (V100 * V100) / (2.0f * s->brake_dist_100);
    float look, d, vmax_allow;
    int j, seg;

    memset(in, 0, sizeof(*in));

    /* steering: pure pursuit toward a speed-scaled lookahead point,
     * pulled in tight when curvature anywhere just ahead is high
     * (hairpins), so the target hugs the road instead of cutting it */
    {
        float cmax = t->curv[k->seg];
        seg = k->seg;
        d = 0.0f;
        for (j = 0; j < 8 && d < 18.0f; j++) {
            if (t->curv[seg] > cmax) cmax = t->curv[seg];
            d += t->seg_len[seg];
            seg = (seg + 1) % t->n;
        }
        look = game_clampf(5.0f + v * 0.50f, 6.0f, 30.0f) /
               (1.0f + cmax * 22.0f);
        if (look < 4.0f) look = 4.0f;
    }

    seg = k->seg;
    d = 0.0f;
    while (d < look) {
        d += t->seg_len[seg];
        seg = (seg + 1) % t->n;
    }
    {
        float txp = t->px[seg] - t->dz[seg] * k->line_target;
        float tzp = t->pz[seg] + t->dx[seg] * k->line_target;
        float desired = atan2f(tzp - k->z, txp - k->x);
        float diff = game_angle_wrap(desired - k->heading);
        int pinned = fabsf(k->lat) > t->wall_half - 0.6f;

        /* recovery: pinned against the barrier (or stalled) facing the
         * wrong way — back out while counter-steering, since a real
         * car cannot rotate in place */
        if ((pinned && fabsf(diff) > 1.0f) ||
            (v < 2.0f && fabsf(diff) > 1.6f)) {
            in->steer = -game_clampf(diff * 2.0f, -1.0f, 1.0f);
            in->brake = 1;                  /* reverse gear below 0.3 m/s */
            in->accel = 0;
            return;
        }
        in->steer = game_clampf(diff * 2.2f, -1.0f, 1.0f);
    }

    /*
     * Speed: look down the road and find the lowest speed this driver
     * could still brake down to in the distance available. Each corner's
     * target speed is scaled by what this driver has *learned* about that
     * specific corner, so a bend it ran wide on last lap gets approached
     * slower next time round.
     */
    vmax_allow = 1000.0f;
    seg = k->seg;
    d = 0.0f;
    for (j = 0; j < 48; j++) {
        float curv = t->curv[seg] > 1e-4f ? t->curv[seg] : 1e-4f;
        float conf = ai_corner_conf(k, t, seg);
        float vt = sqrtf(mu * GRAVITY / curv) * 0.88f * conf;
        float allowed = sqrtf(vt * vt + 2.0f * a_brk * d);
        if (allowed < vmax_allow) vmax_allow = allowed;
        d += t->seg_len[seg];
        seg = (seg + 1) % t->n;
    }

    if (v > vmax_allow) {
        in->brake = 1;
    } else if (v < vmax_allow * 0.97f) {
        in->accel = 1;
    }

    /*
     * Gears. Each strategy has its own shifting style: where in the band
     * it changes up, and how early it grabs a lower gear on the way into
     * a corner. A short-shifter rides the torque and gets out of hairpins
     * well; a driver who hangs on to the limiter keeps the top end but
     * spends more of the lap mid-shift.
     */
    if (k->shift_t <= 0.0f) {
        const KartSpec *sp = &kart_specs[k->spec];
        float next_frac = (k->gear + 1 < sp->n_gears)
                              ? v / sp->gear_top[k->gear + 1] : 0.0f;
        float low_frac = (k->gear > 0)
                             ? v / sp->gear_top[k->gear - 1] : 9.0f;

        /*
         * Every shift is judged on where the revs would land afterwards,
         * against this driver's OWN thresholds. Gear ratios are around
         * 1.6, so a short-shifter changing up at 0.87 of the band lands
         * near 0.52 — and if that is below its downshift point it changes
         * straight back, for ever. Requiring a margin on both sides is
         * what stops the box oscillating.
         */
        if (k->rev_frac > st->shift_up_frac && k->gear < sp->n_gears - 1 &&
            next_frac > st->shift_down_frac + 0.06f &&
            next_frac > BOG_FRACTION + 0.04f) {
            in->gear_up = 1;
        } else if (k->gear > 0 && low_frac < st->shift_up_frac - 0.06f &&
                   (k->rev_frac < st->shift_down_frac ||
                    (in->brake &&
                     k->rev_frac < st->shift_down_frac + 0.18f))) {
            in->gear_down = 1;   /* also comes down the box under braking */
        }
    }

    /*
     * Power-ups get spent the way a race engineer would spend them:
     * push-to-pass when the road is open and there is someone to catch,
     * fresh rubber just before a twisty stretch where grip is what
     * actually costs time. Chargers use whatever they have almost
     * immediately; drafters sit on it until a pass is genuinely on.
     */
    if (k->power_held && k->power_timer >= st->power_wait) {
        float gap;
        int target = nearest_rival(g, k, 1, 0, 30.0f, &gap);
        float curv_ahead = 0.0f, d2 = 0.0f;
        int j2, seg2 = k->seg;

        for (j2 = 0; j2 < 12 && d2 < 80.0f; j2++) {
            if (t->curv[seg2] > curv_ahead)
                curv_ahead = t->curv[seg2];
            d2 += t->seg_len[seg2];
            seg2 = (seg2 + 1) % t->n;
        }

        if (k->power_held == POWER_PUSH) {
            if (t->curv[k->seg] < 0.02f &&
                (target >= 0 || st->power_wait < 1.0f))
                in->item = 1;
        } else if (curv_ahead > 0.03f) {
            in->item = 1;
        }
    }
}

/*
 * Learning. A driver is "in" a corner while its current segment belongs
 * to one; anything that goes wrong in there (running off the road, a
 * barrier, or big understeer) marks the corner as botched. On the way out
 * the belief for that corner is updated: knocked down after a mistake,
 * nudged up after a clean pass. Over three laps this is visible as the
 * late-brakers tidying up and the cautious drivers finding pace.
 */
static void ai_learn(Game *g, Kart *k)
{
    const Track *t = &g->track;
    const AIStrategy *st = &ai_strategies[k->strategy];
    int c = t->corner_id[k->seg];

    /*
     * What counts as a mistake: putting wheels off the road, hitting a
     * barrier, or a genuine big slide. Ordinary understeer at the limit
     * does not count — the steering controller asks for more yaw than
     * grip allows in every tight corner, and punishing that would teach
     * the drivers to crawl.
     */
    if (k->cur_corner >= 0) {
        if (k->hit_wall || fabsf(k->lat) > t->road_half + 0.4f ||
            k->slip > 0.85f)
            k->corner_fault = 1;
    }

    if (c == k->cur_corner)
        return;

    /* left a corner: fold the result into what this driver believes */
    if (k->cur_corner >= 0 && k->cur_corner < t->n_corners) {
        float *conf = &k->corner_conf[k->cur_corner];
        /* a quick human raises the whole field's ambition, a slow one
         * lets it relax — the grid tunes itself to who it is racing */
        float pace = 1.0f;
        int h;
        for (h = 0; h < g->cfg.n_humans; h++)
            if (g->pmodel[h].pace > pace)
                pace = g->pmodel[h].pace;
        {
            float ceiling = st->conf_max *
                            game_clampf(0.97f + 0.10f * (pace - 1.0f),
                                        0.95f, 1.10f);
            if (k->corner_fault) {
                *conf = game_clampf(*conf * (1.0f - st->learn_down),
                                    0.72f, ceiling);
                k->mistakes++;
            } else {
                *conf = game_clampf(*conf * (1.0f + st->learn_up),
                                    0.72f, ceiling);
            }
            k->learn_events++;
        }
    }

    k->cur_corner = c;
    k->corner_fault = 0;
}

/*
 * Watch the humans so the AI has something to adapt to: which side they
 * complete passes on, and how their pace compares with the leading AI.
 */
static void ai_observe_humans(Game *g, float dt)
{
    int h, i;

    for (h = 0; h < g->cfg.n_humans; h++) {
        Kart *hk = &g->karts[h];
        PlayerModel *pm = &g->pmodel[h];
        float best_ai = -1e9f;

        for (i = 0; i < NUM_KARTS; i++) {
            Kart *ai = &g->karts[i];
            if (ai->human >= 0)
                continue;
            if (ai->total_progress > best_ai)
                best_ai = ai->total_progress;

            /* a pass completed this frame, close enough to be a real
             * wheel-to-wheel move rather than a lap gap */
            if (hk->prev_progress < ai->prev_progress &&
                hk->total_progress > ai->total_progress &&
                fabsf(hk->total_progress - ai->total_progress) *
                    (g->track.total_len / (float)g->track.n) < 12.0f) {
                float side = (hk->lat > ai->lat) ? 1.0f : -1.0f;
                pm->pass_side = pm->pass_side * 0.7f + side * 0.3f;
                pm->passes++;
            }
        }

        /* pace: how the human's progress compares with the best AI's,
         * smoothed hard so it tracks the race rather than one corner */
        if (g->race_t > 1.0f && best_ai > 1.0f) {
            float ratio = game_clampf(hk->total_progress / best_ai,
                                      0.5f, 1.5f);
            pm->pace += (ratio - pm->pace) * game_clampf(dt * 0.5f,
                                                         0.0f, 1.0f);
        }
    }
}

/*
 * Engine trim for an AI car. This deliberately does NOT rubber-band: a
 * driver who is behind does not quietly gain horsepower, because that is
 * what makes a race feel scripted. The field stays together because the
 * cars are similar and the drivers learn, not because the game is
 * dragging them along on elastic.
 */
static float ai_power_scale(const Game *g, const Kart *k)
{
    (void)g;
    return k->ai_skill * ai_strategies[k->strategy].power;
}

static void kart_step(Game *g, Kart *k, const Input *in, float dt,
                      float power_scale)
{
    const Track *t = &g->track;
    const KartSpec *s = &kart_specs[k->spec];
    int offroad = fabsf(k->lat) > t->road_half + 0.3f;
    float grip = offroad ? s->offroad_grip : 1.0f;
    float mu_a = s->lat_g * GRAVITY * grip * tire_grip_mult(k->tire);
    float cd_a = s->cd_a * tire_drag_mult(k->tire);
    float P = s->power_hp * HP_TO_W * DRIVE_EFF * grip * power_scale;
    float steer = game_clampf(in->steer, -1.0f, 1.0f);
    float v = k->speed;
    float a = 0.0f;
    int was_inside = fabsf(k->lat) <= t->wall_half;

    k->power_fired = 0;
    k->hit_wall = 0;
    k->got_item = 0;
    k->respawned = 0;

    /* ---- gearbox -------------------------------------------------- */
    if (k->gear < 0) k->gear = 0;
    if (k->gear >= s->n_gears) k->gear = s->n_gears - 1;
    k->rev_frac = fabsf(v) / s->gear_top[k->gear];

    if (k->shift_t > 0.0f) {
        k->shift_t -= dt;
    } else {
        int want_up = 0, want_down = 0;

        if (k->gearbox == GEARBOX_MANUAL) {
            /* edge-triggered, so holding the key does not run through the
             * whole gearbox in three frames */
            want_up   = in->gear_up   && !k->prev_up_btn;
            want_down = in->gear_down && !k->prev_down_btn;
        } else {
            /*
             * Automatic. Both decisions are checked against where the
             * revs would land AFTER the shift, otherwise the box hunts:
             * upshifting at the top of one gear can drop you straight
             * below the downshift threshold of the next, and back again,
             * for ever.
             */
            float next_frac = (k->gear + 1 < s->n_gears)
                                  ? fabsf(v) / s->gear_top[k->gear + 1]
                                  : 0.0f;
            float low_frac = (k->gear > 0)
                                 ? fabsf(v) / s->gear_top[k->gear - 1]
                                 : 9.0f;
            want_up   = (k->rev_frac > 0.95f &&
                         next_frac > BOG_FRACTION + 0.08f);
            want_down = (k->rev_frac < 0.38f && low_frac < 0.92f);
        }

        if (want_up && k->gear < s->n_gears - 1) {
            k->gear++;
            k->shift_t = SHIFT_TIME +
                         ((k->gearbox == GEARBOX_AUTO) ? 0.06f : 0.0f);
        } else if (want_down && k->gear > 0) {
            k->gear--;
            k->shift_t = SHIFT_TIME +
                         ((k->gearbox == GEARBOX_AUTO) ? 0.06f : 0.0f);
        }
        k->rev_frac = fabsf(v) / s->gear_top[k->gear];
    }
    k->prev_up_btn = in->gear_up;
    k->prev_down_btn = in->gear_down;

    /* drive is cut mid-shift, and where you are in the gear decides how
     * much of the engine you actually have */
    if (k->shift_t > 0.0f)
        P = 0.0f;
    else
        P *= gear_power_scale(k->rev_frac);

    /* deployed power-ups: a bounded engine boost, and/or fresh rubber */
    if (k->push_t > 0.0f) {
        P *= PUSH_POWER;
        k->push_t -= dt;
    }
    if (k->grip_t > 0.0f) {
        mu_a *= TIRE_GRIP;
        k->grip_t -= dt;
    }

    /* --- longitudinal forces --- */
    {
        float dirdot = cosf(k->heading) * t->dx[k->seg] +
                       sinf(k->heading) * t->dz[k->seg];
        a += -GRAVITY * t->slope[k->seg] * dirdot;   /* road grade */
    }
    if (in->accel && !in->brake) {
        float a_drive = P / (s->mass_kg * (fabsf(v) > 3.0f ? fabsf(v) : 3.0f));
        float cap = 0.9f * mu_a;
        if (a_drive > cap) a_drive = cap;
        a += a_drive;
    }
    /* drag + rolling resistance oppose motion */
    if (fabsf(v) > 0.2f) {
        float a_res = (0.5f * RHO_AIR * cd_a * v * v) / s->mass_kg +
                      CRR * GRAVITY;
        a += (v > 0.0f) ? -a_res : a_res;
    }
    if (in->brake) {
        if (v > 0.3f) {
            a -= grip * (V100 * V100) / (2.0f * s->brake_dist_100);
        } else if (!in->accel) {
            /* reverse gear, gently */
            v += (-6.0f - v) * 1.2f * dt;
        }
    }
    v += a * dt;
    v = game_clampf(v, -10.0f, 90.0f);

    /*
     * Handbrake. It does what a handbrake does — locks the rear axle so
     * the car rotates, and scrubs speed for the privilege. There is no
     * mini-turbo reward for using it: sliding a car is slow, and
     * pretending otherwise was the most arcade thing in here.
     */
    if (!k->drifting) {
        if (in->hop && fabsf(steer) > 0.2f && v > 8.0f)
            k->drifting = (steer > 0.0f) ? 1 : -1;
    } else if (!in->hop || v < 5.0f) {
        k->drifting = 0;
    }

    /* --- steering: bicycle model, grip-capped yaw --- */
    {
        float delta_max = 0.48f / (1.0f + fabsf(v) * 0.02f);
        float yaw_cmd = v * tanf(steer * delta_max) / s->wheelbase;
        float yaw_cap = mu_a / (fabsf(v) > 0.5f ? fabsf(v) : 0.5f);
        float yaw;

        if (k->drifting) {
            yaw_cmd *= 1.35f;
            yaw_cap *= 1.5f;
            v -= 0.55f * mu_a * dt;                  /* sliding is slow */
            k->slip = fmaxf(k->slip, 0.7f);
        }

        if (yaw_cmd > yaw_cap) {
            yaw = yaw_cap;
            k->slip = game_clampf((yaw_cmd - yaw_cap) / yaw_cap, 0.0f, 1.0f);
            v -= 0.25f * mu_a * k->slip * dt;        /* understeer scrub */
        } else if (yaw_cmd < -yaw_cap) {
            yaw = -yaw_cap;
            k->slip = game_clampf((-yaw_cmd - yaw_cap) / yaw_cap, 0.0f, 1.0f);
            v -= 0.25f * mu_a * k->slip * dt;
        } else {
            yaw = yaw_cmd;
            k->slip *= (1.0f - 4.0f * dt);
        }
        k->heading = game_angle_wrap(k->heading + yaw * dt);
    }
    k->steer_vis += (steer - k->steer_vis) * 10.0f * dt;
    k->speed = v;

    /* --- integrate --- */
    k->x += cosf(k->heading) * v * dt;
    k->z += sinf(k->heading) * v * dt;

    /* --- track relation --- */
    {
        int seg;
        float frac, lat, y, newp, d;

        track_locate(t, k->x, k->z, k->seg, &seg, &frac, &lat, &y);

        if (t->has_walls) {
            /* guardrail: the car is held on the road, and pays in speed */
            if (fabsf(lat) > t->wall_half) {
                float clamped = game_clampf(lat, -t->wall_half, t->wall_half);
                float excess = lat - clamped;
                k->x += t->dz[seg] * excess;
                k->z -= t->dx[seg] * excess;
                k->speed *= (1.0f - 2.5f * dt);
                if (was_inside)
                    k->hit_wall = 1;
                lat = clamped;
            }
        } else if (fabsf(lat) > t->wall_half) {
            /* No barrier here: past the shoulder there is nothing but air.
             * The car drops for a moment, so you see it go, and is then
             * set back down at the last checkpoint it passed. */
            k->fall_t += dt;
            k->y -= (3.0f + 12.0f * k->fall_t) * dt;
            k->speed *= (1.0f - 1.2f * dt);
        } else {
            k->fall_t = 0.0f;
        }

        newp = (float)seg + frac;
        d = newp - k->prog_raw;
        if (d >  (float)t->n * 0.5f) d -= (float)t->n;
        if (d < -(float)t->n * 0.5f) d += (float)t->n;
        k->total_progress += d;
        k->prog_raw = newp;
        k->seg = seg;
        k->lat = lat;
        if (k->fall_t <= 0.0f)
            k->y = y;          /* on the road; while falling, k->y is the
                                * height the fall branch is writing */
        k->lap = (int)floorf(k->total_progress / (float)t->n);

        /* remember the last checkpoint reached while safely on the road */
        if (k->fall_t <= 0.0f && fabsf(lat) <= t->road_half + 1.0f) {
            int cp = track_checkpoint_for(t, seg);
            if (cp >= 0)
                k->last_checkpoint = cp;
        }

        /* power-up panels: three across the road on marked rows. Which
         * of the two a panel carries is fixed, so a driver can aim for
         * the one they actually want instead of drawing a lottery. */
        {
            int row = track_item_row(t, seg);
            if (row >= 0 && !k->power_held) {
                int b;
                for (b = 0; b < 3; b++) {
                    float blat = ((float)b - 1.0f) * 0.55f * t->road_half;
                    if (fabsf(lat - blat) < 1.4f &&
                        g->item_respawn[row][b] <= 0.0f) {
                        k->power_held = ((row + b) & 1) ? POWER_TIRES
                                                       : POWER_PUSH;
                        k->got_item = 1;
                        g->item_respawn[row][b] = 6.0f;
                        break;
                    }
                }
            }
        }
    }

    /* --- fished out of the void, back at the last checkpoint --- */
    if (k->fall_t > 1.1f) {
        int cp = k->last_checkpoint;
        int cseg;
        float back;

        /* `%` keeps the sign of its left operand, so it only guards the
         * top end; clamp both ways or a negative index reads off the
         * front of the array. */
        if (t->n_checkpoints > 0) {
            if (cp < 0) cp = 0;
            if (cp >= t->n_checkpoints) cp = t->n_checkpoints - 1;
            cseg = t->checkpoint_seg[cp];
        } else {
            cseg = 0;
        }
        k->x = t->px[cseg];
        k->z = t->pz[cseg];
        k->y = t->py[cseg];
        k->heading = atan2f(t->dz[cseg], t->dx[cseg]);
        k->speed = 0.0f;
        k->gear = 0;
        k->shift_t = 0.0f;
        k->drifting = 0;
        k->slip = 0.0f;
        k->fall_t = 0.0f;
        k->respawned = 1;
        k->seg = cseg;
        k->lat = 0.0f;

        /* No free progress. Rebuilding total_progress as lap*n + cseg
         * looks right but is not: the lap counter can tick over while the
         * car is in the air, and the checkpoint it is being returned to
         * is then still back on the previous lap — which handed out most
         * of a free lap for going over the edge just before the line.
         * Instead move progress by the signed arc actually given up, the
         * same way the on-track case does, and re-derive the lap from it.
         */
        back = (float)cseg - k->prog_raw;
        if (back >  (float)t->n * 0.5f) back -= (float)t->n;
        if (back < -(float)t->n * 0.5f) back += (float)t->n;
        if (back > 0.0f) back = 0.0f;     /* a respawn never gains ground */
        k->total_progress += back;
        k->prog_raw = (float)cseg;
        k->lap = (int)floorf(k->total_progress / (float)t->n);
        if (k->lap < 0) k->lap = 0;
    }

    /* --- deploy a held power-up --- */
    if (in->item && !k->prev_item_btn && k->power_held) {
        if (k->power_held == POWER_PUSH)
            k->push_t = fmaxf(k->push_t, PUSH_SECONDS);
        else
            k->grip_t = fmaxf(k->grip_t, TIRE_SECONDS);
        k->power_held = POWER_NONE;
        k->power_fired = 1;
    }
    k->prev_item_btn = in->item;
}

static void resolve_kart_collisions(Game *g)
{
    int i, j;
    for (i = 0; i < NUM_KARTS; i++) {
        for (j = i + 1; j < NUM_KARTS; j++) {
            Kart *a = &g->karts[i], *b = &g->karts[j];
            float ddx = b->x - a->x;
            float ddz = b->z - a->z;
            float d2 = ddx * ddx + ddz * ddz;
            const float min_d = 1.9f;
            if (d2 < min_d * min_d && d2 > 1e-6f) {
                float d = sqrtf(d2);
                float push = 0.5f * (min_d - d);
                float nx = ddx / d, nz = ddz / d;
                a->x -= nx * push; a->z -= nz * push;
                b->x += nx * push; b->z += nz * push;
                a->speed *= 0.995f;
                b->speed *= 0.995f;
                /* remember who rubs panels: AI leave more room to a
                 * human who keeps leaning on them */
                if (a->human >= 0 && b->human < 0)
                    g->pmodel[a->human].contacts++;
                else if (b->human >= 0 && a->human < 0)
                    g->pmodel[b->human].contacts++;
            }
        }
    }
}

static void update_ranks(Game *g)
{
    int order[NUM_KARTS];
    int i, j;

    for (i = 0; i < NUM_KARTS; i++)
        order[i] = i;

    for (i = 1; i < NUM_KARTS; i++) {
        int oi = order[i];
        float ki = g->karts[oi].finished
                       ? 1.0e6f - (float)g->karts[oi].final_rank
                       : g->karts[oi].total_progress;
        j = i - 1;
        while (j >= 0) {
            int oj = order[j];
            float kj = g->karts[oj].finished
                           ? 1.0e6f - (float)g->karts[oj].final_rank
                           : g->karts[oj].total_progress;
            if (kj >= ki)
                break;
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = oi;
    }

    for (i = 0; i < NUM_KARTS; i++)
        g->karts[order[i]].rank = i + 1;
}

void game_update(Game *g, const Input inputs[MAX_HUMANS], float dt)
{
    int i, r;

    if (!(dt > 0.0f)) return;      /* also rejects NaN */
    if (dt > 0.1f) dt = 0.1f;

    for (r = 0; r < g->track.n_items; r++)
        for (i = 0; i < 3; i++)
            if (g->item_respawn[r][i] > 0.0f)
                g->item_respawn[r][i] -= dt;

    if (g->state == STATE_COUNTDOWN) {
        g->countdown -= dt;
        if (g->countdown <= 0.0f) {
            g->state = STATE_RACING;
            g->race_t = 0.0f;
        }
        for (i = 0; i < NUM_KARTS; i++)
            g->karts[i].steer_vis *= 0.9f;
        update_ranks(g);
        return;
    }

    g->race_t += dt;

    for (i = 0; i < NUM_KARTS; i++)
        g->karts[i].prev_progress = g->karts[i].total_progress;

    for (i = 0; i < NUM_KARTS; i++) {
        Kart *k = &g->karts[i];
        Input in;
        float scale = 1.0f;

        if (k->human >= 0) {
            /* A finished car coasts. It used to fall through to the AI
             * branch, which then drove it on AI fields a human kart never
             * had — zero skill and zero corner confidence, i.e. brake for
             * everything — so a player who crossed the line watched their
             * car stand on the brakes and reverse back down the circuit
             * through the cars still racing. */
            if (k->finished) {
                memset(&in, 0, sizeof(in));
            } else {
                in = inputs[k->human];
            }
        } else {
            /* ease onto the tactical line rather than darting sideways,
             * and give more room to a human who has been leaning on us */
            float want = ai_tactical_line(g, k);
            int h;
            for (h = 0; h < g->cfg.n_humans; h++) {
                if (g->pmodel[h].contacts > 6) {
                    float shy = game_clampf(
                        (float)g->pmodel[h].contacts * 0.02f, 0.0f, 0.5f);
                    want *= (1.0f - shy * 0.4f);
                    break;
                }
            }
            k->line_target += (want - k->line_target) *
                              game_clampf(dt * 1.8f, 0.0f, 1.0f);
            ai_control(g, k, &in);
            scale = ai_power_scale(g, k);
            k->power_timer = k->power_held ? k->power_timer + dt : 0.0f;
        }
        kart_step(g, k, &in, dt, scale);

        if (k->human < 0)
            ai_learn(g, k);

        if (!k->finished && k->lap >= g->track.laps) {
            k->finished = 1;
            k->finish_time = g->race_t;
            g->finish_count++;
            k->final_rank = g->finish_count;
            if (k->human >= 0) {
                g->humans_done++;
                if (g->humans_done >= g->cfg.n_humans)
                    g->state = STATE_FINISHED;
            }
        }
    }

    resolve_kart_collisions(g);
    ai_observe_humans(g, dt);
    update_ranks(g);
}
