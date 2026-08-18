/*
 * The chase camera.
 *
 * Three things are going on at once, and they are kept separate so each
 * can be tuned (and tested) on its own:
 *
 *   the swing   — reversing does not just push the camera backwards, it
 *                 walks it round the car until it is at the nose looking
 *                 back the way the car is actually going. It is driven by
 *                 reverse speed through a dead zone, so rolling back a few
 *                 centimetres at a junction leaves the view alone, and it
 *                 is both smoothed and rate-limited, so crossing through
 *                 zero can never snap it round or set it hunting.
 *
 *   the pitch   — on a climb the camera drops behind the car and looks up
 *                 the road; on a descent it rises and looks down it. It
 *                 copies a configurable fraction of the road's own pitch,
 *                 smoothed, and clamped hard at both ends so it can never
 *                 end up studying the pavement or the sky.
 *
 *   the easing  — the camera chases a target pose rather than being
 *                 nailed to it, and is never allowed below a minimum
 *                 height over whatever ground is under it.
 */
#include <math.h>
#include "camera.h"

#define CAM_PI 3.14159265358979f

static float cam_clampf(float v, float lo, float hi)
{
    if (!(v >= lo)) return lo;      /* false for NaN as well as for v < lo */
    if (v > hi) return hi;
    return v;
}

/*
 * `smoothing` is the fraction of the remaining error still left one
 * second later, which is a frame-rate independent way to say "how
 * quickly": 0.006 is brisk, 0.5 is lazy. 0 snaps.
 */
static float cam_blend(float smoothing, float dt)
{
    if (smoothing <= 0.0f) return 1.0f;
    if (smoothing >= 1.0f) return 0.0f;
    return 1.0f - powf(smoothing, dt);
}

/* ground height under a point, using the car's segment as the hint */
static float ground_at(const Track *t, float x, float z, int hint)
{
    int seg;
    float frac, lat, y;
    track_locate(t, x, z, hint, &seg, &frac, &lat, &y);
    return y;
}

/*
 * How much of the view should be swung round, 0..1, from the car's signed
 * speed. Forward motion and anything inside the dead zone is 0.
 */
static float reverse_fraction(const GameSettings *s, float speed)
{
    float dead = s->cam_reverse_deadzone_mps;
    float full = s->cam_reverse_full_mps;
    float back = -speed;                    /* positive when reversing */

    if (!(back > dead))
        return 0.0f;
    if (full <= dead)
        return 1.0f;
    return cam_clampf((back - dead) / (full - dead), 0.0f, 1.0f);
}

/*
 * The pose the camera wants this frame: eye and look-at, both derived
 * from one swung axis so the two can never disagree about which way the
 * car is travelling.
 */
static void desired_pose(const CameraState *c, const GameSettings *s,
                         const Kart *k, float pitch,
                         float *ex, float *ey, float *ez,
                         float *lx, float *ly, float *lz)
{
    float axis = k->heading + c->orbit;
    float ax = cosf(axis), az = sinf(axis);
    float d = s->cam_distance_m;
    float h = s->cam_height_m;
    float sp = fabsf(k->speed);
    float look = s->cam_look_ahead_m + s->cam_look_ahead_per_mps * sp;
    float cp = cosf(pitch), sn = sinf(pitch);
    float back, up;

    if (look > s->cam_look_ahead_max_m)
        look = s->cam_look_ahead_max_m;

    /*
     * Rotate the offset with the road instead of hanging it off world
     * vertical: on a climb that puts the camera further back and lower,
     * which is what keeps the crest in shot rather than a wall of tarmac.
     */
    back = d * cp + h * sn;
    up   = h * cp - d * sn;

    *ex = k->x - ax * back;
    *ez = k->z - az * back;
    *ey = k->y + up;

    /* the aim point rides the same pitch, so the road ahead stays framed */
    *lx = k->x + ax * look * cp;
    *lz = k->z + az * look * cp;
    *ly = k->y + s->cam_look_height_m + look * sn;
}

void camera_reset(CameraState *c, const GameSettings *s,
                  const Kart *k, const Track *t)
{
    float ex, ey, ez, lx, ly, lz;
    float ground;

    c->orbit = 0.0f;
    c->pitch = 0.0f;
    desired_pose(c, s, k, 0.0f, &ex, &ey, &ez, &lx, &ly, &lz);

    ground = ground_at(t, ex, ez, k->seg);
    if (ey < ground + s->cam_min_height_m)
        ey = ground + s->cam_min_height_m;

    c->x = ex; c->y = ey; c->z = ez;
    c->look_x = lx; c->look_y = ly; c->look_z = lz;
    c->primed = 1;
}

void camera_update(CameraState *c, const GameSettings *s,
                   const Kart *k, const Track *t, float dt)
{
    float rev, orbit_target, step, rate, blend;
    float road_pitch, pitch_limit_lo, pitch_limit_hi, pitch;
    float ex, ey, ez, lx, ly, lz, ground;

    if (!(dt > 0.0f))            /* also catches NaN */
        return;
    if (dt > 0.1f)
        dt = 0.1f;
    if (!c->primed) {
        camera_reset(c, s, k, t);
        return;
    }

    /*
     * A car that has been picked up and put back at a checkpoint has not
     * driven anywhere — easing after it would send the camera streaking
     * across the mountain. Anything further than a few car-lengths past
     * where the camera could possibly be is a teleport, so start again.
     */
    {
        float dx = k->x - c->x, dz = k->z - c->z, dy = k->y - c->y;
        float far = s->cam_distance_m * 3.0f + 20.0f;
        if (dx * dx + dz * dz + dy * dy > far * far) {
            camera_reset(c, s, k, t);
            return;
        }
    }

    /* --- the swing ------------------------------------------------- */
    rev = reverse_fraction(s, k->speed);
    orbit_target = CAM_PI * rev;

    step = (orbit_target - c->orbit) *
           cam_blend(s->cam_reverse_smoothing, dt);
    rate = s->cam_reverse_orbit_rate_dps * (CAM_PI / 180.0f) * dt;
    if (step >  rate) step =  rate;      /* never whips round, in either */
    if (step < -rate) step = -rate;      /* direction                    */
    c->orbit += step;
    c->orbit = cam_clampf(c->orbit, 0.0f, CAM_PI);

    /* --- the pitch ------------------------------------------------- */
    road_pitch = atanf(t->slope[k->seg % t->n]);
    if (c->orbit > CAM_PI * 0.5f)
        road_pitch = -road_pitch;        /* looking back down the road */
    c->pitch += (road_pitch - c->pitch) *
                cam_blend(s->cam_pitch_smoothing, dt);

    pitch_limit_lo = s->cam_pitch_min_deg * (CAM_PI / 180.0f);
    pitch_limit_hi = s->cam_pitch_max_deg * (CAM_PI / 180.0f);
    pitch = cam_clampf(c->pitch * s->cam_pitch_influence,
                       pitch_limit_lo, pitch_limit_hi);

    /* --- the easing ------------------------------------------------ */
    desired_pose(c, s, k, pitch, &ex, &ey, &ez, &lx, &ly, &lz);

    blend = cam_blend(s->cam_follow_smoothing, dt);
    c->x += (ex - c->x) * blend;
    c->y += (ey - c->y) * blend;
    c->z += (ez - c->z) * blend;

    /* the aim point is stiffer than the eye: a soft eye reads as weight,
     * a soft aim point reads as a loose steering rack */
    blend = cam_blend(s->cam_look_smoothing, dt);
    c->look_x += (lx - c->look_x) * blend;
    c->look_y += (ly - c->look_y) * blend;
    c->look_z += (lz - c->look_z) * blend;

    /* never sink into whatever is under the camera */
    ground = ground_at(t, c->x, c->z, k->seg);
    if (c->y < ground + s->cam_min_height_m)
        c->y = ground + s->cam_min_height_m;

    /* and never aim so low that the view is mostly pavement: the aim
     * point stays at least a little above the ground beneath it */
    ground = ground_at(t, c->look_x, c->look_z, k->seg);
    if (c->look_y < ground + s->cam_look_min_height_m)
        c->look_y = ground + s->cam_look_min_height_m;
}

float camera_view_pitch(const CameraState *c)
{
    float dx = c->look_x - c->x;
    float dz = c->look_z - c->z;
    float dy = c->look_y - c->y;
    float flat = sqrtf(dx * dx + dz * dz);

    if (flat < 1.0e-4f)
        return 0.0f;
    return atan2f(-dy, flat);      /* positive = looking down */
}
