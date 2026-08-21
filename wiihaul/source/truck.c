/* WiiHaul tractor-trailer physics. Plain C99, no platform dependency. */
#include <math.h>
#include <string.h>
#include "truck.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEG2RAD (float)(M_PI / 180.0)
#define AIR_DENSITY 1.2f
#define LAUNCH_FLOOR_MPS 2.5f   /* engine force is power/max(|v|,this) —
                                * a torque-limited, not power-limited,
                                * launch, the way a diesel actually pulls
                                * away from a stop                        */
#define JACKKNIFE_RECOVER_SPEED_MPS 2.5f /* max speed while jackknifed    */

float haul_angle_wrap(float a)
{
    while (a > (float)M_PI)  a -= 2.0f * (float)M_PI;
    while (a < -(float)M_PI) a += 2.0f * (float)M_PI;
    return a;
}

float haul_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

const char *trailer_type_name(int type)
{
    switch (type) {
    case TRAILER_BOX:     return "DRY VAN";
    case TRAILER_FLATBED: return "FLATBED";
    case TRAILER_TANKER:  return "TANKER";
    case TRAILER_LOWBOY:  return "LOWBOY";
    case TRAILER_PUP:     return "PUP";
    default:              return "?";
    }
}

/* ------------------------------------------------------------------ */
/* Default rosters                                                    */
/* ------------------------------------------------------------------ */

TruckSpec   truck_specs[MAX_TRUCK_SPECS];
int         truck_spec_count;
TrailerSpec trailer_specs[MAX_TRAILER_SPECS];
int         trailer_spec_count;
RigSpec     rig_specs[MAX_RIG_SPECS];
int         rig_spec_count;

/* Geometric gear ladder: gear g's redline speed is base * ratio^g, so
 * every gear covers the same proportional stretch of road speed — a
 * heavy first gear for pulling away, a tall top gear for the highway. */
static void set_gear_ladder(TruckSpec *t, int n, float base_mps,
                            float top_mps)
{
    int g;
    float ratio = powf(top_mps / base_mps, 1.0f / (float)(n - 1));
    t->n_gears = n;
    for (g = 0; g < n; g++)
        t->gear_top[g] = base_mps * powf(ratio, (float)g);
}

void truck_specs_reset_defaults(void)
{
    TruckSpec *t;
    TrailerSpec *tr;
    RigSpec *r;

    memset(truck_specs, 0, sizeof(truck_specs));
    truck_spec_count = 0;

    t = &truck_specs[truck_spec_count++];
    strcpy(t->name, "SHORTHAUL");
    t->mass_kg = 7400.0f; t->power_hp = 330.0f; t->wheelbase = 3.2f;
    t->body_length = 4.6f; t->body_width = 2.4f;
    t->hitch_setback = 0.7f; t->max_steer_deg = 50.0f;
    t->reverse_top_mps = 4.2f;
    t->brake_decel_ref = 4.8f; t->ref_mass_kg = 12000.0f; t->cd_a = 5.6f;
    set_gear_ladder(t, 6, 2.0f, 18.0f);
    t->nominal_rpm = 0.55f;

    t = &truck_specs[truck_spec_count++];
    strcpy(t->name, "DAYCAB");
    t->mass_kg = 8200.0f; t->power_hp = 400.0f; t->wheelbase = 4.0f;
    t->body_length = 6.4f; t->body_width = 2.5f;
    t->hitch_setback = 0.9f; t->max_steer_deg = 36.0f;
    t->reverse_top_mps = 3.4f;
    t->brake_decel_ref = 4.4f; t->ref_mass_kg = 15000.0f; t->cd_a = 6.2f;
    set_gear_ladder(t, 10, 2.2f, 28.85f);
    t->nominal_rpm = 0.50f;

    t = &truck_specs[truck_spec_count++];
    strcpy(t->name, "SLEEPER");
    t->mass_kg = 8800.0f; t->power_hp = 455.0f; t->wheelbase = 4.3f;
    t->body_length = 7.4f; t->body_width = 2.5f;
    t->hitch_setback = 1.0f; t->max_steer_deg = 32.0f;
    t->reverse_top_mps = 3.0f;
    t->brake_decel_ref = 4.2f; t->ref_mass_kg = 18000.0f; t->cd_a = 6.6f;
    set_gear_ladder(t, 10, 2.3f, 30.0f);
    t->nominal_rpm = 0.48f;

    memset(trailer_specs, 0, sizeof(trailer_specs));
    trailer_spec_count = 0;

    tr = &trailer_specs[trailer_spec_count++];
    strcpy(tr->name, "DRY VAN 53");
    tr->type = TRAILER_BOX; tr->length = 12.4f; tr->hitch_offset = 0.0f;
    tr->width = 2.6f; tr->height = 4.0f;
    tr->empty_mass_kg = 6800.0f; tr->max_cargo_kg = 20000.0f;
    tr->jackknife_limit_deg = 90.0f; tr->slosh_strength = 0.0f;

    tr = &trailer_specs[trailer_spec_count++];
    strcpy(tr->name, "FLATBED 48");
    tr->type = TRAILER_FLATBED; tr->length = 11.2f; tr->hitch_offset = 0.0f;
    tr->width = 2.6f; tr->height = 1.6f;
    tr->empty_mass_kg = 5400.0f; tr->max_cargo_kg = 22000.0f;
    tr->jackknife_limit_deg = 90.0f; tr->slosh_strength = 0.0f;

    tr = &trailer_specs[trailer_spec_count++];
    strcpy(tr->name, "TANKER 42");
    tr->type = TRAILER_TANKER; tr->length = 10.0f; tr->hitch_offset = 0.0f;
    tr->width = 2.6f; tr->height = 3.4f;
    tr->empty_mass_kg = 7200.0f; tr->max_cargo_kg = 18000.0f;
    tr->jackknife_limit_deg = 85.0f; tr->slosh_strength = 1.0f;

    tr = &trailer_specs[trailer_spec_count++];
    strcpy(tr->name, "LOWBOY 48");
    tr->type = TRAILER_LOWBOY; tr->length = 11.6f; tr->hitch_offset = 0.0f;
    tr->width = 2.9f; tr->height = 1.2f;
    tr->empty_mass_kg = 8600.0f; tr->max_cargo_kg = 32000.0f;
    tr->jackknife_limit_deg = 80.0f; tr->slosh_strength = 0.0f;

    tr = &trailer_specs[trailer_spec_count++];
    strcpy(tr->name, "PUP 28");
    tr->type = TRAILER_PUP; tr->length = 6.2f; tr->hitch_offset = 1.6f;
    tr->width = 2.6f; tr->height = 3.6f;
    tr->empty_mass_kg = 3200.0f; tr->max_cargo_kg = 9000.0f;
    tr->jackknife_limit_deg = 90.0f; tr->slosh_strength = 0.0f;

    memset(rig_specs, 0, sizeof(rig_specs));
    rig_spec_count = 0;

    r = &rig_specs[rig_spec_count++];
    strcpy(r->name, "YARD SPOTTER");
    r->truck_idx = 0; r->n_trailers = 1; r->trailer_idx[0] = 0;
    r->cargo_mass_kg = 0.0f; r->difficulty_stars = 1;

    r = &rig_specs[rig_spec_count++];
    strcpy(r->name, "DRY VAN HAUL");
    r->truck_idx = 1; r->n_trailers = 1; r->trailer_idx[0] = 0;
    r->cargo_mass_kg = 12000.0f; r->difficulty_stars = 2;

    r = &rig_specs[rig_spec_count++];
    strcpy(r->name, "FLATBED LOAD");
    r->truck_idx = 1; r->n_trailers = 1; r->trailer_idx[0] = 1;
    r->cargo_mass_kg = 15000.0f; r->difficulty_stars = 2;

    r = &rig_specs[rig_spec_count++];
    strcpy(r->name, "TANKER RUN");
    r->truck_idx = 2; r->n_trailers = 1; r->trailer_idx[0] = 2;
    r->cargo_mass_kg = 14000.0f; r->difficulty_stars = 3;

    r = &rig_specs[rig_spec_count++];
    strcpy(r->name, "HEAVY HAUL");
    r->truck_idx = 2; r->n_trailers = 1; r->trailer_idx[0] = 3;
    r->cargo_mass_kg = 28000.0f; r->difficulty_stars = 4;

    r = &rig_specs[rig_spec_count++];
    strcpy(r->name, "DOUBLES");
    r->truck_idx = 2; r->n_trailers = 2;
    r->trailer_idx[0] = 4; r->trailer_idx[1] = 4;
    r->cargo_mass_kg = 12000.0f; r->difficulty_stars = 4;
}

float rig_spec_total_mass(const RigSpec *rs)
{
    float mass = truck_specs[rs->truck_idx].mass_kg;
    float total_cap = 0.0f;
    int i;

    for (i = 0; i < rs->n_trailers; i++)
        total_cap += trailer_specs[rs->trailer_idx[i]].max_cargo_kg;

    for (i = 0; i < rs->n_trailers; i++) {
        const TrailerSpec *ts = &trailer_specs[rs->trailer_idx[i]];
        float share = (total_cap > 1.0f) ? ts->max_cargo_kg / total_cap
                                         : 1.0f / (float)rs->n_trailers;
        mass += ts->empty_mass_kg + rs->cargo_mass_kg * share;
    }
    return mass;
}

float rig_spec_total_length(const RigSpec *rs)
{
    const TruckSpec *ts = &truck_specs[rs->truck_idx];
    float len = ts->wheelbase + ts->hitch_setback;
    int i;

    for (i = 0; i < rs->n_trailers; i++) {
        const TrailerSpec *tr = &trailer_specs[rs->trailer_idx[i]];
        len += (i > 0 ? tr->hitch_offset : 0.0f) + tr->length;
    }
    return len;
}

float rig_spec_top_speed_mps(const RigSpec *rs)
{
    const TruckSpec *ts = &truck_specs[rs->truck_idx];
    return ts->gear_top[ts->n_gears - 1];
}

void haul_settings_defaults(HaulSettings *s)
{
    memset(s, 0, sizeof(*s));
    s->steer_rate_on_dps = 130.0f;
    s->steer_rate_center_dps = 190.0f;
    s->steer_speed_fade = 0.045f;

    s->rolling_resistance = 0.012f;
    s->drivetrain_efficiency = 0.86f;
    s->shift_seconds = 0.55f;      /* trucks take real time to shift      */

    s->brake_mass_floor = 0.45f;

    s->jackknife_warn_frac = 0.55f;
    s->jackknife_danger_frac = 0.82f;

    s->slosh_response = 2.2f;
    s->slosh_yaw_feedback = 0.9f;

    s->cam_distance_base_m = 9.0f;
    s->cam_distance_per_rig_length = 0.42f;
    s->cam_height_m = 3.4f;
    s->cam_min_height_m = 0.6f;
    s->cam_look_height_m = 1.4f;
    s->cam_follow_smoothing = 0.010f;
    s->cam_look_smoothing = 0.05f;
    s->cam_reverse_deadzone_mps = 0.15f;
    s->cam_reverse_full_mps = 3.0f;
    s->cam_reverse_orbit_rate_dps = 70.0f;
    s->cam_reverse_smoothing = 0.03f;
    s->cam_reverse_tail_bias = 0.65f;
    s->cam_corner_lean = 2.4f;
}

/* ------------------------------------------------------------------ */
/* Geometry                                                            */
/* ------------------------------------------------------------------ */

void rig_hitch_pos(const Rig *r, const RigSpec *spec, int i,
                   float *x, float *z)
{
    if (i < 0) {
        const TruckSpec *ts = &truck_specs[spec->truck_idx];
        *x = r->x - cosf(r->heading) * ts->hitch_setback;
        *z = r->z - sinf(r->heading) * ts->hitch_setback;
        return;
    }
    {
        float px, pz;
        const TrailerSpec *tr = &trailer_specs[spec->trailer_idx[i]];
        rig_trailer_pos(r, spec, i, &px, &pz);
        *x = px - cosf(r->trailer_heading[i]) * tr->hitch_offset;
        *z = pz - sinf(r->trailer_heading[i]) * tr->hitch_offset;
    }
}

void rig_trailer_pos(const Rig *r, const RigSpec *spec, int i,
                     float *x, float *z)
{
    float hx, hz;
    const TrailerSpec *tr = &trailer_specs[spec->trailer_idx[i]];
    rig_hitch_pos(r, spec, i - 1, &hx, &hz);
    *x = hx - cosf(r->trailer_heading[i]) * tr->length;
    *z = hz - sinf(r->trailer_heading[i]) * tr->length;
}

float rig_articulation(const Rig *r, int joint)
{
    float prev = (joint == 0) ? r->heading : r->trailer_heading[joint - 1];
    return haul_angle_wrap(prev - r->trailer_heading[joint]);
}

void rig_reset(Rig *r, const RigSpec *spec, float x, float z, float y,
              float heading)
{
    int i;
    memset(r, 0, sizeof(*r));
    r->x = x; r->z = z; r->y = y; r->heading = heading;
    r->n_trailers = spec->n_trailers;
    for (i = 0; i < spec->n_trailers; i++)
        r->trailer_heading[i] = heading;
    r->gearbox = GEARBOX_AUTO;
}

float truck_power_scale(float rev_frac, float nominal_frac)
{
    const float FLOOR = 0.30f;    /* diesels stay meaty off their peak    */
    float d, width, x, peak;

    if (rev_frac >= 1.0f)
        return 0.0f;
    if (nominal_frac < 0.05f) nominal_frac = 0.05f;
    if (nominal_frac > 0.95f) nominal_frac = 0.95f;
    d = rev_frac - nominal_frac;
    width = (d >= 0.0f) ? (1.0f - nominal_frac) : nominal_frac;
    x = d / width;
    peak = 1.0f - x * x;
    if (peak < 0.0f) peak = 0.0f;
    return FLOOR + (1.0f - FLOOR) * peak;
}

/* ------------------------------------------------------------------ */
/* Simulation step                                                     */
/* ------------------------------------------------------------------ */

static float blend(float rate_per_s, float dt)
{
    /* fraction of the remaining gap closed this frame, frame-rate
     * independent: 1 - (1 - rate)^dt would need rate in (0,1); rate here
     * is a plain per-second closing speed for a linear ramp instead */
    float f = rate_per_s * dt;
    return (f > 1.0f) ? 1.0f : f;
}

void rig_step(Rig *r, const RigSpec *spec, const Input *in,
             const HaulSettings *set, float dt,
             GroundFn ground_fn, void *ground_ctx)
{
    const TruckSpec *ts = &truck_specs[spec->truck_idx];
    float total_mass = rig_spec_total_mass(spec);
    float max_steer = ts->max_steer_deg * DEG2RAD;
    float steer_target, winding_on, rate_dps, speed_fade, max_step;
    float accel = 0.0f;
    float omega0;
    float v_prev, theta_prev, omega_prev;
    float slope = 0.0f;
    int i, dir;

    if (dt <= 0.0f || !(dt == dt))   /* also catches NaN */
        return;
    if (dt > 0.1f) dt = 0.1f;

    /* --- grade: gravity's component along whichever way the tractor is
     * currently pointed, from a finite difference of the ground under
     * it. Positive slope = uphill in the heading direction, and it
     * simply subtracts from accel like any other force — a rig facing
     * uphill with nothing holding it rolls backward exactly the way a
     * real one does without the parking brake set. */
    if (ground_fn) {
        const float EPS = 0.4f;
        float h0 = ground_fn(r->x, r->z, ground_ctx);
        float h1 = ground_fn(r->x + cosf(r->heading) * EPS,
                             r->z + sinf(r->heading) * EPS, ground_ctx);
        slope = (h1 - h0) / EPS;
    }

    /* --- steering: ramp steer_cmd (-1..1, a fraction of full lock)
     * toward the target at a rate expressed in wheel-degrees/second,
     * converted through this truck's own lock so a twitchy-locked yard
     * tractor and a wide-locked sleeper cab both feel like real
     * steering racks rather than an identical abstract knob --- */
    steer_target = haul_clampf(in->steer, -1.0f, 1.0f);
    winding_on = (steer_target * r->steer_cmd >= 0.0f) &&
                (fabsf(steer_target) > fabsf(r->steer_cmd));
    rate_dps = winding_on ? set->steer_rate_on_dps
                          : set->steer_rate_center_dps;
    speed_fade = 1.0f / (1.0f + fabsf(r->speed) * set->steer_speed_fade);
    max_step = (rate_dps / fmaxf(ts->max_steer_deg, 1.0f)) * speed_fade * dt;
    if (max_step < 0.002f) max_step = 0.002f;
    if (r->steer_cmd < steer_target)
        r->steer_cmd = fminf(steer_target, r->steer_cmd + max_step);
    else if (r->steer_cmd > steer_target)
        r->steer_cmd = fmaxf(steer_target, r->steer_cmd - max_step);
    r->steer_angle = r->steer_cmd * max_steer;

    /* --- engine / gearbox: forward gear ladder, engaged by the accel
     * pedal regardless of which way the rig is currently rolling —
     * flooring accel while still rolling backward just decelerates the
     * reverse motion first, same as a real torque converter fighting a
     * hill roll-back, no special-casing needed --- */
    {
        float in_speed = fabsf(r->speed);
        float nominal_frac = ts->nominal_rpm;
        int want_up = 0, want_down = 0;
        int forward_drive = in->accel && !in->brake;

        if (r->gear >= ts->n_gears) r->gear = ts->n_gears - 1;
        r->rev_frac = in_speed / ts->gear_top[r->gear];

        if (r->gearbox == GEARBOX_AUTO && r->shift_t <= 0.0f) {
            float next_frac = (r->gear + 1 < ts->n_gears)
                ? in_speed / ts->gear_top[r->gear + 1] : 0.0f;
            float low_frac = (r->gear > 0)
                ? in_speed / ts->gear_top[r->gear - 1] : 9.0f;
            float up_thresh = nominal_frac + (1.0f - nominal_frac) * 0.72f;
            float down_thresh = nominal_frac * 0.42f;
            want_up = r->rev_frac > up_thresh && r->gear < ts->n_gears - 1 &&
                     truck_power_scale(next_frac, nominal_frac) > 0.30f;
            want_down = r->rev_frac < down_thresh && r->gear > 0 &&
                       low_frac < 0.94f;
        } else if (r->gearbox == GEARBOX_MANUAL && r->shift_t <= 0.0f) {
            want_up = in->gear_up && !r->prev_up_btn && r->gear < ts->n_gears - 1;
            want_down = in->gear_down && !r->prev_down_btn && r->gear > 0;
        }
        r->prev_up_btn = in->gear_up;
        r->prev_down_btn = in->gear_down;

        if (want_up) { r->gear++; r->shift_t = set->shift_seconds; }
        else if (want_down) { r->gear--; r->shift_t = set->shift_seconds; }
        if (r->shift_t > 0.0f) r->shift_t -= dt;
        r->rev_frac = in_speed / ts->gear_top[r->gear];

        if (forward_drive && r->shift_t <= 0.0f) {
            float denom = fmaxf(in_speed, LAUNCH_FLOOR_MPS);
            float watts = ts->power_hp * 745.7f *
                         truck_power_scale(r->rev_frac, nominal_frac) *
                         set->drivetrain_efficiency;
            accel += (watts / denom) / total_mass;
        }
    }

    /* --- reverse: its own throttle, one low, torquey ratio, no ladder
     * — this is the pedal a backing maneuver is actually driven with,
     * held for as long as the maneuver takes, exactly like accel is for
     * forward driving. Blocked while jackknifed: reverse is usually
     * what caused it, and recovery means easing forward instead. --- */
    if (in->reverse && !in->brake && !r->jackknifed) {
        float denom = fmaxf(-r->speed, LAUNCH_FLOOR_MPS);
        float watts = ts->power_hp * 0.5f * 745.7f *
                     set->drivetrain_efficiency;
        accel -= (watts / denom) / total_mass;
    }

    /* --- brake: purely a decelerator, never by itself a source of
     * motion — it slows whichever way the rig is currently rolling,
     * forward or reverse, and parking_brake does the same to hold it on
     * a grade once stopped (see the grade handling below). --- */
    if (in->brake || in->parking_brake) {
        float mass_mult = haul_clampf(ts->ref_mass_kg / total_mass,
                                      set->brake_mass_floor, 2.0f);
        float decel = ts->brake_decel_ref * mass_mult;
        if (r->speed > 0.02f)
            accel -= fminf(decel, r->speed / dt);
        else if (r->speed < -0.02f)
            accel += fminf(decel, -r->speed / dt);
    }

    /* --- rolling resistance + aero drag, always opposing motion --- */
    if (fabsf(r->speed) > 0.001f) {
        float roll = set->rolling_resistance * GRAVITY;
        float drag = 0.5f * AIR_DENSITY * ts->cd_a * r->speed * r->speed /
                    total_mass;
        float opp = roll + drag;
        if (r->speed > 0.0f) accel -= opp; else accel += opp;
    }

    accel -= GRAVITY * slope;

    /* --- integrate tractor --- */
    r->speed += accel * dt;
    r->speed = haul_clampf(r->speed, -ts->reverse_top_mps,
                           ts->gear_top[ts->n_gears - 1]);
    if (r->jackknifed)
        r->speed = haul_clampf(r->speed, -0.3f, JACKKNIFE_RECOVER_SPEED_MPS);
    /* A stopped rig with no drive/brake input just stays stopped, UNLESS
     * it is resting on enough of a grade that only an actual brake would
     * hold it — then it rolls, same as a real truck with the parking
     * brake off. stalled_on_grade is a one-frame flag for the HUD/audio
     * layer ("you're rolling — set the brake"). */
    r->stalled_on_grade = 0;
    if (fabsf(r->speed) < 0.02f && !in->accel && !in->reverse && !in->brake) {
        if (in->parking_brake || fabsf(slope) < 0.02f)
            r->speed = 0.0f;
        else
            r->stalled_on_grade = 1;
    }

    omega0 = (fabsf(ts->wheelbase) > 0.01f)
                ? (r->speed / ts->wheelbase) * tanf(r->steer_angle) : 0.0f;
    r->heading = haul_angle_wrap(r->heading + omega0 * dt);
    r->x += r->speed * cosf(r->heading) * dt;
    r->z += r->speed * sinf(r->heading) * dt;
    r->y = ground_fn ? ground_fn(r->x, r->z, ground_ctx) : 0.0f;

    /* --- trailer chain --- */
    v_prev = r->speed; theta_prev = r->heading; omega_prev = omega0;
    for (i = 0; i < r->n_trailers; i++) {
        const TrailerSpec *tr = &trailer_specs[spec->trailer_idx[i]];
        float L = fmaxf(tr->length, 0.5f);
        float C = (i == 0) ? ts->hitch_setback : tr->hitch_offset;
        float diff = haul_angle_wrap(theta_prev - r->trailer_heading[i]);
        float limit = tr->jackknife_limit_deg * DEG2RAD;
        float omega_i, new_theta, artic;

        omega_i = (v_prev / L) * sinf(diff) -
                 (C / L) * cosf(diff) * omega_prev;
        new_theta = haul_angle_wrap(r->trailer_heading[i] + omega_i * dt);

        artic = haul_angle_wrap(theta_prev - new_theta);
        if (artic > limit) {
            new_theta = haul_angle_wrap(theta_prev - limit);
            r->jackknifed = 1; r->jackknife_joint = i;
        } else if (artic < -limit) {
            new_theta = haul_angle_wrap(theta_prev + limit);
            r->jackknifed = 1; r->jackknife_joint = i;
        }

        r->trailer_heading[i] = new_theta;
        r->trailer_omega[i] = omega_i;

        v_prev = v_prev * cosf(diff);
        theta_prev = new_theta;
        omega_prev = omega_i;
    }

    /* jackknife recovery: once every joint is back under the warning
     * band, hand control back fully */
    if (r->jackknifed) {
        int clear = 1;
        for (i = 0; i < r->n_trailers; i++) {
            const TrailerSpec *tr = &trailer_specs[spec->trailer_idx[i]];
            float limit = tr->jackknife_limit_deg * DEG2RAD;
            if (fabsf(rig_articulation(r, i)) >
               limit * set->jackknife_warn_frac)
                clear = 0;
        }
        if (clear) { r->jackknifed = 0; r->jackknife_joint = -1; }
    } else {
        r->jackknife_joint = -1;
    }

    /* --- tanker slosh: cosmetic/HUD extra wag on a liquid load,
     * driven by longitudinal accel (braking/launching sloshes the
     * load forward-aft, which shows up as a lateral wag once any
     * articulation is already present) --- */
    for (i = 0; i < r->n_trailers; i++) {
        const TrailerSpec *tr = &trailer_specs[spec->trailer_idx[i]];
        if (tr->slosh_strength <= 0.0f) continue;
        {
            float target = haul_clampf(-accel * 0.30f, -1.0f, 1.0f);
            r->slosh += (target - r->slosh) * blend(set->slosh_response, dt);
            r->trailer_heading[i] = haul_angle_wrap(r->trailer_heading[i] +
                r->slosh * set->slosh_yaw_feedback * tr->slosh_strength * dt);
        }
    }

    /* --- telemetry --- */
    dir = (r->speed > 0.05f) ? 1 : (r->speed < -0.05f) ? -1 : 0;
    if (dir != 0) {
        if (r->prev_dir != 0 && dir != r->prev_dir) r->pullups++;
        r->prev_dir = dir;
    }
    r->odometer_m += fabsf(r->speed) * dt;
    r->sim_t += dt;
}
