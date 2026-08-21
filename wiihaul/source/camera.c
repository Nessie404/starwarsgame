/*
 * The chase camera — see camera.h for what is different from WiiKart's.
 *
 * Yards are flat but for one hill scenario, so unlike WiiKart's camera
 * this one does not follow road pitch; a future scenario with real
 * elevation could add that back the same way WiiKart does (see its
 * camera.c), reading slope from the same GroundFn rig_step already
 * samples for gravity.
 */
#include <math.h>
#include "camera.h"

#define CAM_PI 3.14159265358979f

static float cam_blend(float smoothing, float dt)
{
    if (smoothing <= 0.0f) return 1.0f;
    if (smoothing >= 1.0f) return 0.0f;
    return 1.0f - powf(smoothing, dt);
}

/* How much of the view should be swung round, 0..1, from the rig's
 * signed speed. Forward motion and anything inside the dead zone is 0.
 */
static float reverse_fraction(const HaulSettings *s, float speed)
{
    float dead = s->cam_reverse_deadzone_mps;
    float full = s->cam_reverse_full_mps;
    float back = -speed;

    if (!(back > dead))
        return 0.0f;
    if (full <= dead)
        return 1.0f;
    return haul_clampf((back - dead) / (full - dead), 0.0f, 1.0f);
}

/* The very back of the rig: the last trailer's tail, or the tractor's
 * own rear if it is running bobtail. */
static void tail_point(const Rig *r, const RigSpec *spec,
                       float *tx, float *tz)
{
    if (spec->n_trailers <= 0) {
        *tx = r->x - cosf(r->heading) * 2.0f;
        *tz = r->z - sinf(r->heading) * 2.0f;
        return;
    }
    {
        int last = spec->n_trailers - 1;
        float px, pz;
        rig_trailer_pos(r, spec, last, &px, &pz);
        *tx = px - cosf(r->trailer_heading[last]) * 1.5f;
        *tz = pz - sinf(r->trailer_heading[last]) * 1.5f;
    }
}

/*
 * The pose the camera wants this frame: eye and look-at, both derived
 * from one swung axis so the two can never disagree about which way the
 * rig is travelling.
 */
static void desired_pose(const CameraState *c, const HaulSettings *s,
                         const Rig *r, const RigSpec *spec,
                         float *ex, float *ey, float *ez,
                         float *lx, float *ly, float *lz)
{
    float axis = r->heading + c->orbit;
    float ax = cosf(axis), az = sinf(axis);
    float rig_len = rig_spec_total_length(spec);
    float d = s->cam_distance_base_m + s->cam_distance_per_rig_length * rig_len;
    float h = s->cam_height_m;
    float rev, tail_bias, nose_x, nose_z, tx, tz;
    float lean, lax, laz;

    *ex = r->x - ax * d;
    *ez = r->z - az * d;
    *ey = r->y + h;

    /*
     * The aim point starts on the tractor's nose, same as a normal
     * chase view, and slides toward the last trailer's tail as the
     * reverse swing completes — by the time the camera is fully round
     * at the nose looking back, it is also looking straight at what
     * actually matters while backing: the trailer and whatever it is
     * being aimed at.
     */
    rev = reverse_fraction(s, r->speed);
    tail_bias = rev * s->cam_reverse_tail_bias;
    nose_x = r->x + cosf(r->heading) * 4.0f;
    nose_z = r->z + sinf(r->heading) * 4.0f;
    tail_point(r, spec, &tx, &tz);

    *lx = nose_x + (tx - nose_x) * tail_bias;
    *lz = nose_z + (tz - nose_z) * tail_bias;
    *ly = r->y + s->cam_look_height_m;

    /*
     * Corner lean: nudge the aim point toward whichever way the front
     * wheels are currently pointed, so the camera previews which way
     * the rig is about to swing instead of only ever looking straight
     * along the tractor's nose. There is no track curvature to read
     * here the way WiiKart's camera reads one — the rig's own steering
     * command stands in for it, which is arguably more honest anyway:
     * it previews what the DRIVER just committed to, not the terrain.
     */
    lax = -az; laz = ax;
    lean = r->steer_cmd * s->cam_corner_lean;
    if (c->orbit > CAM_PI * 0.5f)
        lean = -lean;              /* looking back down the rig, right
                                    * and left swap from this side */
    lean = haul_clampf(lean, -5.0f, 5.0f);
    *lx += lax * lean;
    *lz += laz * lean;
}

void camera_reset(CameraState *c, const HaulSettings *s, const Rig *r,
                  const RigSpec *spec, GroundFn ground_fn, void *ctx)
{
    float ex, ey, ez, lx, ly, lz, ground;

    c->orbit = 0.0f;
    c->pitch = 0.0f;
    desired_pose(c, s, r, spec, &ex, &ey, &ez, &lx, &ly, &lz);

    ground = ground_fn ? ground_fn(ex, ez, ctx) : 0.0f;
    if (ey < ground + s->cam_min_height_m)
        ey = ground + s->cam_min_height_m;

    c->x = ex; c->y = ey; c->z = ez;
    c->look_x = lx; c->look_y = ly; c->look_z = lz;
    c->primed = 1;
}

void camera_update(CameraState *c, const HaulSettings *s, const Rig *r,
                   const RigSpec *spec, GroundFn ground_fn, void *ctx,
                   float dt)
{
    float rev, orbit_target, step, rate, blend_f;
    float ex, ey, ez, lx, ly, lz, ground;

    if (!(dt > 0.0f))            /* also catches NaN */
        return;
    if (dt > 0.1f)
        dt = 0.1f;
    if (!c->primed) {
        camera_reset(c, s, r, spec, ground_fn, ctx);
        return;
    }

    /* a rig that has been reset to a scenario's start pose has not
     * driven anywhere — easing after it would streak the camera across
     * the yard, so anything further than a few rig-lengths away is
     * treated as a teleport */
    {
        float dx = r->x - c->x, dz = r->z - c->z, dy = r->y - c->y;
        float rig_len = rig_spec_total_length(spec);
        float far = (s->cam_distance_base_m +
                    s->cam_distance_per_rig_length * rig_len) * 3.0f + 20.0f;
        if (dx * dx + dz * dz + dy * dy > far * far) {
            camera_reset(c, s, r, spec, ground_fn, ctx);
            return;
        }
    }

    /* --- the swing --- */
    rev = reverse_fraction(s, r->speed);
    orbit_target = CAM_PI * rev;
    step = (orbit_target - c->orbit) * cam_blend(s->cam_reverse_smoothing, dt);
    rate = s->cam_reverse_orbit_rate_dps * (CAM_PI / 180.0f) * dt;
    if (step >  rate) step =  rate;
    if (step < -rate) step = -rate;
    c->orbit += step;
    c->orbit = haul_clampf(c->orbit, 0.0f, CAM_PI);

    /* --- the easing --- */
    desired_pose(c, s, r, spec, &ex, &ey, &ez, &lx, &ly, &lz);

    blend_f = cam_blend(s->cam_follow_smoothing, dt);
    c->x += (ex - c->x) * blend_f;
    c->y += (ey - c->y) * blend_f;
    c->z += (ez - c->z) * blend_f;

    blend_f = cam_blend(s->cam_look_smoothing, dt);
    c->look_x += (lx - c->look_x) * blend_f;
    c->look_y += (ly - c->look_y) * blend_f;
    c->look_z += (lz - c->look_z) * blend_f;

    ground = ground_fn ? ground_fn(c->x, c->z, ctx) : 0.0f;
    if (c->y < ground + s->cam_min_height_m)
        c->y = ground + s->cam_min_height_m;
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

void mirror_camera(const Rig *r, const RigSpec *spec, float side,
                   float *ex, float *ey, float *ez,
                   float *lx, float *ly, float *lz)
{
    const TruckSpec *ts = &truck_specs[spec->truck_idx];
    float fx = cosf(r->heading), fz = sinf(r->heading);
    float latx = -fz, latz = fx;
    float mirror_out = ts->body_width * 0.5f + 0.3f;
    float mx = r->x + fx * (ts->body_length * 0.3f) + latx * mirror_out * side;
    float mz = r->z + fz * (ts->body_length * 0.3f) + latz * mirror_out * side;
    float look_heading = r->heading + CAM_PI + side * 0.35f;

    *ex = mx; *ey = r->y + 2.0f; *ez = mz;
    *lx = mx + cosf(look_heading) * 8.0f;
    *lz = mz + sinf(look_heading) * 8.0f;
    *ly = r->y + 1.0f;
}
