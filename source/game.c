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
#define BOOST_PWR  1.35f       /* nitro power multiplier               */

const KartSpec kart_specs[SPEC_COUNT] = {
    /* name      mass    hp   brake  lat_g  CdA   wheelbase offroad */
    { "RACER",   260.f,  48.f, 30.f, 1.30f, 0.45f, 1.05f,   0.30f },
    { "SPORT",   950.f, 150.f, 37.f, 0.95f, 0.66f, 2.45f,   0.45f },
    { "RALLY",  1180.f, 220.f, 40.f, 0.88f, 0.70f, 2.60f,   0.72f },
    { "TOURER", 1350.f, 310.f, 34.f, 1.02f, 0.60f, 2.70f,   0.35f },
};

float game_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

float game_angle_wrap(float a)
{
    while (a >  PI_F) a -= 2.0f * PI_F;
    while (a < -PI_F) a += 2.0f * PI_F;
    return a;
}

/* ------------------------------------------------------------------ */
/* Spec-derived display stats                                          */
/* ------------------------------------------------------------------ */

float spec_top_speed(const KartSpec *s)
{
    float P = s->power_hp * HP_TO_W * DRIVE_EFF;
    float v = 40.0f;
    int i;
    for (i = 0; i < 40; i++) {
        float resist = 0.5f * RHO_AIR * s->cd_a * v * v +
                       CRR * s->mass_kg * GRAVITY;
        v = 0.5f * (v + P / resist);
    }
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

static void kart_place_on_grid(Game *g, Kart *k, int grid_slot)
{
    const Track *t = &g->track;
    float d0x = t->dx[0], d0z = t->dz[0];
    float lx = -d0z, lz = d0x;
    float side = (grid_slot % 2 == 0) ? 1.7f : -1.7f;
    float back = 6.0f + (float)grid_slot * 3.6f;
    float frac;

    k->x = t->px[0] - d0x * back + lx * side;
    k->z = t->pz[0] - d0z * back + lz * side;
    k->heading = atan2f(d0z, d0x);
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
        } else {
            k->human = -1;
            k->spec = (i - g->cfg.n_humans) % SPEC_COUNT;
            k->ai_line = ((i & 1) ? -1.0f : 1.0f) *
                         (0.25f + 0.10f * (float)(i % 3)) *
                         g->track.road_half;
            k->ai_skill = 0.94f + 0.03f * (float)((i * 7) % 5);
        }
        /* humans start at the back of the grid */
        kart_place_on_grid(g, k,
                           (k->human >= 0)
                               ? NUM_KARTS - g->cfg.n_humans + k->human
                               : i - g->cfg.n_humans);
        k->rank = i + 1;
    }

    for (r = 0; r < g->track.n_items; r++)
        for (i = 0; i < 3; i++)
            g->item_respawn[r][i] = 0.0f;

    g->state = STATE_COUNTDOWN;
    g->countdown = 3.2f;
}

/* ------------------------------------------------------------------ */
/* AI driver                                                           */
/* ------------------------------------------------------------------ */

static void ai_control(const Game *g, const Kart *k, Input *in)
{
    const Track *t = &g->track;
    const KartSpec *s = &kart_specs[k->spec];
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
        float txp = t->px[seg] - t->dz[seg] * k->ai_line;
        float tzp = t->pz[seg] + t->dx[seg] * k->ai_line;
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

    /* speed: braking-point logic against the corner speeds ahead */
    vmax_allow = 1000.0f;
    seg = k->seg;
    d = 0.0f;
    for (j = 0; j < 48; j++) {
        float vt = sqrtf(mu * GRAVITY / (t->curv[seg] > 1e-4f
                                             ? t->curv[seg] : 1e-4f)) * 0.88f;
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
}

static float ai_power_scale(const Game *g, const Kart *k)
{
    /* light rubber-banding against the best-placed human */
    float lead = -1e9f;
    int i;
    for (i = 0; i < g->cfg.n_humans; i++)
        if (g->karts[i].total_progress > lead)
            lead = g->karts[i].total_progress;
    return k->ai_skill *
           game_clampf(1.0f + (lead - k->total_progress) * 0.0015f,
                       0.94f, 1.08f);
}

/* ------------------------------------------------------------------ */
/* Vehicle dynamics                                                    */
/* ------------------------------------------------------------------ */

static void kart_step(Game *g, Kart *k, const Input *in, float dt,
                      float power_scale)
{
    const Track *t = &g->track;
    const KartSpec *s = &kart_specs[k->spec];
    int offroad = fabsf(k->lat) > t->road_half + 0.3f;
    float grip = offroad ? s->offroad_grip : 1.0f;
    float mu_a = s->lat_g * GRAVITY * grip;          /* lateral accel cap */
    float P = s->power_hp * HP_TO_W * DRIVE_EFF * grip * power_scale;
    float steer = game_clampf(in->steer, -1.0f, 1.0f);
    float v = k->speed;
    float a = 0.0f;
    int was_inside = fabsf(k->lat) <= t->wall_half;

    k->just_boosted = 0;
    k->hit_wall = 0;
    k->got_item = 0;

    if (k->boost_t > 0.0f) {
        P *= BOOST_PWR;
        k->boost_t -= dt;
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
        float a_res = (0.5f * RHO_AIR * s->cd_a * v * v) / s->mass_kg +
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

    /* --- handbrake drift --- */
    if (!k->drifting) {
        if (in->hop && fabsf(steer) > 0.2f && v > 8.0f)
            k->drifting = (steer > 0.0f) ? 1 : -1;
    } else if (!in->hop || v < 5.0f) {
        if (k->drift_charge > 1.2f) {
            k->boost_t = fmaxf(k->boost_t, 0.8f);
            k->just_boosted = 1;
        }
        k->drifting = 0;
        k->drift_charge = 0.0f;
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
            v -= 0.20f * mu_a * dt;                  /* scrub in the slide */
            if (fabsf(yaw_cmd) > 0.5f * yaw_cap)
                k->drift_charge += dt;
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

        /* guardrail */
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

        newp = (float)seg + frac;
        d = newp - k->prog_raw;
        if (d >  (float)t->n * 0.5f) d -= (float)t->n;
        if (d < -(float)t->n * 0.5f) d += (float)t->n;
        k->total_progress += d;
        k->prog_raw = newp;
        k->seg = seg;
        k->lat = lat;
        k->y = y;
        k->lap = (int)floorf(k->total_progress / (float)t->n);

        if (track_is_pad_seg(t, seg) && fabsf(lat) <= t->road_half) {
            if (k->boost_t < 0.4f)
                k->just_boosted = 1;
            k->boost_t = fmaxf(k->boost_t, 1.0f);
        }

        /* item boxes: three across the road on marked rows */
        {
            int row = track_item_row(t, seg);
            if (row >= 0 && !k->item_held) {
                int b;
                for (b = 0; b < 3; b++) {
                    float blat = ((float)b - 1.0f) * 0.55f * t->road_half;
                    if (fabsf(lat - blat) < 1.4f &&
                        g->item_respawn[row][b] <= 0.0f) {
                        k->item_held = 1;
                        k->got_item = 1;
                        g->item_respawn[row][b] = 4.0f;
                        break;
                    }
                }
            }
        }
    }

    /* --- use item (nitro canister) --- */
    if (in->item && !k->prev_item_btn && k->item_held) {
        k->item_held = 0;
        k->boost_t = fmaxf(k->boost_t, 1.6f);
        k->just_boosted = 1;
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

    if (dt <= 0.0f) return;
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

    for (i = 0; i < NUM_KARTS; i++) {
        Kart *k = &g->karts[i];
        Input in;
        float scale = 1.0f;

        if (k->human >= 0 && !k->finished) {
            in = inputs[k->human];
        } else {
            ai_control(g, k, &in);
            if (k->human < 0)
                scale = ai_power_scale(g, k);
        }
        kart_step(g, k, &in, dt, scale);

        if (!k->finished && k->lap >= RACE_LAPS) {
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
    update_ranks(g);
}
