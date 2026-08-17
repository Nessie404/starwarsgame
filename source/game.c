/*
 * WiiKart simulation: kart physics, drifting, boost, AI drivers,
 * lap counting and ranking. Platform-independent C99 (libm only).
 */
#include <math.h>
#include <string.h>
#include "game.h"

#define PI_F 3.14159265358979f

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

static void kart_place_on_grid(Game *g, Kart *k, int grid_slot)
{
    const Track *t = &g->track;
    float d0x = t->dx[0], d0z = t->dz[0];
    float lx = -d0z, lz = d0x;
    int   row = grid_slot;
    float side = (grid_slot % 2 == 0) ? 1.8f : -1.8f;
    float back = 5.0f + (float)row * 3.4f;
    float frac;

    k->x = t->px[0] - d0x * back + lx * side;
    k->z = t->pz[0] - d0z * back + lz * side;
    k->heading = atan2f(d0z, d0x);
    k->speed = 0.0f;

    track_locate(t, k->x, k->z, -1, &k->seg, &frac, &k->lat);
    k->prog_raw = (float)k->seg + frac;
    /* Grid is just behind the start line, so raw progress is near n;
     * shift so that crossing the line takes total_progress past 0. */
    k->total_progress = (k->prog_raw > (float)t->n * 0.5f)
                            ? k->prog_raw - (float)t->n
                            : k->prog_raw;
    k->lap = (int)floorf(k->total_progress / (float)t->n);
}

void game_init(Game *g)
{
    static const float ai_lines[NUM_KARTS - 1]  = { -2.2f, -0.8f, 0.8f, 2.2f };
    static const float ai_skills[NUM_KARTS - 1] = { 0.93f, 0.97f, 1.00f, 1.04f };
    int i;

    memset(g, 0, sizeof(*g));
    track_init(&g->track);

    for (i = 0; i < NUM_KARTS; i++) {
        Kart *k = &g->karts[i];
        k->is_player = (i == 0);
        if (i > 0) {
            k->ai_line  = ai_lines[i - 1];
            k->ai_skill = ai_skills[i - 1];
        }
        /* Player starts at the back of the grid. */
        kart_place_on_grid(g, k, k->is_player ? NUM_KARTS - 1 : i - 1);
        k->rank = i + 1;
    }

    g->state = STATE_COUNTDOWN;
    g->countdown = 3.2f;
    g->race_t = 0.0f;
    g->finish_count = 0;
}

/* ------------------------------------------------------------------ */

static void ai_control(const Game *g, const Kart *k, Input *in)
{
    const Track *t = &g->track;
    int look = 3 + (int)(fabsf(k->speed) * 0.16f);
    int ti = ((k->seg + look) % t->n + t->n) % t->n;
    float txp = t->px[ti] - t->dz[ti] * k->ai_line;
    float tzp = t->pz[ti] + t->dx[ti] * k->ai_line;
    float desired = atan2f(tzp - k->z, txp - k->x);
    float diff = game_angle_wrap(desired - k->heading);

    memset(in, 0, sizeof(*in));
    in->steer = game_clampf(diff * 2.0f, -1.0f, 1.0f);
    in->accel = 1;

    if (fabsf(diff) > 0.55f && k->speed > 0.55f * KART_VMAX) {
        in->accel = 0;
        if (fabsf(diff) > 0.9f)
            in->brake = 1;
    }
}

/* Rubber-banding: trailing AI gets a small speed bonus, leading AI is
 * reined in slightly, keeping the pack near the player. */
static float ai_rubber_band(const Game *g, const Kart *k)
{
    float gap = g->karts[0].total_progress - k->total_progress;
    return game_clampf(1.0f + gap * 0.002f, 0.90f, 1.12f);
}

static void kart_step(Game *g, Kart *k, const Input *in, float dt,
                      float vmax_scale)
{
    const Track *t = &g->track;
    int offroad = fabsf(k->lat) > t->road_half + 0.4f;
    float vmax = KART_VMAX * vmax_scale;
    float steer = game_clampf(in->steer, -1.0f, 1.0f);
    float turn_f;

    k->just_boosted = 0;
    k->hit_wall = 0;

    if (k->boost_t > 0.0f)
        vmax *= KART_BOOST_MULT;
    else if (offroad)
        vmax *= KART_OFFROAD_MULT;

    /* --- longitudinal --- */
    if (k->boost_t > 0.0f) {
        k->speed += (vmax - k->speed) * 2.5f * dt;
        k->boost_t -= dt;
    } else if (in->accel) {
        k->speed += (vmax - k->speed) * 1.1f * dt;
    } else {
        k->speed -= k->speed * 0.8f * dt;
    }
    if (in->brake) {
        if (k->speed > 0.5f)
            k->speed -= 28.0f * dt;
        else
            k->speed += (-KART_REVERSE_MAX - k->speed) * 2.0f * dt;
    }

    /* --- drift (Mario-Kart-style: locked direction, charge, mini-turbo) */
    if (!k->drifting) {
        if (in->hop && fabsf(steer) > 0.25f && k->speed > 0.45f * KART_VMAX)
            k->drifting = (steer > 0.0f) ? 1 : -1;
    } else {
        if (!in->hop || k->speed < 0.30f * KART_VMAX) {
            if (k->drift_charge > 2.2f)
                k->boost_t = fmaxf(k->boost_t, 1.5f);
            else if (k->drift_charge > 1.0f)
                k->boost_t = fmaxf(k->boost_t, 0.8f);
            if (k->drift_charge > 1.0f)
                k->just_boosted = 1;
            k->drifting = 0;
            k->drift_charge = 0.0f;
        } else {
            k->drift_charge += dt * (0.7f + 0.6f * fabsf(steer));
        }
    }
    if (k->drifting)
        steer = game_clampf((float)k->drifting * 0.9f + steer * 0.6f,
                            -1.5f, 1.5f);

    /* --- steering ---
     * Turn response peaks at mid speed and softens near top speed. */
    turn_f = fabsf(k->speed) / (6.0f + 0.045f * k->speed * k->speed);
    k->heading += steer * 2.4f * turn_f * dt * (k->speed < 0.0f ? -1.0f : 1.0f);
    k->heading = game_angle_wrap(k->heading);
    k->steer_vis += (steer - k->steer_vis) * 10.0f * dt;

    /* --- integrate --- */
    k->x += cosf(k->heading) * k->speed * dt;
    k->z += sinf(k->heading) * k->speed * dt;

    /* --- track relation: progress, walls, pads --- */
    {
        int seg;
        float frac, lat, newp, d;

        track_locate(t, k->x, k->z, k->seg, &seg, &frac, &lat);

        /* invisible wall at the edge of the grass */
        if (fabsf(lat) > t->wall_half) {
            float clamped = game_clampf(lat, -t->wall_half, t->wall_half);
            float excess = lat - clamped;
            k->x += t->dz[seg] * excess;   /* -= left * excess */
            k->z -= t->dx[seg] * excess;
            k->speed *= 0.96f;
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
        k->lap = (int)floorf(k->total_progress / (float)t->n);

        if (track_is_pad_seg(t, seg) && fabsf(lat) <= t->road_half) {
            if (k->boost_t < 0.5f)
                k->just_boosted = 1;
            k->boost_t = fmaxf(k->boost_t, 1.1f);
        }
    }
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
            const float min_d = 1.7f;
            if (d2 < min_d * min_d && d2 > 1e-6f) {
                float d = sqrtf(d2);
                float push = 0.5f * (min_d - d);
                float nx = ddx / d, nz = ddz / d;
                a->x -= nx * push; a->z -= nz * push;
                b->x += nx * push; b->z += nz * push;
                a->speed *= 0.99f;
                b->speed *= 0.99f;
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

    /* Finished karts rank by finish order; the rest by progress. */
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

void game_update(Game *g, const Input *player_in, float dt)
{
    int i;

    if (dt <= 0.0f) return;
    if (dt > 0.1f) dt = 0.1f;   /* clamp hitches so physics stays sane */

    if (g->state == STATE_COUNTDOWN) {
        g->countdown -= dt;
        if (g->countdown <= 0.0f) {
            g->state = STATE_RACING;
            g->race_t = 0.0f;
        }
        /* karts are held on the grid; let visuals settle */
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

        if (k->is_player && !k->finished) {
            in = *player_in;
        } else {
            ai_control(g, k, &in);
            if (!k->is_player)
                scale = k->ai_skill * ai_rubber_band(g, k);
        }
        kart_step(g, k, &in, dt, scale);

        if (!k->finished && k->lap >= RACE_LAPS) {
            k->finished = 1;
            k->finish_time = g->race_t;
            g->finish_count++;
            k->final_rank = g->finish_count;
            if (k->is_player)
                g->state = STATE_FINISHED;
        }
    }

    resolve_kart_collisions(g);
    update_ranks(g);
}
