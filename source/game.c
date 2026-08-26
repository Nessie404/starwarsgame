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
 *  - braking from the same traction budget as everything else, not a
 *    free-standing stopping-distance dial
 *  - cornering from a real dynamic bicycle model (v1.28.0): front and
 *    rear each get their own slip angle and read a genuine tire curve
 *    off it — rises to a peak, falls off past it toward a sliding
 *    floor that never quite reaches zero — instead of one whole-car
 *    yaw rate clamped against a single grip number. Kart.vy and
 *    Kart.yaw_rate are real integrated states now, not values read
 *    straight off the steering input each frame.
 *  - gravity acting along the road grade, so climbs cost speed and
 *    descents give it back, and weight transfer front-to-back under
 *    acceleration and braking, the same "squat and dive" a real chassis
 *    has
 *
 * The handbrake locks the rear axle to its sliding friction level and
 * switches off stability control — the one deliberate way to put the
 * rear axle past its own peak on purpose and hand the driver the real,
 * open-loop-unstable dynamics a genuine drift has there. There is
 * deliberately no hidden slide boost or rubber-banding: sliding costs
 * real speed on tarmac, and is only actually quick on loose surfaces,
 * where a yawed tire builds a wedge of displaced material that adds
 * force a dry tire's friction alone would not give.
 */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "game.h"

#define PI_F 3.14159265358979f

#define RHO_AIR       1.225f
#define DEFAULT_CRR   0.015f   /* rolling resistance coefficient       */
#define DEFAULT_DRIVE 0.85f    /* drivetrain efficiency                */
#define HP_TO_W    745.7f
#define V100       27.78f      /* 100 km/h in m/s                      */

/*
 * The compiled-in roster mirrors config/cars.json exactly, so opening the
 * DOL with no filesystem at all (no SD card, no cars.json to read) still
 * shows the full eleven-car garage rather than only the first four — see
 * HANDOFF.md for the bug this used to be. If you add a car to cars.json,
 * add it here too, in the same order, or it silently vanishes for anyone
 * without a virtual SD card set up.
 */
static const KartSpec default_kart_specs[DEFAULT_SPEC_COUNT] = {
    /* name      mass    hp   brake  lat_g  CdA   wheelbase offroad  drivetrain          bias
     *   gears, and the road speed (m/s) at the limiter in each                              */
    { "RACER",   260.f,  48.f, 30.f, 1.62f, 0.45f, 1.05f,   0.30f, DRIVETRAIN_RWD, 0.5f,
      4, { 14.000f, 24.000f, 35.000f, 47.000f } },
    { "SPORT",   950.f, 150.f, 37.f, 1.19f, 0.66f, 2.45f,   0.45f, DRIVETRAIN_RWD, 0.5f,
      5, { 13.000f, 22.000f, 33.000f, 45.000f, 60.000f } },
    { "RALLY",  1180.f, 220.f, 40.f, 1.10f, 0.70f, 2.60f,   0.72f, DRIVETRAIN_AWD, 0.40f,
      6, { 12.000f, 20.000f, 29.000f, 40.000f, 53.000f, 67.000f },
      6200.0f, ASPIRATION_TURBO },
    { "TOURER", 1350.f, 310.f, 34.f, 1.28f, 0.60f, 2.70f,   0.35f, DRIVETRAIN_AWD, 0.50f,
      6, { 15.000f, 25.000f, 37.000f, 50.000f, 64.000f, 79.000f } },
    { "RUBY",   1080.f, 310.f, 33.f, 1.25f, 0.58f, 2.55f,   0.40f, DRIVETRAIN_FWD, 0.5f,
      6, { 10.556f, 17.583f, 26.028f, 35.167f, 45.000f, 55.556f } },
    { "BUGGY",   620.f,  95.f, 32.f, 1.44f, 0.55f, 2.20f,   0.85f, DRIVETRAIN_AWD, 0.45f,
      4, { 11.111f, 19.444f, 29.167f, 40.278f },
      6500.0f, ASPIRATION_TURBO },
    { "WAGON",  1550.f, 190.f, 42.f, 1.06f, 0.72f, 2.75f,   0.40f, DRIVETRAIN_FWD, 0.5f,
      5, { 12.500f, 21.667f, 31.944f, 43.056f, 54.167f } },
    { "FORMULA", 720.f, 260.f, 28.f, 1.81f, 0.55f, 2.90f,   0.10f, DRIVETRAIN_RWD, 0.5f,
      6, { 15.278f, 26.389f, 38.889f, 51.389f, 63.889f, 75.000f },
      7200.0f, ASPIRATION_NA },
    { "TRUCK",  2100.f, 280.f, 48.f, 0.88f, 0.85f, 3.10f,   0.78f, DRIVETRAIN_AWD, 0.35f,
      5, { 11.667f, 19.444f, 27.778f, 36.111f, 45.833f },
      3400.0f, ASPIRATION_TURBO },
    { "HERITAGE", 890.f, 85.f, 45.f, 0.94f, 0.58f, 2.35f,   0.35f, DRIVETRAIN_RWD, 0.5f,
      4, { 10.556f, 18.056f, 26.389f, 34.722f } },
    { "MUSCLE", 1620.f, 420.f, 40.f, 1.00f, 0.68f, 2.85f,   0.30f, DRIVETRAIN_RWD, 0.5f,
      5, { 16.111f, 27.222f, 40.278f, 54.167f, 69.444f },
      5200.0f, ASPIRATION_SUPERCHARGED },
    { "STOCKER", 1560.f, 540.f, 36.f, 1.38f, 0.56f, 2.80f, 0.15f, DRIVETRAIN_RWD, 0.5f,
      5, { 19.444f, 33.333f, 48.611f, 65.278f, 83.333f },
      6000.0f, ASPIRATION_NA },
    { "SLIPSTREAM", 1500.f, 550.f, 38.f, 1.28f, 0.50f, 2.85f, 0.15f, DRIVETRAIN_RWD, 0.5f,
      5, { 20.000f, 33.889f, 50.000f, 67.222f, 86.111f },
      6000.0f, ASPIRATION_NA },
};

/*
 * Given a valid starting roster independent of kart_specs_reset_defaults()
 * ever having been called — main.c's boot path always calls it before the
 * garage is shown, but a belt-and-suspenders non-zero starting state is
 * cheap insurance against some future caller that reads kart_specs first.
 */
KartSpec kart_specs[MAX_KART_SPECS] = {
    { "RACER",   260.f,  48.f, 30.f, 1.62f, 0.45f, 1.05f,   0.30f, DRIVETRAIN_RWD, 0.5f,
      4, { 14.000f, 24.000f, 35.000f, 47.000f } },
    { "SPORT",   950.f, 150.f, 37.f, 1.19f, 0.66f, 2.45f,   0.45f, DRIVETRAIN_RWD, 0.5f,
      5, { 13.000f, 22.000f, 33.000f, 45.000f, 60.000f } },
    { "RALLY",  1180.f, 220.f, 40.f, 1.10f, 0.70f, 2.60f,   0.72f, DRIVETRAIN_AWD, 0.40f,
      6, { 12.000f, 20.000f, 29.000f, 40.000f, 53.000f, 67.000f },
      6200.0f, ASPIRATION_TURBO },
    { "TOURER", 1350.f, 310.f, 34.f, 1.28f, 0.60f, 2.70f,   0.35f, DRIVETRAIN_AWD, 0.50f,
      6, { 15.000f, 25.000f, 37.000f, 50.000f, 64.000f, 79.000f } },
    { "RUBY",   1080.f, 310.f, 33.f, 1.25f, 0.58f, 2.55f,   0.40f, DRIVETRAIN_FWD, 0.5f,
      6, { 10.556f, 17.583f, 26.028f, 35.167f, 45.000f, 55.556f } },
    { "BUGGY",   620.f,  95.f, 32.f, 1.44f, 0.55f, 2.20f,   0.85f, DRIVETRAIN_AWD, 0.45f,
      4, { 11.111f, 19.444f, 29.167f, 40.278f },
      6500.0f, ASPIRATION_TURBO },
    { "WAGON",  1550.f, 190.f, 42.f, 1.06f, 0.72f, 2.75f,   0.40f, DRIVETRAIN_FWD, 0.5f,
      5, { 12.500f, 21.667f, 31.944f, 43.056f, 54.167f } },
    { "FORMULA", 720.f, 260.f, 28.f, 1.81f, 0.55f, 2.90f,   0.10f, DRIVETRAIN_RWD, 0.5f,
      6, { 15.278f, 26.389f, 38.889f, 51.389f, 63.889f, 75.000f },
      7200.0f, ASPIRATION_NA },
    { "TRUCK",  2100.f, 280.f, 48.f, 0.88f, 0.85f, 3.10f,   0.78f, DRIVETRAIN_AWD, 0.35f,
      5, { 11.667f, 19.444f, 27.778f, 36.111f, 45.833f },
      3400.0f, ASPIRATION_TURBO },
    { "HERITAGE", 890.f, 85.f, 45.f, 0.94f, 0.58f, 2.35f,   0.35f, DRIVETRAIN_RWD, 0.5f,
      4, { 10.556f, 18.056f, 26.389f, 34.722f } },
    { "MUSCLE", 1620.f, 420.f, 40.f, 1.00f, 0.68f, 2.85f,   0.30f, DRIVETRAIN_RWD, 0.5f,
      5, { 16.111f, 27.222f, 40.278f, 54.167f, 69.444f },
      5200.0f, ASPIRATION_SUPERCHARGED },
    { "STOCKER", 1560.f, 540.f, 36.f, 1.38f, 0.56f, 2.80f, 0.15f, DRIVETRAIN_RWD, 0.5f,
      5, { 19.444f, 33.333f, 48.611f, 65.278f, 83.333f },
      6000.0f, ASPIRATION_NA },
    { "SLIPSTREAM", 1500.f, 550.f, 38.f, 1.28f, 0.50f, 2.85f, 0.15f, DRIVETRAIN_RWD, 0.5f,
      5, { 20.000f, 33.889f, 50.000f, 67.222f, 86.111f },
      6000.0f, ASPIRATION_NA },
};

int kart_spec_count = DEFAULT_SPEC_COUNT;

/*
 * A car that says nothing about its own engine character gets a middling
 * naturally-aspirated peak-power RPM. Called for the built-ins and for
 * any car loaded from cars.json before its own numbers are read over
 * the top (aspiration needs no equivalent fill-in: 0 is already the
 * naturally-aspirated default).
 */
void kart_spec_default_gearing(KartSpec *s)
{
    if (!(s->nominal_rpm > 0.0f))
        s->nominal_rpm = DEFAULT_NOMINAL_RPM;
}

void kart_specs_reset_defaults(void)
{
    int i;
    memset(kart_specs, 0, sizeof(kart_specs));
    memcpy(kart_specs, default_kart_specs, sizeof(default_kart_specs));
    for (i = 0; i < MAX_KART_SPECS; i++)
        kart_spec_default_gearing(&kart_specs[i]);
    kart_spec_count = DEFAULT_SPEC_COUNT;
}

static int kart_spec_error(char *error, int error_cap, const char *message)
{
    if (error && error_cap > 0) {
        snprintf(error, (size_t)error_cap, "%s", message);
        error[error_cap - 1] = '\0';
    }
    return 0;
}

/*
 * Whether a KartSpec is sane enough to actually race — the same bounds
 * cars.json is held to (config.c's loader calls this too), plus the
 * one piece that only matters for a hand-built car: a gearbox that
 * won't hunt (an upshift point has to clear its own downshift point by
 * a real margin). Shared by cars.json validation and the in-game car
 * designer (kart_specs_add_custom below) so a player-built car can
 * never be less sound than a JSON one.
 */
int kart_spec_validate(const KartSpec *s, char *error, int error_cap)
{
    int g;
    if (!s->name[0]) return kart_spec_error(error, error_cap, "CAR NEEDS NAME");
    for (g = 0; s->name[g]; g++)
        if (!isalnum((unsigned char)s->name[g]) && s->name[g] != ' ' &&
            s->name[g] != '-' && s->name[g] != '.')
            return kart_spec_error(error, error_cap, "BAD CAR NAME");
    if (s->mass_kg < 100.0f || s->mass_kg > 5000.0f)
        return kart_spec_error(error, error_cap, "BAD CAR MASS");
    if (s->power_hp < 5.0f || s->power_hp > 2500.0f)
        return kart_spec_error(error, error_cap, "BAD CAR POWER");
    if (s->brake_dist_100 < 5.0f || s->brake_dist_100 > 200.0f)
        return kart_spec_error(error, error_cap, "BAD CAR BRAKES");
    if (s->lat_g < 0.20f || s->lat_g > 3.0f)
        return kart_spec_error(error, error_cap, "BAD CAR GRIP");
    if (s->cd_a < 0.10f || s->cd_a > 3.0f)
        return kart_spec_error(error, error_cap, "BAD CAR DRAG");
    if (s->wheelbase < 0.50f || s->wheelbase > 6.0f)
        return kart_spec_error(error, error_cap, "BAD WHEELBASE");
    if (s->offroad_grip < 0.05f || s->offroad_grip > 1.20f)
        return kart_spec_error(error, error_cap, "BAD DIRT GRIP");
    if (s->drivetrain != DRIVETRAIN_FWD && s->drivetrain != DRIVETRAIN_RWD &&
        s->drivetrain != DRIVETRAIN_AWD)
        return kart_spec_error(error, error_cap, "BAD DRIVETRAIN");
    if (s->drivetrain == DRIVETRAIN_AWD &&
        (s->awd_front_bias < 0.0f || s->awd_front_bias > 1.0f))
        return kart_spec_error(error, error_cap, "BAD AWD BIAS");
    if (s->n_gears < 1 || s->n_gears > MAX_GEARS)
        return kart_spec_error(error, error_cap, "BAD GEAR COUNT");
    for (g = 0; g < s->n_gears; g++) {
        if (s->gear_top[g] < 2.0f || s->gear_top[g] > 140.0f ||
            (g > 0 && s->gear_top[g] <= s->gear_top[g - 1]))
            return kart_spec_error(error, error_cap, "BAD GEAR SPEEDS");
    }
    if (s->nominal_rpm < 1500.0f || s->nominal_rpm > 9500.0f)
        return kart_spec_error(error, error_cap, "BAD NOMINAL RPM");
    if (s->aspiration < ASPIRATION_NA || s->aspiration >= ASPIRATION_COUNT)
        return kart_spec_error(error, error_cap, "BAD ASPIRATION");
    return 1;
}

/*
 * Add one car to the live roster — the in-game car designer's save
 * step. Validates, rejects a name collision with anything already in
 * the garage (case-sensitive, same as cars.json), fills in a default
 * nominal RPM if the caller left it at zero, and returns the new
 * car's index into kart_specs[] (so a caller can select it
 * immediately), or -1 if it didn't fit. Does not touch disk — see
 * config_save_cars_file for persisting the roster this now includes.
 */
int kart_specs_add_custom(const KartSpec *s, char *error, int error_cap)
{
    KartSpec candidate;
    int i;

    /* fill in nominal_rpm if the caller left it at zero before
     * validating — the same order config.c's JSON loader uses
     * (read_gearing calls kart_spec_default_gearing before
     * kart_spec_validate ever runs), so a caller that only sets the
     * stats a designer actually exposes does not get rejected over a
     * field it was never asked to fill in */
    candidate = *s;
    kart_spec_default_gearing(&candidate);
    if (!kart_spec_validate(&candidate, error, error_cap))
        return -1;
    for (i = 0; i < kart_spec_count; i++) {
        if (strcmp(kart_specs[i].name, candidate.name) == 0) {
            kart_spec_error(error, error_cap, "DUPLICATE CAR NAME");
            return -1;
        }
    }
    if (kart_spec_count >= MAX_KART_SPECS) {
        kart_spec_error(error, error_cap, "GARAGE IS FULL");
        return -1;
    }
    kart_specs[kart_spec_count] = candidate;
    kart_spec_count++;
    if (error && error_cap > 0) error[0] = '\0';
    return kart_spec_count - 1;
}

void game_settings_defaults(GameSettings *s)
{
    int i;
    memset(s, 0, sizeof(*s));
    s->countdown_seconds = 3.2f;
    s->target_race_distance_m = 3200.0f;
    s->min_laps = 2;
    s->max_laps = 4;
    for (i = 0; i < TRACK_COUNT; i++) {
        s->track_width_mult[i] = 1.0f;
        s->track_scale_mult[i] = 1.0f;
        s->track_elevation_mult[i] = 1.0f;
        s->track_laps[i] = 0;
    }
    s->rolling_resistance = DEFAULT_CRR;
    s->drivetrain_efficiency = DEFAULT_DRIVE;
    s->shift_seconds = SHIFT_TIME;
    s->steer_rate_on = 2.6f;
    s->steer_rate_center = 6.0f;
    s->steer_speed_fade = 0.035f;
    s->steer_curve = 1.55f;
    s->steer_max_angle_deg = 40.0f;
    s->steer_speed_taper = 0.048f;
    s->tire_grip_mult[TIRE_MEDIUM] = 1.00f;
    s->tire_grip_mult[TIRE_SOFT] = 1.08f;
    s->tire_grip_mult[TIRE_HARD] = 0.96f;
    /* the straight-line counterpart to the cornering numbers above: hard
     * is the dry accel/braking specialist, soft gives some of that back
     * for its cornering bite — see the GameSettings comment in game.h */
    s->tire_traction_mult[TIRE_MEDIUM] = 1.00f;
    s->tire_traction_mult[TIRE_SOFT] = 0.92f;
    s->tire_traction_mult[TIRE_HARD] = 1.08f;
    s->tire_drag_mult[TIRE_MEDIUM] = 1.00f;
    s->tire_drag_mult[TIRE_SOFT] = 1.04f;
    s->tire_drag_mult[TIRE_HARD] = 0.97f;
    /*
     * The three compounds are meant to be a real choice over a race
     * distance, not a ladder. Softs give the most grip but only while
     * they are in a narrow window and only for a while; hards are slower
     * at their best, but they roll better, warm slowly and last.
     */
    s->tire_rolling_mult[TIRE_MEDIUM] = 1.00f;
    s->tire_rolling_mult[TIRE_SOFT]   = 1.06f;
    s->tire_rolling_mult[TIRE_HARD]   = 0.90f;
    s->tire_wear_rate[TIRE_MEDIUM] = 0.0080f;
    s->tire_wear_rate[TIRE_SOFT]   = 0.0200f;
    s->tire_wear_rate[TIRE_HARD]   = 0.0018f;
    s->tire_wear_grip_loss[TIRE_MEDIUM] = 0.15f;
    s->tire_wear_grip_loss[TIRE_SOFT]   = 0.28f;
    s->tire_wear_grip_loss[TIRE_HARD]   = 0.07f;
    s->tire_temp_optimal[TIRE_MEDIUM] = 84.0f;
    s->tire_temp_optimal[TIRE_SOFT]   = 78.0f;
    s->tire_temp_optimal[TIRE_HARD]   = 88.0f;
    s->tire_temp_window[TIRE_MEDIUM] = 34.0f;
    s->tire_temp_window[TIRE_SOFT]   = 26.0f;
    s->tire_temp_window[TIRE_HARD]   = 42.0f;
    s->tire_heat_rate[TIRE_MEDIUM] = 20.0f;
    s->tire_heat_rate[TIRE_SOFT]   = 23.0f;
    s->tire_heat_rate[TIRE_HARD]   = 17.0f;
    s->tire_cool_rate[TIRE_MEDIUM] = 0.065f;
    s->tire_cool_rate[TIRE_SOFT]   = 0.065f;
    s->tire_cool_rate[TIRE_HARD]   = 0.065f;
    s->tire_off_window_grip[TIRE_MEDIUM] = 0.86f;
    s->tire_off_window_grip[TIRE_SOFT]   = 0.80f;
    s->tire_off_window_grip[TIRE_HARD]   = 0.88f;
    s->tire_ambient_c = 18.0f;
    s->weather_snow_to_ice_s = 40.0f;
    s->weather_ice_to_puddle_s = 90.0f;
    /* soft: best rubber for snow, ice AND standing water — the dry-road
     * traction the hard compound owns (tire_traction_mult) is worth
     * nothing once the road isn't dry, and soft has the most tread bite
     * left over of the three to fall back on */
    s->weather_snow_grip[TIRE_SOFT]   = 0.92f;
    s->weather_ice_grip[TIRE_SOFT]    = 0.85f;
    s->weather_puddle_grip[TIRE_SOFT] = 0.90f;
    /* medium: never the best or the worst tire on the lot */
    s->weather_snow_grip[TIRE_MEDIUM]   = 0.80f;
    s->weather_ice_grip[TIRE_MEDIUM]    = 0.72f;
    s->weather_puddle_grip[TIRE_MEDIUM] = 0.75f;
    /* hard: the dry-road accel/braking specialist, and the wrong choice
     * for absolutely anything wet, icy or snowed over — no tread to
     * bite in with, so it skates and fishtails where the other two
     * compounds would still be finding grip */
    s->weather_snow_grip[TIRE_HARD]   = 0.55f;
    s->weather_ice_grip[TIRE_HARD]    = 0.50f;
    s->weather_puddle_grip[TIRE_HARD] = 0.50f;
    s->weather_puddle_drag_mult = 1.12f;
    s->turbo_spool_rate = 0.90f;
    s->turbo_spool_decay_rate = 1.50f;
    s->turbo_max_power_bonus = 0.35f;
    s->twin_turbo_power_bonus_mult = 1.45f;
    s->boost_kickdown_spool_thresh = 0.20f;
    s->understeer_scrub = 0.22f;
    s->understeer_scrub_curve = 0.60f;
    s->engine_brake_decel = 2.2f;
    s->tc_strength = 0.55f;
    s->friction_circle_strength = 0.60f;
    s->chassis_settle_rate = 8.0f;
    s->slip_peak_deg = 8.0f;
    s->slip_falloff_range = 2.5f;
    s->slip_floor_frac = 0.62f;
    s->weight_transfer_coeff = 0.24f;
    s->yaw_inertia_mult = 1.0f;
    s->stability_control_strength = 0.85f;
    s->drift_loose_surface_bonus = 0.30f;
    s->ai_skill_mult = 1.06f;
    s->ai_brake_mult = 0.76f;
    s->ai_unguarded_line_room = 0.72f;
    s->ai_overcommit_chance = 0.055f;
    s->ai_overcommit_min_curvature = 0.028f;
    s->ai_overcommit_seconds = 1.15f;
    s->ai_overcommit_overshoot_m = 1.10f;
    s->cam_distance_m = 9.0f;
    s->cam_height_m = 3.6f;
    s->cam_min_height_m = 1.6f;
    s->cam_look_ahead_m = 6.0f;
    s->cam_look_ahead_per_mps = 0.30f;
    s->cam_look_ahead_max_m = 22.0f;
    s->cam_look_height_m = 1.2f;
    s->cam_look_min_height_m = 0.8f;
    s->cam_follow_smoothing = 0.006f;
    s->cam_look_smoothing = 0.0015f;
    s->cam_reverse_deadzone_mps = 1.2f;
    s->cam_reverse_full_mps = 6.0f;
    s->cam_reverse_orbit_rate_dps = 150.0f;
    s->cam_reverse_smoothing = 0.02f;
    s->cam_pitch_influence = 0.65f;
    s->cam_pitch_smoothing = 0.05f;
    s->cam_pitch_min_deg = -20.0f;
    s->cam_pitch_max_deg = 20.0f;
    s->cam_corner_lean = 4.5f;
    s->grade_gravity_mult = 1.0f;
    s->grade_load_effect = 1.0f;
    s->tacho_idle_rpm = 1200.0f;
    s->tacho_redline_rpm = 7800.0f;
    s->fall_seconds = 1.10f;
    s->respawn_black_seconds = 1.0f;
    s->respawn_fade_seconds = 0.8f;
    s->invincible_seconds = 5.0f;
    s->invincible_flash_hz = 5.0f;
    s->wrong_way_seconds = 2.5f;
    s->wrong_way_power = 0.35f;
}

static int settings_error(char *error, int cap, const char *message)
{
    if (error && cap > 0) {
        snprintf(error, (size_t)cap, "%s", message);
        error[cap - 1] = '\0';
    }
    return 0;
}

int game_settings_validate(GameSettings *s, char *error, int error_cap)
{
    int i;
#define FINITE_RANGE(v, lo, hi, msg) \
    do { if (!isfinite(v) || (v) < (lo) || (v) > (hi)) \
        return settings_error(error, error_cap, msg); } while (0)

    if (!s)
        return settings_error(error, error_cap, "NO SETTINGS");
    FINITE_RANGE(s->countdown_seconds, 0.0f, 10.0f, "BAD COUNTDOWN");
    FINITE_RANGE(s->target_race_distance_m, 500.0f, 20000.0f,
                 "BAD RACE DISTANCE");
    if (s->min_laps < 1 || s->min_laps > 20 ||
        s->max_laps < s->min_laps || s->max_laps > 20)
        return settings_error(error, error_cap, "BAD LAP LIMITS");
    for (i = 0; i < TRACK_COUNT; i++) {
        FINITE_RANGE(s->track_width_mult[i], 0.60f, 2.50f,
                     "BAD TRACK WIDTH");
        FINITE_RANGE(s->track_scale_mult[i], 0.60f, 2.50f,
                     "BAD TRACK SCALE");
        FINITE_RANGE(s->track_elevation_mult[i], 0.20f, 2.50f,
                     "BAD TRACK HEIGHT");
        if (s->track_laps[i] < 0 || s->track_laps[i] > 20)
            return settings_error(error, error_cap, "BAD TRACK LAPS");
    }
    FINITE_RANGE(s->rolling_resistance, 0.0f, 0.10f, "BAD ROLLING DRAG");
    FINITE_RANGE(s->drivetrain_efficiency, 0.20f, 1.0f, "BAD DRIVE EFF");
    FINITE_RANGE(s->shift_seconds, 0.02f, 2.0f, "BAD SHIFT TIME");
    FINITE_RANGE(s->steer_rate_on, 0.2f, 20.0f, "BAD STEER RATE");
    FINITE_RANGE(s->steer_rate_center, 0.2f, 30.0f, "BAD CENTER RATE");
    FINITE_RANGE(s->steer_speed_fade, 0.0f, 0.5f, "BAD SPEED FADE");
    FINITE_RANGE(s->steer_curve, 0.2f, 4.0f, "BAD STEER CURVE");
    FINITE_RANGE(s->steer_max_angle_deg, 5.0f, 60.0f, "BAD STEER ANGLE");
    FINITE_RANGE(s->steer_speed_taper, 0.0f, 0.2f, "BAD STEER TAPER");
    for (i = 0; i < TIRE_COMPOUNDS; i++) {
        FINITE_RANGE(s->tire_grip_mult[i], 0.30f, 2.0f, "BAD TIRE GRIP");
        FINITE_RANGE(s->tire_traction_mult[i], 0.30f, 2.0f,
                     "BAD TIRE TRACTION");
        FINITE_RANGE(s->tire_drag_mult[i], 0.50f, 2.0f, "BAD TIRE DRAG");
        FINITE_RANGE(s->tire_rolling_mult[i], 0.50f, 2.0f, "BAD TIRE ROLL");
        FINITE_RANGE(s->tire_wear_rate[i], 0.0f, 0.5f, "BAD TIRE WEAR");
        FINITE_RANGE(s->tire_wear_grip_loss[i], 0.0f, 0.80f,
                     "BAD TIRE FALLOFF");
        FINITE_RANGE(s->tire_temp_optimal[i], 20.0f, 200.0f, "BAD TIRE TEMP");
        FINITE_RANGE(s->tire_temp_window[i], 5.0f, 120.0f, "BAD TIRE RANGE");
        FINITE_RANGE(s->tire_heat_rate[i], 0.0f, 200.0f, "BAD TIRE HEATING");
        FINITE_RANGE(s->tire_cool_rate[i], 0.0f, 1.0f, "BAD TIRE COOLING");
        FINITE_RANGE(s->tire_off_window_grip[i], 0.30f, 1.0f,
                     "BAD COLD TIRE GRIP");
    }
    FINITE_RANGE(s->tire_ambient_c, -40.0f, 60.0f, "BAD AIR TEMP");
    FINITE_RANGE(s->weather_snow_to_ice_s, 1.0f, 600.0f, "BAD SNOW TIMER");
    FINITE_RANGE(s->weather_ice_to_puddle_s, 1.0f, 600.0f, "BAD ICE TIMER");
    if (s->weather_ice_to_puddle_s <= s->weather_snow_to_ice_s)
        return settings_error(error, error_cap, "ICE MELTS BEFORE SNOW DOES");
    for (i = 0; i < TIRE_COMPOUNDS; i++) {
        FINITE_RANGE(s->weather_snow_grip[i], 0.10f, 1.20f, "BAD SNOW GRIP");
        FINITE_RANGE(s->weather_ice_grip[i], 0.10f, 1.20f, "BAD ICE GRIP");
        FINITE_RANGE(s->weather_puddle_grip[i], 0.10f, 1.20f,
                     "BAD PUDDLE GRIP");
    }
    FINITE_RANGE(s->weather_puddle_drag_mult, 1.0f, 2.0f,
                 "BAD PUDDLE DRAG");
    FINITE_RANGE(s->turbo_spool_rate, 0.02f, 20.0f, "BAD TURBO SPOOL RATE");
    FINITE_RANGE(s->turbo_spool_decay_rate, 0.02f, 20.0f,
                 "BAD TURBO DECAY RATE");
    FINITE_RANGE(s->turbo_max_power_bonus, 0.0f, 2.0f, "BAD TURBO BONUS");
    FINITE_RANGE(s->twin_turbo_power_bonus_mult, 1.0f, 3.0f,
                 "BAD TWIN-TURBO BONUS");
    FINITE_RANGE(s->boost_kickdown_spool_thresh, 0.0f, 1.0f,
                 "BAD KICKDOWN THRESHOLD");
    FINITE_RANGE(s->understeer_scrub, 0.0f, 3.0f, "BAD UNDERSTEER SCRUB");
    FINITE_RANGE(s->understeer_scrub_curve, 0.0f, 5.0f, "BAD UNDERSTEER CURVE");
    FINITE_RANGE(s->engine_brake_decel, 0.0f, 15.0f, "BAD ENGINE BRAKING");
    FINITE_RANGE(s->tc_strength, 0.0f, 1.0f, "BAD TC STRENGTH");
    FINITE_RANGE(s->friction_circle_strength, 0.0f, 1.0f,
                 "BAD FRICTION CIRCLE");
    FINITE_RANGE(s->chassis_settle_rate, 0.5f, 60.0f, "BAD CHASSIS RATE");
    FINITE_RANGE(s->slip_peak_deg, 2.0f, 25.0f, "BAD SLIP PEAK");
    FINITE_RANGE(s->slip_falloff_range, 0.3f, 10.0f, "BAD SLIP FALLOFF");
    FINITE_RANGE(s->slip_floor_frac, 0.05f, 1.0f, "BAD SLIP FLOOR");
    FINITE_RANGE(s->weight_transfer_coeff, 0.0f, 1.0f, "BAD WEIGHT TRANSFER");
    FINITE_RANGE(s->yaw_inertia_mult, 0.2f, 5.0f, "BAD YAW INERTIA");
    FINITE_RANGE(s->stability_control_strength, 0.0f, 1.0f,
                 "BAD STABILITY CONTROL");
    FINITE_RANGE(s->drift_loose_surface_bonus, 0.0f, 1.0f,
                 "BAD LOOSE SURFACE BONUS");
    FINITE_RANGE(s->grade_gravity_mult, 0.0f, 3.0f, "BAD GRADE GRAUITY");
    FINITE_RANGE(s->grade_load_effect, 0.0f, 1.0f, "BAD GRADE LOAD");
    FINITE_RANGE(s->tacho_idle_rpm, 0.0f, 20000.0f, "BAD IDLE RPM");
    FINITE_RANGE(s->tacho_redline_rpm, 500.0f, 30000.0f, "BAD REDLINE");
    if (s->tacho_redline_rpm <= s->tacho_idle_rpm)
        return settings_error(error, error_cap, "REDLINE BELOW IDLE");
    FINITE_RANGE(s->cam_distance_m, 2.0f, 40.0f, "BAD CAM DISTANCE");
    FINITE_RANGE(s->cam_height_m, 0.5f, 20.0f, "BAD CAM HEIGHT");
    FINITE_RANGE(s->cam_min_height_m, 0.2f, 10.0f, "BAD CAM CLEARANCE");
    FINITE_RANGE(s->cam_look_ahead_m, 0.5f, 60.0f, "BAD CAM LOOH AHEAD");
    FINITE_RANGE(s->cam_look_ahead_per_mps, 0.0f, 2.0f, "BAD CAM LOOH GAIN");
    FINITE_RANGE(s->cam_look_ahead_max_m, 1.0f, 120.0f, "BAD CAM LOOH CAP");
    if (s->cam_look_ahead_max_m < s->cam_look_ahead_m)
        return settings_error(error, error_cap, "CAM LOOH CAP TOO LOW");
    FINITE_RANGE(s->cam_look_height_m, -2.0f, 10.0f, "BAD CAM AIM HEIGHT");
    FINITE_RANGE(s->cam_look_min_height_m, 0.0f, 10.0f, "BAD CAM AIM FLOOR");
    FINITE_RANGE(s->cam_follow_smoothing, 0.0f, 0.99f, "BAD CAM SMOOTHING");
    FINITE_RANGE(s->cam_look_smoothing, 0.0f, 0.99f, "BAD CAM AIM SMOOTH");
    FINITE_RANGE(s->cam_reverse_deadzone_mps, 0.0f, 15.0f, "BAD CAM DEADZONE");
    FINITE_RANGE(s->cam_reverse_full_mps, 0.1f, 30.0f, "BAD CAM FULL SPEED");
    if (s->cam_reverse_full_mps <= s->cam_reverse_deadzone_mps)
        return settings_error(error, error_cap, "CAM DEADZONE TOO HIGH");
    FINITE_RANGE(s->cam_reverse_orbit_rate_dps, 10.0f, 1080.0f,
                 "BAD CAM ORBIT RATE");
    FINITE_RANGE(s->cam_reverse_smoothing, 0.0f, 0.99f, "BAD CAM ORBIT SMOOTH");
    FINITE_RANGE(s->cam_pitch_influence, 0.0f, 1.50f, "BAD CAM PITCH GAIN");
    FINITE_RANGE(s->cam_pitch_smoothing, 0.0f, 0.99f, "BAD CAM PITCH SMOOTH");
    FINITE_RANGE(s->cam_pitch_min_deg, -60.0f, 0.0f, "BAD CAM PITCH FLOOR");
    FINITE_RANGE(s->cam_pitch_max_deg, 0.0f, 60.0f, "BAD CAM PITCH CEILING");
    FINITE_RANGE(s->cam_corner_lean, 0.0f, 20.0f, "BAD CAM CORNER LEAN");
    FINITE_RANGE(s->ai_skill_mult, 0.50f, 1.30f, "BAD AI SKILL");
    FINITE_RANGE(s->ai_brake_mult, 0.30f, 1.50f, "BAD AI BRAKES");
    FINITE_RANGE(s->ai_unguarded_line_room, 0.20f, 1.40f,
                 "BAD AI ROAD ROOM");
    FINITE_RANGE(s->ai_overcommit_chance, 0.0f, 1.0f,
                 "BAD AI RISK CHANCE");
    FINITE_RANGE(s->ai_overcommit_min_curvature, 0.001f, 0.30f,
                 "BAD AI RISK CURVE");
    FINITE_RANGE(s->ai_overcommit_seconds, 0.1f, 8.0f,
                 "BAD AI RISK TIME");
    FINITE_RANGE(s->ai_overcommit_overshoot_m, 0.0f, 8.0f,
                 "BAD AI OVERSHOOT");
    FINITE_RANGE(s->fall_seconds, 0.2f, 8.0f, "BAD FALL TIME");
    FINITE_RANGE(s->respawn_black_seconds, 0.0f, 8.0f, "BAD BLACK TIME");
    FINITE_RANGE(s->respawn_fade_seconds, 0.05f, 8.0f, "BAD FADE TIME");
    FINITE_RANGE(s->invincible_seconds, 0.0f, 30.0f, "BAD INVINCIBLE TIME");
    FINITE_RANGE(s->invincible_flash_hz, 0.5f, 30.0f, "BAD FLASH RATE");
    FINITE_RANGE(s->wrong_way_seconds, 0.5f, 15.0f, "BAD WRONG WAY TIME");
    FINITE_RANGE(s->wrong_way_power, 0.0f, 1.0f, "BAD WRONG WAY POWER");
#undef FINITE_RANGE
    if (error && error_cap > 0) error[0] = '\0';
    return 1;
}

/*
 * Where you are in a gear matters, same as real RPM does: an engine
 * makes its rated power at one nominal RPM (nominal_frac, the same
 * fraction-of-redline units as rev_frac itself — see the KartSpec
 * comment in game.h) and falls away sharply to either side of it — a
 * highly strung engine punishes wandering off its peak far more than a
 * lazy, torquey one would, which is why the falloff is steep rather
 * than a gentle ramp. The redline itself (rev_frac >= 1.0) is a hard
 * wall, same as it always was — that is what stops a gear from pulling
 * past its own top speed.
 */
float gear_power_scale_rpm(float rev_frac, float nominal_frac)
{
    const float FLOOR = 0.22f;   /* still enough to launch and to limp
                                  * home well off the power peak — a
                                  * real idling engine is not actually
                                  * making zero torque either */
    float d, width, x, peak;

    if (rev_frac >= 1.0f)
        return 0.0f;                          /* bounced off the limiter */
    if (nominal_frac < 0.05f) nominal_frac = 0.05f;
    if (nominal_frac > 0.95f) nominal_frac = 0.95f;
    d = rev_frac - nominal_frac;
    /* a full parabola spanning nominal_frac's whole distance to idle (0)
     * or to redline (1) on whichever side rev_frac falls — smooth and
     * continuously falling away the further off nominal you are, no
     * flat "dead zone", but still down at FLOOR by the time you reach
     * either end, and a hard cliff at the limiter itself */
    width = (d >= 0.0f) ? (1.0f - nominal_frac) : nominal_frac;
    x = d / width;
    peak = 1.0f - x * x;
    if (peak < 0.0f) peak = 0.0f;
    return FLOOR + (1.0f - FLOOR) * peak;
}

const char *gearbox_name(int mode)
{
    return (mode == GEARBOX_MANUAL) ? "SHIFT" : "AUTO";
}

const char *aspiration_name(int aspiration)
{
    switch (aspiration) {
    case ASPIRATION_TURBO:        return "TURBO";
    case ASPIRATION_SUPERCHARGED: return "SUPERCHARGED";
    case ASPIRATION_TWIN_TURBO:   return "TWIN-TURBO";
    default:                      return "N/A";
    }
}

const char *tire_name(int compound)
{
    switch (compound) {
    case TIRE_SOFT: return "SOFT";
    case TIRE_HARD: return "HARD";
    default:        return "STD";
    }
}

const char *drivetrain_name(int drivetrain)
{
    switch (drivetrain) {
    case DRIVETRAIN_FWD: return "FWD";
    case DRIVETRAIN_AWD: return "AWD";
    default:             return "RWD";
    }
}

const char *weather_name(int weather)
{
    switch (weather) {
    case WEATHER_SNOW:   return "SNOW";
    case WEATHER_ICE:    return "ICE";
    case WEATHER_PUDDLE: return "PUDDLE";
    default:             return "CLEAR";
    }
}

/* How much of a tire's grip survives the surface underneath it. Clear
 * pavement never touches this (always 1.0) — everything else is a
 * settings-driven trade-off: soft rubber is the one to have in snow and
 * ice, hard rubber is the one to have once it has all melted into a
 * puddle, and the medium compound never wins or loses that trade. */
float weather_tire_grip_mult(const GameSettings *settings, int weather,
                             int compound)
{
    if (compound < 0 || compound >= TIRE_COMPOUNDS)
        compound = TIRE_MEDIUM;
    if (!settings)
        return 1.0f;
    switch (weather) {
    case WEATHER_SNOW:   return settings->weather_snow_grip[compound];
    case WEATHER_ICE:    return settings->weather_ice_grip[compound];
    case WEATHER_PUDDLE: return settings->weather_puddle_grip[compound];
    default:             return 1.0f;
    }
}

/* Softer rubber grips harder and drags a little more; hard rubber gives
 * some grip back for a slipperier, faster car. This is cornering grip —
 * see tire_traction_mult below for the separate straight-line number. */
float tire_grip_mult(int compound)
{
    switch (compound) {
    case TIRE_SOFT: return 1.08f;
    case TIRE_HARD: return 0.96f;
    default:        return 1.00f;
    }
}

float tire_grip_mult_with_settings(const GameSettings *settings,
                                   int compound)
{
    if (!settings)
        return tire_grip_mult(compound);
    if (compound < 0 || compound >= TIRE_COMPOUNDS)
        compound = TIRE_MEDIUM;
    return settings->tire_grip_mult[compound];
}

/*
 * The straight-line counterpart to tire_grip_mult: a harder compound
 * puts down a cleaner, more consistent bite under acceleration and
 * braking on dry pavement than a soft one does — see the
 * GameSettings.tire_traction_mult comment in game.h for the reasoning
 * and how this differs from cornering grip.
 */
float tire_traction_mult(int compound)
{
    switch (compound) {
    case TIRE_SOFT: return 0.92f;
    case TIRE_HARD: return 1.08f;
    default:        return 1.00f;
    }
}

float tire_traction_mult_with_settings(const GameSettings *settings,
                                       int compound)
{
    if (!settings)
        return tire_traction_mult(compound);
    if (compound < 0 || compound >= TIRE_COMPOUNDS)
        compound = TIRE_MEDIUM;
    return settings->tire_traction_mult[compound];
}

/*
 * Grip is peak grip scaled by two things: how far the rubber is from the
 * temperature it wants, and how worn it is. The temperature term is a
 * parabola across the window — cold rubber and overheated rubber are both
 * short of grip — flattening out at the compound's off-window value so a
 * cold tire is poor rather than useless. Shared by the cornering and
 * straight-line readings, which differ only in which peak they start from.
 */
static float tire_condition_factor(const GameSettings *settings,
                                   int compound, float peak,
                                   float temp_c, float wear)
{
    float off, opt, window, d, temp_factor, wear_factor;

    off = settings->tire_off_window_grip[compound];
    opt = settings->tire_temp_optimal[compound];
    window = settings->tire_temp_window[compound];
    if (!(window > 1.0f))
        window = 1.0f;

    d = (temp_c - opt) / window;
    if (d < -2.0f) d = -2.0f;
    if (d >  2.0f) d =  2.0f;
    temp_factor = 1.0f - (1.0f - off) * d * d;
    if (temp_factor < off) temp_factor = off;

    if (!(wear >= 0.0f)) wear = 0.0f;
    if (wear > 1.0f) wear = 1.0f;
    wear_factor = 1.0f - settings->tire_wear_grip_loss[compound] * wear;

    return peak * temp_factor * wear_factor;
}

float tire_condition_grip(const GameSettings *settings, int compound,
                          float temp_c, float wear)
{
    if (compound < 0 || compound >= TIRE_COMPOUNDS)
        compound = TIRE_MEDIUM;
    if (!settings)
        return tire_grip_mult(compound);
    return tire_condition_factor(settings, compound,
                                 settings->tire_grip_mult[compound],
                                 temp_c, wear);
}

float tire_traction_condition(const GameSettings *settings, int compound,
                              float temp_c, float wear)
{
    if (compound < 0 || compound >= TIRE_COMPOUNDS)
        compound = TIRE_MEDIUM;
    if (!settings)
        return tire_traction_mult(compound);
    return tire_condition_factor(settings, compound,
                                 settings->tire_traction_mult[compound],
                                 temp_c, wear);
}

float tire_drag_mult(int compound)
{
    switch (compound) {
    case TIRE_SOFT: return 1.04f;
    case TIRE_HARD: return 0.97f;
    default:        return 1.00f;
    }
}

float tire_drag_mult_with_settings(const GameSettings *settings,
                                   int compound)
{
    if (!settings)
        return tire_drag_mult(compound);
    if (compound < 0 || compound >= TIRE_COMPOUNDS)
        compound = TIRE_MEDIUM;
    return settings->tire_drag_mult[compound];
}

/* Strategy sheets. conf_start over 1.0 means the driver begins the race
 * believing it can beat the grip limit: it will run wide, learn, and
 * settle down. Under 1.0 means it starts cautious and works up. */
/*
 * The sheets are *style*, not standing.
 *
 * conf_max used to differ per sheet, which meant the way a driver liked to
 * race decided how quick they ultimately were: every CRUISER was slow and
 * every LATE was fast, whoever was holding the wheel. The ceiling is now
 * the same ambition for all of them and skill scales it per driver (see
 * ai_learn), so a cautious elite is quick in a cautious way and a wild
 * novice is slow in a wild way. Engine trim is uniform for the same
 * reason. What the sheets still own: where a driver starts out on lap one,
 * how fast they adapt, how hard they commit to the apex, whether they
 * defend or attack, and how they use the gearbox.
 *
 * `line_bias` used to be a fixed lateral offset held for the whole lap —
 * which meant a sheet's line was quicker or slower depending on which way
 * that particular circuit's corners happened to bend, not on anything
 * about the driver. It is now how hard the driver commits to the real
 * apex of whatever corner is actually ahead (ai_tactical_line reads the
 * track's own signed curvature for that), so it is never negative: 0
 * drives every corner near the centerline, 1 clips it hard.
 */
const AIStrategy ai_strategies[AI_STRATEGY_COUNT] = {
/*   name        conf_start conf_max learn_up learn_down line   defend attack trim
 *                                                        up    down  shift delay     */
  { "BALANCED",   0.97f,   1.08f,   0.014f,  0.10f,   0.75f,  0.35f, 0.40f, 1.000f,
                                                             0.93f, 0.42f, 0.04f },
  { "LATE",       1.12f,   1.08f,   0.010f,  0.17f,   0.60f,  0.25f, 0.70f, 1.000f,
                                                             0.99f, 0.34f, 0.02f },
  { "INSIDE",     0.99f,   1.08f,   0.013f,  0.11f,   1.00f,  0.55f, 0.45f, 1.000f,
                                                             0.90f, 0.40f, 0.05f },
  { "DEFENDER",   0.95f,   1.08f,   0.011f,  0.09f,   0.45f,  0.95f, 0.25f, 1.000f,
                                                             0.87f, 0.38f, 0.09f },
  { "CHARGER",    1.06f,   1.08f,   0.012f,  0.14f,   0.65f,  0.30f, 0.95f, 1.000f,
                                                             0.97f, 0.38f, 0.02f },
  { "DRAFTER",    1.00f,   1.08f,   0.016f,  0.12f,   0.50f,  0.40f, 0.80f, 1.000f,
                                                             0.92f, 0.45f, 0.06f },
  { "CRUISER",    0.88f,   1.08f,   0.018f,  0.07f,   0.30f,  0.20f, 0.30f, 1.000f,
                                                             0.82f, 0.36f, 0.12f },
  /* No guts, no glory: the most overconfident sheet in the field by a
   * wide margin, clips every apex it can reach, never bothers covering
   * a line, and rides every gear to the limiter — which also means it
   * spools its turbo harder than anyone else on the grid. */
  { "YOLO",       1.14f,   1.08f,   0.008f,  0.20f,   0.80f,  0.10f, 1.00f, 1.000f,
                                                             0.99f, 0.44f, 0.01f },
};

const char *ai_strategy_name(int strategy)
{
    if (strategy < 0 || strategy >= AI_STRATEGY_COUNT)
        return "BALANCED";
    return ai_strategies[strategy].name;
}

/*
 * Difficulty presets, see the DifficultyPreset comment in game.h.
 * Ordered to match the DIFFICULTY_* enum (index == enum value): NORMAL
 * first, matching today's untouched behavior, then EASY and HARD.
 */
const DifficultyPreset difficulty_presets[DIFFICULTY_PRESET_COUNT] = {
    { "NORMAL", 0, 1.00f, DIFFICULTY_CARS_ANY,
                          DIFFICULTY_GUARDRAILS_TRACK_DEFAULT },
    { "EASY",   0, 0.85f, DIFFICULTY_CARS_UNDERDOG, DIFFICULTY_GUARDRAILS_ON },
    { "HARD",   0, 1.15f, DIFFICULTY_CARS_MATCHED, DIFFICULTY_GUARDRAILS_OFF },
};

const char *difficulty_preset_name(int preset)
{
    if (preset < 0 || preset >= DIFFICULTY_PRESET_COUNT)
        return "NORMAL";
    return difficulty_presets[preset].name;
}

/*
 * Team mode, see the TeamDef comment in game.h. Four teams, each flying
 * a colour from the existing paint palette (main.c).
 */
const TeamDef team_defs[TEAM_COUNT] = {
    { "CRIMSON", 0 },  /* RED    */
    { "AZURE",   1 },  /* BLUE   */
    { "VIPER",   2 },  /* GREEN  */
    { "BULLION", 3 },  /* GOLD   */
};

const char *team_name(int team)
{
    if (team < 0 || team >= TEAM_COUNT)
        return "CRIMSON";
    return team_defs[team].name;
}

/*
 * Combined per-team score, see the game_team_scores comment in game.h.
 * A plain linear scale (last place still scores 1) rather than a
 * motorsport-style top-10-only table, since a 12-car field with team
 * mode on is meant to make every finish count toward the total, not
 * just a podium result.
 */
void game_team_scores(const Game *g, int scores[TEAM_COUNT])
{
    int i;

    for (i = 0; i < TEAM_COUNT; i++)
        scores[i] = 0;
    for (i = 0; i < NUM_KARTS; i++) {
        const Kart *k = &g->karts[i];
        if (k->team < 0 || k->team >= TEAM_COUNT)
            continue;
        if (k->final_rank < 1 || k->final_rank > NUM_KARTS)
            continue;
        scores[k->team] += NUM_KARTS + 1 - k->final_rank;
    }
}

/*
 * Career/campaign mode, see the CareerState comment in game.h. A real
 * campaign shell calls this once a race finishes, then reads
 * last_finish_rank back out while building the next race's GameConfig
 * (game_init's grid placement reads it from there).
 */
void career_record_result(CareerState *cs, int final_rank)
{
    cs->has_last_result = 1;
    cs->last_finish_rank = final_rank;
}

/*
 * The eleven rivals. Short enough for a leaderboard column, and each one
 * keeps its grid slot from race to race so "SANDOVAL again" means
 * something. Strategy still comes from the sheet rota, so a name is an
 * identity rather than a second copy of the behaviour.
 */
/*
 * The eleven rivals. Skill and temperament are set per driver rather than
 * per strategy sheet, so the field has a sharp end, a scruffy middle and
 * a couple of people who are simply along for the ride — which is what
 * makes finishing fourth mean something.
 *
 * HOLT was, for a while, the one driver a human genuinely struggled to
 * beat. KESSLER and DUARTE are cut from the same top-of-the-field cloth
 * — genuinely hard to beat, not just quick — but not the same driver
 * twice: KESSLER is HOLT's kind of trouble (late braking, rides the
 * limiter, commits and rarely pays for it — actually a little more
 * consistent than HOLT, if anything), while DUARTE is a different
 * problem entirely, a metronome who takes the tightest line on the
 * track lap after lap and essentially never puts a wheel wrong.
 *
 * A human hunting a lap time reads well past the corner they are in —
 * the reason to run wide on the way into one bend is often the shape
 * of the next one. OSEI, NORDLI and DUARTE are the three who do that
 * too (line_lookahead_m, last column below), each reading a different
 * distance ahead of the near apex: OSEI just past this corner, NORDLI
 * a full corner further on, DUARTE further still — three different
 * amounts of the same idea rather than one setting worn by everybody.
 *
 *   name       sheet         skill  consist  aggr  tires  colour  trait      lookahead
 */
static const AIDriver ai_drivers[] = {
    /* the sharp end: quick and willing, IBARRA is quick and wild, and
     * KESSLER/DUARTE are the two who are hardest to actually beat. All
     * four nudged a little more toward TANAKA/VOSS's commitment as of
     * v1.26.1 (aggression up, consistency down a touch) — see the note
     * by VOSS below for the field-wide pass this belongs to. */
    { "HOLT",    AI_LATE,      1.05f, 0.82f, 0.93f, 1.15f, 0, "ATTACKER",   0.0f },
    { "KESSLER", AI_LATE,      1.04f, 0.85f, 0.88f, 1.05f, 1, "RUTHLESS",   0.0f },
    { "IBARRA",  AI_CHARGER,   1.02f, 0.52f, 1.00f, 1.30f, 5, "WILD",       0.0f },
    { "DUARTE",  AI_INSIDE,    1.03f, 0.89f, 0.78f, 1.00f, 4, "SURGICAL",  34.0f },
    /* the dependable middle. BASTIEN and OSEI are the only two drivers
     * that ever land in test_driver_field_has_characters' "dependable"
     * bin (consistency >= 0.90 and aggression in [0.40, 0.70]) — their
     * aggression still crept up a little for v1.26.1, just carefully
     * short of that 0.70 ceiling, and their consistency was left alone
     * rather than risk it below the bin's 0.90 floor. */
    { "BASTIEN", AI_DEFENDER,  0.99f, 0.90f, 0.64f, 0.95f, 2, "STUBBORN",   0.0f },
    { "OSEI",    AI_INSIDE,    0.98f, 0.92f, 0.55f, 0.90f, 3, "TIDY",      16.0f },
    { "NORDLI",  AI_CRUISER,   0.96f, 0.97f, 0.25f, 0.72f, 6, "SMOOTH",    24.0f },
    /* the back: one who overdrives, one who under-drives, two learners —
     * still the tail of the field, but no longer plain slow */
    /* no guts, no glory: committed to every apex, defends nothing, rides
     * every gear to the limiter — see the YOLO sheet */
    { "TANAKA",  AI_YOLO,      0.95f, 0.48f, 0.95f, 1.35f, 7, "RAGGED",     0.0f },
    { "DELGADO", AI_CRUISER,   0.93f, 0.95f, 0.15f, 0.75f, 2, "TIMID",      0.0f },
    /* CROSS's own skill nudged down slightly (0.92 -> 0.89) alongside
     * the v1.26.1 pass below, purely to keep the field's skill spread
     * (test_driver_field_has_characters requires >= 0.15) once PETRAN —
     * the previous low anchor — was gone; fits its "OVERDRIVES" trait
     * (rides ahead of its own ability) even better than before. */
    { "CROSS",   AI_YOLO,      0.89f, 0.57f, 0.84f, 1.20f, 5, "OVERDRIVES", 0.0f },
    /* v1.26.1: PETRAN (BALANCED, the field's plainest sheet, no test
     * depended on it) replaced with a second TANAKA-like recruit — same
     * no-guts-no-glory commitment, but pitched as genuinely quick
     * (skill on par with DUARTE/IBARRA, well above TANAKA's own 0.95)
     * and deliberately sloppier than TANAKA (consistency 0.40 vs 0.48):
     * goes for every apex anyway and pays for it in falls more often. */
    { "VOSS",    AI_YOLO,      1.03f, 0.40f, 0.98f, 1.30f, 1, "HEADLONG",   0.0f }
};

int ai_driver_count(void)
{
    return (int)(sizeof(ai_drivers) / sizeof(ai_drivers[0]));
}

const AIDriver *ai_driver(int grid_slot)
{
    int n = ai_driver_count();
    if (grid_slot < 0)
        grid_slot = -grid_slot;
    return &ai_drivers[grid_slot % n];
}

const char *ai_driver_name(int grid_slot)
{
    return ai_driver(grid_slot)->name;
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

void steer_axis_reset(SteerAxis *a)
{
    a->value = 0.0f;
}

float steer_axis_update_with_settings(SteerAxis *a, float target,
                                      float speed, float dt,
                                      const GameSettings *settings)
{
    GameSettings defaults;
    float rate, diff, mag;

    if (!settings) {
        game_settings_defaults(&defaults);
        settings = &defaults;
    }

    target = game_clampf(target, -1.0f, 1.0f);

    /* unwinding toward center (or crossing it) is quick, like letting a
     * real wheel spin back; winding on is slower, and slower still the
     * faster the car is going */
    if (fabsf(target) < fabsf(a->value) || target * a->value < 0.0f) {
        rate = settings->steer_rate_center;
    } else {
        rate = settings->steer_rate_on *
               (0.30f + 0.70f /
                    (1.0f + fabsf(speed) * settings->steer_speed_fade));
    }

    diff = target - a->value;
    if (diff >  rate * dt) diff =  rate * dt;
    if (diff < -rate * dt) diff = -rate * dt;
    a->value = game_clampf(a->value + diff, -1.0f, 1.0f);

    mag = powf(fabsf(a->value), settings->steer_curve);
    return (a->value < 0.0f) ? -mag : mag;
}

float steer_axis_update(SteerAxis *a, float target, float speed, float dt)
{
    return steer_axis_update_with_settings(a, target, speed, dt, NULL);
}

/* ------------------------------------------------------------------ */
/* Spec-derived display stats                                          */
/* ------------------------------------------------------------------ */

float spec_top_speed_with_settings(const KartSpec *s,
                                   const GameSettings *settings)
{
    GameSettings defaults;
    float P;
    float v = 40.0f;
    float geared;
    int i;

    if (!settings) {
        game_settings_defaults(&defaults);
        settings = &defaults;
    }
    P = s->power_hp * HP_TO_W * settings->drivetrain_efficiency;
    for (i = 0; i < 40; i++) {
        float resist = 0.5f * RHO_AIR * s->cd_a * v * v +
                       settings->rolling_resistance * s->mass_kg * GRAVITY;
        v = 0.5f * (v + P / resist);
    }
    /* whichever runs out first: the air, or top gear */
    geared = s->gear_top[s->n_gears - 1];
    if (geared < v)
        v = geared;
    return v * 3.6f;
}

float spec_top_speed(const KartSpec *s)
{
    return spec_top_speed_with_settings(s, NULL);
}

float spec_accel_time_with_settings(const KartSpec *s,
                                    const GameSettings *settings)
{
    GameSettings defaults;
    float P;
    float v = 0.5f, t = 0.0f;

    if (!settings) {
        game_settings_defaults(&defaults);
        settings = &defaults;
    }
    P = s->power_hp * HP_TO_W * settings->drivetrain_efficiency;
    while (v < V100 && t < 30.0f) {
        float a = P / (s->mass_kg * (v > 3.0f ? v : 3.0f));
        float cap = s->lat_g * GRAVITY;      /* traction limit */
        if (a > cap) a = cap;
        a -= (0.5f * RHO_AIR * s->cd_a * v * v) / s->mass_kg +
             settings->rolling_resistance * GRAVITY;
        v += a * 0.01f;
        t += 0.01f;
    }
    return t;
}

float spec_accel_time(const KartSpec *s)
{
    return spec_accel_time_with_settings(s, NULL);
}

/*
 * 100-0 braking distance, the same simplified way spec_accel_time_with_
 * settings estimates 0-100: lat_g at face value, no tire compound or
 * weather (both are a race-time choice, not a car spec). kart_step's
 * real braking is traction-limited off mu_trac now (KartSpec.
 * brake_dist_100 is no longer read for it at all), so this is the
 * estimate that actually matches what the car does on track instead of
 * a number a designer could set independently of everything else.
 */
float spec_brake_dist_100_with_settings(const KartSpec *s,
                                        const GameSettings *settings)
{
    (void)settings;
    return (V100 * V100) / (2.0f * s->lat_g * GRAVITY);
}

float spec_brake_dist_100(const KartSpec *s)
{
    return spec_brake_dist_100_with_settings(s, NULL);
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

    /* Pass the segment the backward walk already found as a hint: a
     * switchback track can loop back close to itself near the line, and
     * a global nearest-point search could otherwise snap the grid slot
     * onto a spatially-close but progress-wise-distant piece of road. */
    track_locate(t, k->x, k->z, seg, &k->seg, &frac, &k->lat, &k->y);
    k->prog_raw = (float)k->seg + frac;
    k->total_progress = (k->prog_raw > (float)t->n * 0.5f)
                            ? k->prog_raw - (float)t->n
                            : k->prog_raw;
    k->lap = (int)floorf(k->total_progress / (float)t->n);
}

/*
 * Session-best lap per circuit, per human — deliberately outside `Game`,
 * because `game_init` below unconditionally `memset`s the whole struct
 * to zero for every new race, and the entire point of this is to
 * survive that. Lives only as long as the process does: no save/load,
 * no disk I/O, just a number that keeps improving as long as the game
 * stays open across however many races get run. Full persistence
 * across sessions is a separate, bigger item — see TODO.md.
 */
static float session_best_lap_s[TRACK_COUNT][MAX_HUMANS];

float session_best_lap_get(int track_id, int human)
{
    if (track_id < 0 || track_id >= TRACK_COUNT ||
        human < 0 || human >= MAX_HUMANS)
        return 0.0f;
    return session_best_lap_s[track_id][human];
}

void session_best_lap_record(int track_id, int human, float lap_time)
{
    if (track_id < 0 || track_id >= TRACK_COUNT ||
        human < 0 || human >= MAX_HUMANS || !(lap_time > 0.0f))
        return;
    if (session_best_lap_s[track_id][human] <= 0.0f ||
        lap_time < session_best_lap_s[track_id][human])
        session_best_lap_s[track_id][human] = lap_time;
}

void session_best_lap_reset_all(void)
{
    memset(session_best_lap_s, 0, sizeof(session_best_lap_s));
}

static float kart_spec_ptw(int spec)
{
    return kart_specs[spec].power_hp / kart_specs[spec].mass_kg;
}

/*
 * Which car an AI driver gets once DifficultyPreset.ai_car_choice is
 * MATCHED or UNDERDOG (see game.h): a pool of specs judged close to, or
 * weaker than, the human's own power-to-weight, cycling through the
 * pool by ai_no so the field still has some variety rather than
 * everyone driving the identical car. Falls back to the whole roster if
 * nothing qualifies — e.g. the human is already in the weakest car on
 * an UNDERDOG grid. CARS_ANY never calls this; it keeps game_init's
 * plain ai_no % kart_spec_count exactly as it always has been.
 */
static int ai_choice_spec(int ai_no, int mode, float human_ptw)
{
    int pool[MAX_KART_SPECS];
    int pool_n = 0;
    int i;

    if (mode == DIFFICULTY_CARS_UNDERDOG) {
        for (i = 0; i < kart_spec_count; i++)
            if (kart_spec_ptw(i) <= human_ptw * 0.95f)
                pool[pool_n++] = i;
    } else if (mode == DIFFICULTY_CARS_MATCHED) {
        for (i = 0; i < kart_spec_count; i++)
            if (fabsf(kart_spec_ptw(i) - human_ptw) <= human_ptw * 0.20f)
                pool[pool_n++] = i;
    }
    if (pool_n == 0)
        for (i = 0; i < kart_spec_count; i++)
            pool[pool_n++] = i;
    return pool[ai_no % pool_n];
}

/*
 * Which starting-grid slot (0 = pole) each kart array index gets. A
 * human with a recorded CareerState result (career_record_result, see
 * game.h) claims the slot matching that result; everyone else — every
 * AI, and any human without a result yet (career mode unused, or this
 * is their first race in it) — fills the remaining slots front to back
 * in today's order: AI by driver number, then any leftover humans by
 * player index. With no career data anywhere in `career`, every slot is
 * "remaining" and this reproduces the grid game_init has always built.
 */
static void grid_slot_assign(int n_humans, const CareerState career[MAX_HUMANS],
                             int kart_slot[NUM_KARTS])
{
    int slot_kart[NUM_KARTS];   /* -1 = empty, else kart array index    */
    int h, s, next_free, ai_no;

    for (s = 0; s < NUM_KARTS; s++)
        slot_kart[s] = -1;

    for (h = 0; h < n_humans; h++) {
        int desired, slot, step;
        if (!career[h].has_last_result)
            continue;
        desired = career[h].last_finish_rank;
        if (desired < 1) desired = 1;
        if (desired > NUM_KARTS) desired = NUM_KARTS;
        slot = desired - 1;
        if (slot_kart[slot] >= 0) {
            /* taken — two humans tied last time, say. Walk outward for
             * the nearest free slot, preferring further back first:
             * arriving to find your exact spot taken, you slot in just
             * behind rather than jump the queue. */
            int back = slot, fwd = slot, found = 0;
            for (step = 1; step < NUM_KARTS && !found; step++) {
                back++;
                fwd--;
                if (back < NUM_KARTS && slot_kart[back] < 0) {
                    slot = back;
                    found = 1;
                } else if (fwd >= 0 && slot_kart[fwd] < 0) {
                    slot = fwd;
                    found = 1;
                }
            }
        }
        slot_kart[slot] = h;
    }

    next_free = 0;
    for (ai_no = 0; ai_no < NUM_KARTS - n_humans; ai_no++) {
        int kart_i = n_humans + ai_no;
        while (next_free < NUM_KARTS && slot_kart[next_free] >= 0)
            next_free++;
        slot_kart[next_free] = kart_i;
    }
    for (h = 0; h < n_humans; h++) {
        if (career[h].has_last_result)
            continue;
        while (next_free < NUM_KARTS && slot_kart[next_free] >= 0)
            next_free++;
        slot_kart[next_free] = h;
    }

    for (s = 0; s < NUM_KARTS; s++)
        kart_slot[slot_kart[s]] = s;
}

void game_init(Game *g, const GameConfig *cfg)
{
    int i, r;
    char settings_error_text[32];
    const DifficultyPreset *dp;
    int kart_slot[NUM_KARTS];

    memset(g, 0, sizeof(*g));
    g->cfg = *cfg;
    game_settings_defaults(&g->settings);
    if (cfg->settings) {
        GameSettings candidate = *cfg->settings;
        if (game_settings_validate(&candidate, settings_error_text,
                                   (int)sizeof(settings_error_text)))
            g->settings = candidate;
    }
    g->cfg.settings = NULL;       /* the race owns the snapshot above */
    if (g->cfg.n_humans < 1) g->cfg.n_humans = 1;
    if (g->cfg.n_humans > MAX_HUMANS) g->cfg.n_humans = MAX_HUMANS;

    dp = &difficulty_presets[(g->cfg.difficulty >= 0 &&
                              g->cfg.difficulty < DIFFICULTY_PRESET_COUNT)
                                  ? g->cfg.difficulty : DIFFICULTY_NORMAL];

    if (kart_spec_count < 1 || kart_spec_count > MAX_KART_SPECS)
        kart_specs_reset_defaults();

    /*
     * Any car that never went through the JSON loader — the compiled-in
     * roster, or one an older config left half-filled — still needs a
     * nominal RPM. Zero would put the power peak at idle, souring the
     * whole curve toward a permanent, precipitous bog.
     */
    for (i = 0; i < MAX_KART_SPECS; i++)
        kart_spec_default_gearing(&kart_specs[i]);

    track_init_with_settings(&g->track, cfg->track_id, &g->settings);
    /* a lap count chosen in the menu beats both the difficulty preset's
     * and the circuit's own automatic count, within the same sane
     * limits; the preset's own laps only applies when the menu did not
     * already choose one */
    if (cfg->laps_override > 0) {
        g->track.laps = cfg->laps_override;
        if (g->track.laps > 20)
            g->track.laps = 20;
    } else if (dp->laps > 0) {
        g->track.laps = dp->laps;
        if (g->track.laps > 20)
            g->track.laps = 20;
    }
    /* guardrails: a preset can force a circuit's own has_walls on or
     * off; TRACK_DEFAULT (NORMAL) leaves what track_init_with_settings
     * just set alone */
    if (dp->guardrails == DIFFICULTY_GUARDRAILS_ON)
        g->track.has_walls = 1;
    else if (dp->guardrails == DIFFICULTY_GUARDRAILS_OFF)
        g->track.has_walls = 0;

    /* weather: track_init always builds every circuit's zones, but a
     * race only actually sees them with the toggle on — off (including
     * the zero-init default) blanks them back out so track_weather_at
     * has nothing to find anywhere on the lap */
    if (g->cfg.weather != WEATHER_TOGGLE_ON) {
        g->track.n_weather_zones = 0;
        for (i = 0; i < g->track.n; i++)
            g->track.weather_zone[i] = -1;
    }

    grid_slot_assign(g->cfg.n_humans, g->cfg.career, kart_slot);

    for (i = 0; i < NUM_KARTS; i++) {
        Kart *k = &g->karts[i];
        if (i < g->cfg.n_humans) {
            k->human = i;
            k->spec = g->cfg.spec[i] % kart_spec_count;
            if (k->spec < 0) k->spec = 0;
            if (g->cfg.team_mode) {
                int team = g->cfg.team[i];
                if (team < 0 || team >= TEAM_COUNT) team = 0;
                k->team = team;
                k->paint_idx = team_defs[team].paint_idx;
            } else {
                k->team = -1;
                k->paint_idx = ((g->cfg.paint[i] % PAINT_COUNT) + PAINT_COUNT)
                               % PAINT_COUNT;
            }
            k->gearbox = (g->cfg.gearbox[i] == GEARBOX_MANUAL)
                             ? GEARBOX_MANUAL : GEARBOX_AUTO;
            k->tire = ((g->cfg.tire[i] % TIRE_COMPOUNDS) + TIRE_COMPOUNDS)
                      % TIRE_COMPOUNDS;
            /* A human never runs the AI driver, but give it sane values
             * anyway: memset leaves zero skill and zero confidence, which
             * read as "brake for everything" if anything ever asks. */
            k->ai_skill = 1.0f;
            k->tire_care = 1.0f;
            k->consistency = 1.0f;
            k->aggression = 0.5f;
            k->cur_corner = -1;
            for (r = 0; r < TRACK_MAX_CORNERS; r++)
                k->corner_conf[r] = 1.0f;
        } else {
            int ai_no = i - g->cfg.n_humans;
            const AIStrategy *st;
            int c;

            k->human = -1;
            k->driver_no = ai_no;
            k->spec = (dp->ai_car_choice == DIFFICULTY_CARS_ANY)
                          ? ai_no % kart_spec_count
                          : ai_choice_spec(ai_no, dp->ai_car_choice,
                                          kart_spec_ptw(g->karts[0].spec));
            if (g->cfg.team_mode) {
                int team = ai_no % TEAM_COUNT;
                k->team = team;
                k->paint_idx = team_defs[team].paint_idx;
            } else {
                k->team = -1;
                k->paint_idx = ai_driver(ai_no)->paint % PAINT_COUNT;
            }
            {
                const AIDriver *d = ai_driver(ai_no);
                k->strategy = d->strategy;
                /* a harder preset makes the field both bolder and
                 * faster, not just aggressive at NORMAL pace */
                k->ai_skill = d->skill * g->settings.ai_skill_mult *
                              dp->ai_aggressiveness;
                k->tire_care = d->tire_care;
                k->consistency = d->consistency;
                k->aggression = d->aggression * dp->ai_aggressiveness;
            }
            st = &ai_strategies[k->strategy];
            /* the racing line is computed fresh every frame from the
             * track's own shape (see ai_tactical_line); start on the
             * centerline and let it take over from the first frame */
            k->line_target = 0.0f;
            for (c = 0; c < TRACK_MAX_CORNERS; c++)
                k->corner_conf[c] = st->conf_start;
            k->cur_corner = -1;
            /* AI drive their own gearbox by hand, and pick rubber to suit
             * how they race: the aggressive sheets take softs */
            k->gearbox = GEARBOX_MANUAL;
            /* someone who is hard on rubber takes the harder compound;
             * the gentle ones can afford softs and their extra grip */
            k->tire = (k->tire_care > 1.10f)
                          ? TIRE_HARD
                          : (k->tire_care < 0.85f ? TIRE_SOFT
                                                  : TIRE_MEDIUM);
        }
        /* grid position: today's back-of-grid-for-humans shape unless a
         * career result says otherwise — see grid_slot_assign above */
        kart_place_on_grid(g, k, kart_slot[i]);
        k->rank = i + 1;
        k->gear = 0;
        k->tire_wear = 0.0f;
        k->tire_temp = g->settings.tire_ambient_c + 6.0f;   /* out of the
                                                             * paddock */
        k->tire_grip_now = tire_condition_grip(&g->settings, k->tire,
                                               k->tire_temp, 0.0f);
        k->tire_traction_now = tire_traction_condition(&g->settings, k->tire,
                                                       k->tire_temp, 0.0f);
        k->last_checkpoint = 0;
        k->risk_corner = -1;
        k->rng_state = 0x9e3779b9u ^ (unsigned int)(i + 1) * 0x85ebca6bu ^
                       (unsigned int)(g->track.id + 11) * 0xc2b2ae35u;
    }

    for (i = 0; i < MAX_HUMANS; i++) {
        g->pmodel[i].pass_side = 0.0f;
        g->pmodel[i].pace = 1.0f;
    }

    g->state = STATE_COUNTDOWN;
    g->countdown = g->settings.countdown_seconds;
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

/* how far ahead the racing line looks to find the apex it should be
 * leaning toward, and how much of the track's signed curvature (1/m)
 * turns into a lateral lean once scaled by a driver's commitment */
#define AI_APEX_LOOKAHEAD_M 14.0f
#define AI_APEX_LEAN_SCALE  16.0f

/*
 * The track's signed curvature at the near apex point (AI_APEX_LOOKAHEAD_M
 * up the road from `seg`), optionally blended with a second, farther
 * sample read `lookahead_m` past that near point — `lookahead_m <= 0`
 * skips the far sample entirely and this is just the single-point apex
 * read every driver used before v1.21.0.
 *
 * A driver hunting the real racing line reads past the corner they are
 * already in — the reason to run wide into this bend is often the shape
 * of the next one, not this one alone. The far sample blends in at a
 * modest weight (enough to noticeably pre-load the next bend without
 * ever letting it out-vote the corner directly under the nose), and
 * that weight itself fades toward nothing as the near corner gets
 * tight: a real driver mid-hairpin is committed to THAT corner and is
 * not still weighing what comes after it. Tight enough that a
 * genuinely tricky, technical circuit — Monarch, Berthoud Pass 2.0 —
 * sees a lookahead driver reading closer to only the near apex like
 * everyone else; a fast, flowing circuit sees the full anticipation.
 */
/*
 * Normalized 0..1 read of an AI's effective skill (Kart.ai_skill, which
 * already folds in the driver's own rating, the settings.json-wide
 * ai_skill_mult, and the difficulty preset's aggressiveness), spanning
 * the practical range across every driver on the roster and every
 * difficulty preset: roughly 0.70 at the weakest driver on EASY to 1.25
 * at the strongest on HARD. Used anywhere skill should show up as line
 * precision or execution quality — not just how much grip a driver
 * dares to use — so two drivers on the same strategy sheet feel like
 * different levels of the same style rather than identical robots.
 */
static float ai_skill01(float ai_skill)
{
    return game_clampf((ai_skill - 0.70f) / 0.55f, 0.0f, 1.0f);
}

float ai_line_curvature(const Track *t, int seg, float lookahead_m)
{
    int ahead = seg;
    float dist = 0.0f;
    float curv;

    while (dist < AI_APEX_LOOKAHEAD_M) {
        dist += t->seg_len[ahead];
        ahead = (ahead + 1) % t->n;
    }
    curv = t->curv_signed[ahead];

    if (lookahead_m > 0.0f) {
        int far = ahead;
        float fdist = dist;
        float target = dist + lookahead_m;
        float near_severity = game_clampf(fabsf(curv) * 8.0f, 0.0f, 1.0f);
        float far_weight = 0.25f * (1.0f - near_severity);

        while (fdist < target) {
            fdist += t->seg_len[far];
            far = (far + 1) % t->n;
        }
        curv = curv * (1.0f - far_weight) + t->curv_signed[far] * far_weight;
    }
    return curv;
}

/*
 * Decide where on the road this driver wants to be, as a signed offset
 * from the centerline (positive = right, see game.h).
 *
 * The base is a real racing line rather than a fixed lateral position:
 * it looks a short way up the road, reads which way and how sharply the
 * track bends there (Track.curv_signed), and leans toward that apex by
 * an amount the driver's style controls. A straight reads near zero
 * curvature, so the lean relaxes back toward the centerline on its own —
 * nothing here is held constant all lap, which is what used to make the
 * INSIDE sheet quick and the CRUISER sheet slow on a track whose corners
 * happened to mostly bend one way, regardless of who was driving.
 *
 * On top of that, a driver being chased by a human covers the side that
 * human keeps passing on (learned in ai_observe_humans), and a driver
 * hunting a car ahead picks the opposite side to set up a run at it. Both
 * of those tactical moves are scaled by `capitalize` (see ai_skill01):
 * the strategy sheet decides who WANTS to cover or attack, skill decides
 * how fully they actually pull it off — a low-skill DEFENDER still tries
 * to cover the door, just half-heartedly, while a high-skill one shuts
 * it properly.
 */
static float ai_tactical_line(const Game *g, const Kart *k)
{
    const Track *t = &g->track;
    const AIStrategy *st = &ai_strategies[k->strategy];
    float line;
    float room = track_road_half(t, k->seg) * 0.70f;
    float capitalize = 0.55f + 0.65f * ai_skill01(k->ai_skill); /* 0.55..1.20 */
    float gap;
    int who;

    /* With nothing but air past the shoulder, everyone drives closer to
     * the middle, though the amount is configurable. The separate
     * overcommit path below is what lets an aggressive driver knowingly
     * use more road and occasionally get that calculation wrong. */
    if (!t->has_walls)
        room *= g->settings.ai_unguarded_line_room;

    line = st->line_bias *
           ai_line_curvature(t, k->seg, ai_driver(k->driver_no)->line_lookahead_m) *
           AI_APEX_LEAN_SCALE * room;

    /* being hunted: cover the side of the road this human keeps using */
    who = nearest_rival(g, k, 0, 1, AI_DEFEND_RANGE, &gap);
    if (who >= 0) {
        const PlayerModel *pm = &g->pmodel[g->karts[who].human];
        float close = 1.0f - gap / AI_DEFEND_RANGE;   /* 1 = on the bumper */
        float side = pm->pass_side;
        if (fabsf(side) < 0.15f)                  /* no read yet: cover the
                                                   * side they are on now */
            side = (g->karts[who].lat > k->lat) ? 1.0f : -1.0f;
        line += side * st->defend * close * room * capitalize;
    }

    /* hunting: line up on the opposite side of the car ahead */
    who = nearest_rival(g, k, 1, 0, AI_ATTACK_RANGE, &gap);
    if (who >= 0) {
        float side = (g->karts[who].lat > k->lat) ? -1.0f : 1.0f;
        float close = 1.0f - gap / AI_ATTACK_RANGE;
        line += side * (st->attack * 0.4f + k->aggression * 0.6f) *
                close * room * capitalize;
    }

    return game_clampf(line, -room, room);
}

static float ai_random01(Kart *k)
{
    unsigned int x = k->rng_state;
    if (!x) x = 0x6d2b79f5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    k->rng_state = x;
    return (float)(x & 0x00ffffffu) / 16777216.0f;
}

static void ai_control(const Game *g, Kart *k, Input *in, float dt)
{
    const Track *t = &g->track;
    const KartSpec *s = &kart_specs[k->spec];
    const AIStrategy *st = &ai_strategies[k->strategy];
    float v = fabsf(k->speed);
    /* a driver feels the rubber: cold, worn or overheated tires mean a
     * slower corner, which is what makes a compound choice a decision */
    float mu = s->lat_g * k->ai_skill *
               (k->tire_grip_now > 0.1f ? k->tire_grip_now : 1.0f);
    float a_brk = g->settings.ai_brake_mult *
                  (V100 * V100) / (2.0f * s->brake_dist_100);
    float look, d, vmax_allow;
    int j, seg;

    memset(in, 0, sizeof(*in));

    /* Test the risk once per tight, unguarded corner. The result comes
     * from a per-driver PRNG, so a replay is repeatable and the host test
     * is not a slot machine. Attack-minded strategies are the ones likely
     * to keep a doomed outside move alive. */
    {
        int corner = t->corner_id[k->seg];
        if (corner < 0) {
            k->risk_corner = -1;
        } else if (!t->has_walls && k->fall_t <= 0.0f &&
                   k->respawn_t <= 0.0f && corner != k->risk_corner &&
                   t->corner_peak[corner] >=
                       g->settings.ai_overcommit_min_curvature) {
            float aggr = st->attack * 0.35f + k->aggression * 0.65f;
            float attack_weight = game_clampf((aggr - 0.25f) / 0.70f,
                                               0.0f, 1.0f);
            /* and a steady head does it far less often than a wild one */
            float chance = g->settings.ai_overcommit_chance * attack_weight *
                           (1.45f - k->consistency);
            float roll;
            k->risk_corner = corner;
            roll = ai_random01(k);
            if (roll < chance) {
                k->overcommits++;
                int ahead = (k->seg + 6) % t->n;
                float h0 = atan2f(t->dz[k->seg], t->dx[k->seg]);
                float h1 = atan2f(t->dz[ahead], t->dx[ahead]);
                float turn = game_angle_wrap(h1 - h0);
                float outside = (turn >= 0.0f) ? -1.0f : 1.0f;
                k->overcommit_t = g->settings.ai_overcommit_seconds *
                                  (0.85f + 0.30f * ai_random01(k));
                k->overcommit_line = outside *
                    (track_wall_half(t, k->seg) +
                     g->settings.ai_overcommit_overshoot_m *
                                      (0.55f + 0.45f * st->attack));
            }
        }
        if (k->overcommit_t > 0.0f) {
            k->overcommit_t -= dt;
            if (k->overcommit_t < 0.0f) k->overcommit_t = 0.0f;
        }
    }

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
        float wanted_line = (k->overcommit_t > 0.0f)
                                ? k->overcommit_line : k->line_target;
        float txp = t->px[seg] - t->dz[seg] * wanted_line;
        float tzp = t->pz[seg] + t->dx[seg] * wanted_line;
        float desired = atan2f(tzp - k->z, txp - k->x);
        float diff = game_angle_wrap(desired - k->heading);
        int pinned = fabsf(k->lat) > track_wall_half(t, k->seg) - 0.6f;

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
        /*
         * A consistent driver leaves a sliver in hand and rarely gets it
         * wrong; a ragged one carries a few percent more into the corner
         * than the tires will take, runs wide, and loses nerve for it.
         * This is where "poor drivers who overcommit" comes from.
         */
        float margin = 1.03f - 0.06f * k->consistency;
        /* a driver can see the road surface change ahead as well as a
         * human can — snow, ice and standing water all cost grip, so a
         * corner sat in a weather patch gets approached slower, same as
         * a corner this driver has learned to respect (CLASSIC only;
         * every other track's segments always read WEATHER_CLEAR) */
        int wx = track_weather_at(t, seg, g->race_t, &g->settings);
        float wgrip = weather_tire_grip_mult(&g->settings, wx, k->tire);
        float vt = sqrtf(mu * wgrip * GRAVITY / curv) * 0.88f * conf * margin;
        float allowed = sqrtf(vt * vt + 2.0f * a_brk * d);
        if (allowed < vmax_allow) vmax_allow = allowed;
        d += t->seg_len[seg];
        seg = (seg + 1) % t->n;
    }

    if (k->overcommit_t > 0.0f)
        vmax_allow *= 1.18f + 0.18f * st->attack;

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
        float nominal_frac = sp->nominal_rpm / g->settings.tacho_redline_rpm;
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
         * what stops the box oscillating. The "won't bog" check reads
         * the car's own engine curve directly (gear_power_scale_rpm)
         * rather than a fixed fraction, since how far below nominal_frac
         * really hurts depends on the same curve the engine itself uses.
         */
        if (k->rev_frac > st->shift_up_frac && k->gear < sp->n_gears - 1 &&
            next_frac > st->shift_down_frac + 0.06f &&
            gear_power_scale_rpm(next_frac, nominal_frac) > 0.25f) {
            in->gear_up = 1;
        } else if (k->gear > 0 && low_frac < st->shift_up_frac - 0.06f &&
                   (k->rev_frac < st->shift_down_frac ||
                    (in->brake &&
                     k->rev_frac < st->shift_down_frac + 0.18f))) {
            in->gear_down = 1;   /* also comes down the box under braking */
        }
    }

    /* No "use" button for the AI: turbo spool is automatic now, so
     * simply driving the corner exit on the throttle already builds
     * and spends it every frame. "use" only matters as an instant
     * brake-to-throttle override, and the AI never holds both at once
     * to begin with (see the accel/brake decision above) — there is
     * nothing left for it to override. */
    in->boost = 0;
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
        if (k->hit_wall || fabsf(k->lat) > track_road_half(t, k->seg) + 0.4f ||
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
            /*
             * How close to the limit this driver can ever get is what
             * skill means: an elite driver learns their way to the edge
             * of the sheet's ambition, a poor one plateaus short of it
             * however many clean laps they string together. Without this
             * the strategy sheet decided everything and the field was
             * eleven variations of the same pace.
             */
            float skill_ceiling = game_clampf(
                0.85f + 1.05f * (k->ai_skill - 0.85f), 0.80f, 1.10f);
            float ceiling = st->conf_max * skill_ceiling *
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

/*
 * The slowing-down lap.
 *
 * Crossing the line takes the car away from the player, so something has
 * to drive it: left to itself it coasts in a straight line, and on an
 * unguarded pass a straight line ends over the edge. A stand-in driver
 * takes the wheel, steers the car down the road, eases it to the side out
 * of the way of the cars still racing, and settles it into a steady
 * cruise at COOLDOWN_CRUISE_MPS — it keeps circulating for as long as the
 * race runs on, rather than coming to a dead stop and sitting there.
 *
 * It is deliberately not the racing AI: no gambling on corners, and no
 * throttle beyond what it takes to hold the cruise, since the point is a
 * marshal easing the car around, not a car still trying to race.
 */
static void cooldown_control(Game *g, Kart *k, Input *in, float dt)
{
    const Track *t = &g->track;
    float v = k->speed;
    float look, d, target, shoulder;
    int seg;

    memset(in, 0, sizeof(*in));
    k->cooldown_t += dt;

    /* aim well down the road, further the faster it is still going */
    look = game_clampf(6.0f + fabsf(v) * 0.55f, 6.0f, 26.0f);
    seg = k->seg;
    d = 0.0f;
    while (d < look) {
        d += t->seg_len[seg];
        seg = (seg + 1) % t->n;
    }

    /*
     * Pull over: ease across to whichever side it is already nearer,
     * about two thirds of the way out, so the racing line stays clear.
     */
    shoulder = track_road_half(t, seg) * 0.62f;
    if (k->lat < 0.0f)
        shoulder = -shoulder;

    {
        float txp = t->px[seg] - t->dz[seg] * shoulder;
        float tzp = t->pz[seg] + t->dx[seg] * shoulder;
        float desired = atan2f(tzp - k->z, txp - k->x);
        float diff = game_angle_wrap(desired - k->heading);
        in->steer = game_clampf(diff * 2.0f, -1.0f, 1.0f);
    }

    /*
     * Speed: come down from whatever it crossed the line at to
     * COOLDOWN_CRUISE_MPS over COOLDOWN_SECONDS, then hold that cruise
     * indefinitely — never brought to a dead stop.
     */
    target = COOLDOWN_CRUISE_MPS + (k->cooldown_v0 - COOLDOWN_CRUISE_MPS) *
             (1.0f - game_clampf(k->cooldown_t / COOLDOWN_SECONDS,
                                 0.0f, 1.0f));

    /*
     * ...but the ramp is not the only limit. Coming off the line at
     * racing speed there are still corners to negotiate, and a cool-down
     * driver who only obeys a clock drives straight over the edge of an
     * unguarded pass. Scan the road ahead and take the slowest corner in
     * it as a cap, conservatively — this is a slowing-down lap, not a
     * qualifying run.
     */
    {
        const KartSpec *sp = &kart_specs[k->spec];
        float mu = sp->lat_g * GRAVITY *
                   (k->tire_grip_now > 0.1f ? k->tire_grip_now : 1.0f);
        float scan = 0.0f;
        int s2 = k->seg;
        float cap = 1.0e9f;

        while (scan < 55.0f) {
            float c = t->curv[s2];
            if (c > 1.0e-4f) {
                /* how fast the corner can be taken, with plenty in hand,
                 * and how much of that is reachable by the time we get
                 * there given the braking distance available */
                float v_corner = sqrtf(mu / c) * 0.72f;
                float reachable = sqrtf(v_corner * v_corner +
                                        2.0f * 0.45f *
                                        (V100 * V100 /
                                         (2.0f * sp->brake_dist_100)) * scan);
                if (reachable < cap)
                    cap = reachable;
            }
            scan += t->seg_len[s2];
            s2 = (s2 + 1) % t->n;
        }
        if (cap < target)
            target = cap;
    }

    if (v > 1.6f && v > target + 0.4f)
        in->brake = 1;
    else if (v < target - 0.4f)
        in->accel = 1;         /* hold the cruise, don't just coast to it */
}

/*
 * The slip-angle curve, in one place: force rises smoothly from zero at
 * zero slip angle to the tire's peak at peak_rad, then decays toward a
 * sliding floor over the next falloff_range peak-widths, and holds
 * there — never all the way to zero, the same way a real tire still
 * drags at huge slip angles. That floor is what lets a sustained drift
 * find an equilibrium at all instead of just spinning the instant it
 * passes peak: the driver (or the physics below) can still find a
 * throttle/steering balance against a real, nonzero rear force, even
 * deep into a slide.
 *
 * floor_frac can be pushed above 1.0 (see the loose-surface bonus in
 * kart_step) to model a yawed tire on gravel or snow genuinely making
 * more usable force sliding than gripping would — the one situation
 * where that is actually true.
 */
static float slip_force_frac(float alpha_abs, float peak_rad,
                             float falloff_range, float floor_frac)
{
    if (peak_rad <= 0.001f) return 0.0f;
    if (alpha_abs <= peak_rad)
        return sinf((alpha_abs / peak_rad) * (PI_F * 0.5f));
    {
        float over = (alpha_abs - peak_rad) / peak_rad;
        float t = (falloff_range > 0.01f)
                      ? game_clampf(over / falloff_range, 0.0f, 1.0f)
                      : 1.0f;
        return 1.0f + (floor_frac - 1.0f) * t;
    }
}

static void kart_step(Game *g, Kart *k, const Input *in, float dt,
                      float power_scale)
{
    const Track *t = &g->track;
    const KartSpec *s = &kart_specs[k->spec];
    int offroad = fabsf(k->lat) > track_road_half(t, k->seg) + 0.3f;
    float grip = offroad ? s->offroad_grip : 1.0f;
    /* CLASSIC only; every other track's weather_zone is all -1 and this
     * is always WEATHER_CLEAR, multiplier 1.0 */
    int weather = track_weather_at(t, k->seg, g->race_t, &g->settings);
    float weather_grip = weather_tire_grip_mult(&g->settings, weather,
                                                k->tire);
    float mu_a = s->lat_g * GRAVITY * grip * weather_grip * k->tire_grip_now;
    /* a banked road lends the tires a hand: part of gravity now points
     * toward the corner instead of straight down, the way it does on a
     * real banked turn (v^2/r <= g*(mu + tan(bank)) rather than g*mu
     * alone). Unsigned — the bank at this exact point does not know or
     * care which way the driver is actually sliding, and every banked
     * corner in the game is banked to help the turn the road itself
     * makes, never the other way. */
    /* rudimentary shocks: the chassis's own sense of bank catches up to
     * the road's actual bank rather than snapping to it — see the
     * Kart.bank_filt comment in game.h */
    k->bank_filt += (t->bank[k->seg] - k->bank_filt) *
                    game_clampf(g->settings.chassis_settle_rate * dt,
                               0.0f, 1.0f);
    mu_a += GRAVITY * tanf(fabsf(k->bank_filt));
    /*
     * The same grip budget, read for straight-line traction instead of
     * cornering: same lat_g and road/weather conditions, but through
     * tire_traction_now (hard tire favored dry, soft favored anywhere
     * wet) instead of tire_grip_now, and with no banking term — banking
     * helps a car turn, not accelerate or stop in a straight line. This
     * is what the acceleration traction cap and braking below read; it
     * replaces KartSpec.brake_dist_100 as the thing that actually
     * decides how hard a car can stop, so braking distance is no longer
     * an unconstrained dial a designer can just set arbitrarily short —
     * it falls out of the same grip a car had to earn for cornering.
     */
    float mu_trac = s->lat_g * GRAVITY * grip * weather_grip *
                    k->tire_traction_now;
    float cd_a = s->cd_a *
                 tire_drag_mult_with_settings(&g->settings, k->tire) *
                 (weather == WEATHER_PUDDLE
                      ? g->settings.weather_puddle_drag_mult : 1.0f);
    float P = s->power_hp * HP_TO_W * g->settings.drivetrain_efficiency *
              grip * power_scale;
    float steer = game_clampf(in->steer, -1.0f, 1.0f);
    /* 1 = all power to the front axle, 0 = all to the rear; FWD and RWD
     * are just the ends of the same scale, AWD sits wherever its own
     * front_bias says (see kart_specs / KartSpec.awd_front_bias). */
    float dt_front = (s->drivetrain == DRIVETRAIN_FWD) ? 1.0f :
                      (s->drivetrain == DRIVETRAIN_RWD) ? 0.0f :
                      s->awd_front_bias;
    /* "use"/FLOOR IT is an instantaneous full-throttle stab, not a
     * resource to spend: for this one frame it is exactly as if the
     * driver planted the gas pedal on the floor and came off the brake,
     * whatever they were actually holding. It does not add power on its
     * own — see the automatic turbo spool below for that — it just
     * guarantees the engine gets the wide-open throttle it needs to use
     * whatever spool is already built, and (see the gearbox block
     * below) gives an automatic transmission a reason to kick down when
     * there is nothing built to use instead. */
    int accel = in->accel;
    int brake = in->brake;
    if (in->boost) {
        accel = 1;
        brake = 0;
    }
    /* what a brake light should read: the pedal, not engine braking off
     * the throttle, and not while FLOOR IT has just overridden it */
    k->braking = brake;

    /* the marshal helicopter has arrived: hold most of the engine back
     * until the driver turns around */
    if (k->wrong_way)
        P *= g->settings.wrong_way_power;
    float v = k->speed;
    float a = 0.0f;
    int was_inside = fabsf(k->lat) <= track_wall_half(t, k->seg);

    /*
     * Tires, before anything asks what they are worth. Work is how hard
     * they are being used — sliding, cornering and speed — which is what
     * heats them and what wears them out. They cool towards the air
     * temperature at a rate that rises with speed, so a long straight
     * brings a soft tire back under its window.
     */
    {
        int c = (k->tire >= 0 && k->tire < TIRE_COMPOUNDS) ? k->tire
                                                           : TIRE_MEDIUM;
        float sp = fabsf(v);
        /* work is what the rubber is being asked to do: rolling at all,
         * plus how much of that is spent sliding. A parked car does no
         * work, on or off the road, and neither heats nor wears. */
        float work = game_clampf(sp / 40.0f, 0.0f, 1.0f) *
                     (0.30f + 0.70f * k->slip);
        float cool = g->settings.tire_cool_rate[c] *
                     (0.55f + game_clampf(sp / 30.0f, 0.0f, 1.6f));

        if (offroad)
            work *= 1.5f;                     /* dirt is hard on rubber */

        k->tire_temp += (g->settings.tire_heat_rate[c] * work -
                         cool * (k->tire_temp - g->settings.tire_ambient_c))
                        * dt;
        if (k->tire_temp < -40.0f) k->tire_temp = -40.0f;
        if (k->tire_temp > 220.0f) k->tire_temp = 220.0f;

        k->tire_wear += g->settings.tire_wear_rate[c] * work * dt *
                        (k->tire_care > 0.0f ? k->tire_care : 1.0f);
        if (k->tire_wear > 1.0f) k->tire_wear = 1.0f;

        k->tire_grip_now = tire_condition_grip(&g->settings, c,
                                               k->tire_temp, k->tire_wear);
        k->tire_traction_now = tire_traction_condition(&g->settings, c,
                                                       k->tire_temp,
                                                       k->tire_wear);
    }

    k->hit_wall = 0;
    k->respawned = 0;
    k->lap_event = 0;
    k->lap_best_event = 0;

    if (k->invincible_t > 0.0f) {
        k->invincible_t -= dt;
        if (k->invincible_t < 0.0f) k->invincible_t = 0.0f;
    }

    /* Hold the recovered car still while its viewport is black/fading.
     * Remember held buttons so a key pressed during the blackout does not
     * turn into an accidental edge-triggered shift on return. */
    if (k->respawn_t > 0.0f) {
        k->respawn_t -= dt;
        if (k->respawn_t < 0.0f) k->respawn_t = 0.0f;
        k->speed = 0.0f;
        k->prev_up_btn = in->gear_up;
        k->prev_down_btn = in->gear_down;
        return;
    }

    /* ---- gearbox -------------------------------------------------- */
    if (k->gear < 0) k->gear = 0;
    if (k->gear >= s->n_gears) k->gear = s->n_gears - 1;
    k->rev_frac = fabsf(v) / s->gear_top[k->gear];

    /* the engine's own peak-power RPM, in the same fraction-of-redline
     * units as rev_frac (see the KartSpec comment in game.h) — the same
     * value every gear is judged against, since it is one engine curve
     * mapped through different ratios, not a per-gear property */
    {
        float nominal_frac = game_clampf(s->nominal_rpm /
                                         g->settings.tacho_redline_rpm,
                                         0.05f, 0.95f);

        if (k->shift_t > 0.0f) {
            k->shift_t -= dt;
        } else {
            int want_up = 0, want_down = 0;

            if (k->gearbox == GEARBOX_MANUAL) {
                /* edge-triggered, so holding the key does not run through
                 * the whole gearbox in three frames */
                want_up   = in->gear_up   && !k->prev_up_btn;
                want_down = in->gear_down && !k->prev_down_btn;
            } else {
                /*
                 * Automatic: shift up once comfortably past the power
                 * peak (most of the way from it to redline), down once
                 * comfortably below it, judged on where the revs would
                 * land AFTER the shift so the box does not hunt — and
                 * never into a gear so tall the engine would bog badly
                 * doing it (gear_power_scale_rpm reads the same curve
                 * the engine itself uses, not a separate fixed point).
                 */
                float next_frac = (k->gear + 1 < s->n_gears)
                                      ? fabsf(v) / s->gear_top[k->gear + 1]
                                      : 0.0f;
                float low_frac = (k->gear > 0)
                                     ? fabsf(v) / s->gear_top[k->gear - 1]
                                     : 9.0f;
                float up_thresh = nominal_frac +
                                  (1.0f - nominal_frac) * 0.75f;
                float down_thresh = nominal_frac * 0.45f;

                want_up   = (k->rev_frac > up_thresh &&
                             gear_power_scale_rpm(next_frac, nominal_frac) >
                                 0.25f);
                want_down = (k->rev_frac < down_thresh && low_frac < 0.92f);

                /*
                 * FLOOR IT kickdown: with nothing already spooled to
                 * lean on (NA, or a turbo/supercharger that hasn't built
                 * up yet), planting the pedal is instead a request for
                 * an automatic to drop a gear right now for the extra
                 * pull, the same "passing gear" a real kickdown gives —
                 * but only into a gear that genuinely makes more power
                 * at the current road speed, not into one that would
                 * just bounce off the limiter.
                 */
                if (in->boost && k->gear > 0 &&
                    k->turbo_spool < g->settings.boost_kickdown_spool_thresh &&
                    low_frac < 0.98f &&
                    gear_power_scale_rpm(low_frac, nominal_frac) >
                        gear_power_scale_rpm(k->rev_frac, nominal_frac) +
                            0.05f)
                    want_down = 1;
            }

            if (want_up && k->gear < s->n_gears - 1) {
                k->gear++;
                k->shift_t = g->settings.shift_seconds +
                             ((k->gearbox == GEARBOX_AUTO) ? 0.06f : 0.0f);
            } else if (want_down && k->gear > 0) {
                k->gear--;
                k->shift_t = g->settings.shift_seconds +
                             ((k->gearbox == GEARBOX_AUTO) ? 0.06f : 0.0f);
            }
            k->rev_frac = fabsf(v) / s->gear_top[k->gear];
        }
        k->prev_up_btn = in->gear_up;
        k->prev_down_btn = in->gear_down;

        /*
         * Forced induction: how it spools depends on KartSpec.aspiration
         * (game.h) — naturally aspirated makes no extra power at all, a
         * turbo builds it up and bleeds it off over real time (genuine
         * lag either way), a supercharger is driven straight off the
         * engine so it is there the instant the revs are, proportional
         * to rev_frac right now, with no lag in either direction. A
         * quick shift just holds a turbo's spool where it was (drive is
         * cut, but not for long enough to matter) rather than dumping
         * it, so a driver who short-shifts through a band is not
         * punished for it.
         */
        if (s->aspiration == ASPIRATION_NA) {
            k->turbo_spool = 0.0f;
        } else if (s->aspiration == ASPIRATION_SUPERCHARGED) {
            k->turbo_spool = (accel && !brake && k->shift_t <= 0.0f)
                                 ? game_clampf(k->rev_frac, 0.0f, 1.0f)
                                 : 0.0f;
        } else if (accel && !brake) {
            if (k->shift_t <= 0.0f) {
                float curve = k->rev_frac * k->rev_frac;
                if (curve > 1.0f) curve = 1.0f;
                k->turbo_spool += g->settings.turbo_spool_rate * curve * dt;
                if (k->turbo_spool > 1.0f) k->turbo_spool = 1.0f;
            }
        } else {
            k->turbo_spool -= g->settings.turbo_spool_decay_rate * dt;
            if (k->turbo_spool < 0.0f) k->turbo_spool = 0.0f;
        }

        /* drive is cut mid-shift, and where you are in the gear decides
         * how much of the engine you actually have */
        if (k->shift_t > 0.0f)
            P = 0.0f;
        else
            P *= gear_power_scale_rpm(k->rev_frac, nominal_frac);
        /*
         * Whatever is spooled goes straight to the wheels — but only in
         * proportion to how much grip is not already spent on cornering
         * (1 - last frame's slip), the way a real traction control
         * tapers boost when the tires are already loaded rather than
         * piling more power on right as a corner is asking everything
         * of them. A turbo's bonus is the biggest of the three; a
         * supercharger's is real but smaller — the trade for having no
         * lag to manage.
         */
        {
            float aspiration_bonus_mult =
                (s->aspiration == ASPIRATION_TURBO) ? 1.0f :
                (s->aspiration == ASPIRATION_TWIN_TURBO) ?
                    g->settings.twin_turbo_power_bonus_mult :
                (s->aspiration == ASPIRATION_SUPERCHARGED) ? 0.6f : 0.0f;
            P *= 1.0f + k->turbo_spool * g->settings.turbo_max_power_bonus *
                        aspiration_bonus_mult * (1.0f - k->slip);
        }
    }

    /*
     * --- longitudinal forces ---
     *
     * The road's slope is a tangent, so the component of gravity along it
     * is g*sin(theta), not g*tan(theta): on Breakneck's 29% that is a 4%
     * difference, and it is free to get right. The same angle takes weight
     * off the tires — the load is m*g*cos(theta) — so a steep climb costs
     * grip as well as speed.
     */
    {
        /* rudimentary shocks: same damped catch-up as the bank filter
         * above, for grade — the load term below is the "vertical
         * axis" this is meant to stand in for */
        k->grade_filt += (atanf(t->slope[k->seg]) - k->grade_filt) *
                         game_clampf(g->settings.chassis_settle_rate * dt,
                                    0.0f, 1.0f);
        float theta = k->grade_filt;
        float dirdot = cosf(k->heading) * t->dx[k->seg] +
                       sinf(k->heading) * t->dz[k->seg];
        float load = 1.0f - g->settings.grade_load_effect *
                            (1.0f - cosf(theta));
        a += -GRAVITY * sinf(theta) * dirdot *
             g->settings.grade_gravity_mult;
        mu_a *= load;
        mu_trac *= load;
    }
    /* how much of this frame's longitudinal acceleration or braking is
     * actually asking the tires for grip — signed, positive for
     * accelerating and negative for braking/engine-braking, since both
     * the combined-slip split below and weight transfer need to know
     * which way the car is actually loading its tires, not just how
     * hard. Fed to the per-axle combined slip in the steering section
     * below, so a driver who brakes hard while still asking for a lot
     * of yaw finds less cornering grip left over, the same way a real
     * tire's total grip is one shared budget rather than a separate
     * allowance for turning and for slowing down. Drag and rolling
     * resistance are not tire-limited (aerodynamic drag is not a
     * friction-circle force at all, and rolling resistance is a small,
     * constant loss) so neither counts here. */
    float a_tire_long = 0.0f;
    if (accel && !brake) {
        float a_drive = P / (s->mass_kg * (fabsf(v) > 3.0f ? fabsf(v) : 3.0f));
        /* two driven axles share the traction demand between them, so
         * more of the grip circle is left for accelerating without
         * either one individually breaking loose */
        float traction_frac = (s->drivetrain == DRIVETRAIN_AWD) ? 1.00f
                                                                 : 0.90f;
        /* traction control: trims how much of the engine's power
         * actually reaches the road while the tires are already sliding
         * from last frame's reading, easing back in as the slide clears
         * rather than piling more torque onto wheels already spinning
         * past their grip */
        float cap = traction_frac * mu_trac * (1.0f - g->settings.tc_strength *
                                                       k->slip);
        if (a_drive > cap) a_drive = cap;
        a += a_drive;
        a_tire_long = a_drive;
    } else if (!accel) {
        /* Engine braking: off the throttle, a real engine is still
         * turning with the wheels through a closed throttle plate, so
         * it holds the car back on its own rather than letting it
         * coast like a golf cart — more so the higher the revs, same as
         * a real engine, which is why a downshift before a descent
         * genuinely helps hold speed without ever touching the brake.
         * Applies whether or not the brake is also held. */
        float a_engine_brake = g->settings.engine_brake_decel *
                               game_clampf(k->rev_frac, 0.0f, 1.0f);
        if (fabsf(v) > 0.3f) {
            a += (v > 0.0f) ? -a_engine_brake : a_engine_brake;
            a_tire_long = -a_engine_brake;
        }
    }
    /* drag + rolling resistance oppose motion */
    if (fabsf(v) > 0.2f) {
        int rc = (k->tire >= 0 && k->tire < TIRE_COMPOUNDS) ? k->tire
                                                            : TIRE_MEDIUM;
        float a_res = (0.5f * RHO_AIR * cd_a * v * v) / s->mass_kg +
                      g->settings.rolling_resistance *
                      g->settings.tire_rolling_mult[rc] * GRAVITY;
        a += (v > 0.0f) ? -a_res : a_res;
    }
    if (brake) {
        if (v > 0.3f) {
            /* traction-limited, not a per-car settable distance: the
             * same mu_trac budget every straight-line force reads
             * (see its comment above), so there is no free dial a
             * designer can just shorten arbitrarily — a car stops
             * exactly as hard as its own grip, weather and tires
             * actually allow, same as everything else here. */
            a -= mu_trac;
            a_tire_long = -mu_trac;
        } else if (!accel) {
            /* reverse gear, gently */
            v += (-6.0f - v) * 1.2f * dt;
        }
    }
    v += a * dt;
    v = game_clampf(v, -10.0f, 90.0f);

    /*
     * Handbrake. A real handbrake locks the rear axle to its kinetic
     * (sliding) friction level regardless of slip angle, and takes
     * stability control out of the loop — see the steering section
     * below for what that actually does to the car. There is no
     * mini-turbo reward for using it: sliding a car is slow, and
     * pretending otherwise was the most arcade thing in here.
     */
    if (!k->drifting) {
        if (in->hop && fabsf(steer) > 0.2f && v > 8.0f)
            k->drifting = 1;
    } else if (!in->hop || v < 5.0f) {
        k->drifting = 0;
    }

    /*
     * --- steering: a real dynamic bicycle model ---
     *
     * A tire's lateral force is a curve in slip angle (the angle
     * between where a wheel points and where it is actually
     * traveling): it rises smoothly to a peak, then falls off toward a
     * sliding floor that never quite reaches zero (slip_force_frac,
     * above). Front and rear each get their own slip angle and read
     * that same curve separately — this is what makes counter-steering
     * a real, emergent mechanic rather than a scripted one: steering
     * further into a slide pushes the front's own slip angle further
     * from zero too (alpha_f below is the front's own velocity angle
     * minus the steer angle directly), so the front keeps losing grip
     * right when a driver most wants it to bite; steering away
     * (opposite lock) brings alpha_f back toward zero and hands the
     * front axle back its grip — the textbook reason counter-steering
     * works at all.
     *
     * Combined slip, per axle: whatever this frame's accelerating or
     * braking (a_tire_long, set above) is asking of the tires eats into
     * that same axle's lateral budget, the same shared-grip idea the
     * whole-car friction circle used before v1.28.0, now split by which
     * axle actually carries it. Braking acts on all four wheels, so it
     * costs both axles alike; accelerating only costs the driven
     * axle(s) (dt_front), so an undriven axle keeps its whole lateral
     * budget — this is the whole reason RWD and AWD can drift on the
     * throttle and FWD genuinely cannot the same way: spending the
     * rear's own budget on drive lowers the rear's own lateral force,
     * which grows the yaw moment, which grows the slide — throttle
     * becomes a yaw control once the rear is past its own peak, exactly
     * backwards from what it does below peak.
     *
     * Weight transfer adds "squat and dive" on top: accelerating loads
     * the rear (more grip there, less at the nose), braking or lifting
     * off loads the front — a genuine way to provoke oversteer with no
     * handbrake at all, same as a real trail-brake or lift-off flick.
     *
     * Past its own peak, a tire's force actually falls as slip angle
     * keeps growing — genuinely open-loop unstable once the rear gets
     * there: more yaw angle costs rear force, which grows yaw further
     * still. Ordinary grip driving never reaches this (stability
     * control below holds the rear's own floor close to its peak); the
     * handbrake deliberately switches it off and hands the driver the
     * real dynamics, unsmoothed — they are the stabilizer now, via
     * counter-steer and throttle, the same way a real driver is.
     */
    {
        float delta_max = (g->settings.steer_max_angle_deg * PI_F / 180.0f) /
                          (1.0f + fabsf(v) * g->settings.steer_speed_taper);
        /* the driver's actual road-wheel angle — and the whole
         * counter-steer budget available to catch a slide with, since
         * it cannot ask for more than this either way */
        float steer_angle = steer * delta_max;
        float a_dist = s->wheelbase * 0.5f;     /* CG assumed centered —
                                                 * no separate front/rear
                                                 * weight-distribution
                                                 * stat; see HANDOFF.md */
        float b_dist = s->wheelbase * 0.5f;
        float peak_rad = g->settings.slip_peak_deg * PI_F / 180.0f;
        /* slip angles are only well-defined with a real forward speed
         * to measure them against — same 6 m/s floor the old yaw_cap
         * used. Always positive, even in reverse: atan2's second
         * argument going negative with the first pinned at ~0 (a
         * car creeping backward with no lateral velocity at all, the
         * exact state a fresh respawn or a parked car on a grade sits
         * in) returns a spurious +/-pi "slip angle" — 180 degrees of
         * sideways sliding that was never actually happening — rather
         * than the genuine near-zero angle straight-line reversing
         * actually has. Reverse's own slip-angle physics is not
         * modeled precisely here, same simplification the rest of
         * reverse gear already gets (see "reverse gear, gently"
         * above) — this just keeps it from blowing up. */
        float u = fmaxf(fabsf(v), 6.0f);
        float alpha_f = atan2f(k->vy + k->yaw_rate * a_dist, u) - steer_angle;
        float alpha_r = atan2f(k->vy - k->yaw_rate * b_dist, u);

        float wt = g->settings.weight_transfer_coeff *
                  (a_tire_long / GRAVITY);
        float front_load_mult = game_clampf(1.0f - wt, 0.4f, 1.6f);
        float rear_load_mult  = game_clampf(1.0f + wt, 0.4f, 1.6f);

        float front_long, rear_long;
        if (a_tire_long > 0.0f) {
            front_long = a_tire_long * dt_front;
            rear_long  = a_tire_long * (1.0f - dt_front);
        } else {
            front_long = a_tire_long;
            rear_long  = a_tire_long;
        }
        float circle = g->settings.friction_circle_strength;
        float front_peak = mu_a * front_load_mult;
        float rear_peak  = mu_a * rear_load_mult;
        {
            float fl = front_long * circle, rl = rear_long * circle;
            float f2 = front_peak * front_peak - fl * fl;
            float r2 = rear_peak * rear_peak - rl * rl;
            front_peak = (f2 > 0.0f) ? sqrtf(f2) : 0.0f;
            rear_peak  = (r2 > 0.0f) ? sqrtf(r2) : 0.0f;
        }

        /* loose surfaces: a yawed tire past its peak builds a wedge of
         * displaced gravel or snow that genuinely adds force a dry
         * tire's friction coefficient alone would not give — real
         * off-road or on snow/ice, imaginary on tarmac */
        float loose_bonus = 0.0f;
        if (offroad)
            loose_bonus = g->settings.drift_loose_surface_bonus *
                         (1.0f - s->offroad_grip);
        else if (weather == WEATHER_SNOW || weather == WEATHER_ICE)
            loose_bonus = g->settings.drift_loose_surface_bonus * 0.6f;

        /* stability control: everyday grip driving does not let the
         * rear axle actually run away with itself — pulling the
         * handbrake (k->drifting) is the one deliberate way to take
         * that assist out of the loop */
        {
            float rear_floor = g->settings.slip_floor_frac + loose_bonus;
            float front_floor = g->settings.slip_floor_frac + loose_bonus;
            float frac_f, frac_r;

            if (!k->drifting)
                rear_floor += (1.0f - rear_floor) *
                             g->settings.stability_control_strength;

            frac_f = slip_force_frac(fabsf(alpha_f), peak_rad,
                                     g->settings.slip_falloff_range,
                                     front_floor);
            /* the handbrake locks the rear to kinetic friction outright
             * — a locked wheel has no speed-dependent rolling grip left
             * to peak toward, just the sliding floor, whatever the rear
             * slip angle actually reads */
            frac_r = k->drifting
                        ? rear_floor
                        : slip_force_frac(fabsf(alpha_r), peak_rad,
                                          g->settings.slip_falloff_range,
                                          rear_floor);

            {
                float Fyf = -copysignf(front_peak * frac_f, alpha_f);
                float Fyr = -copysignf(rear_peak * frac_r, alpha_r);
                float over_f = fmaxf(0.0f,
                                     (fabsf(alpha_f) - peak_rad) / peak_rad);
                float over_r = fmaxf(0.0f,
                                     (fabsf(alpha_r) - peak_rad) / peak_rad);
                float r_gyr_sq = (s->wheelbase * s->wheelbase / 12.0f) *
                                g->settings.yaw_inertia_mult;
                float yaw_moment = a_dist * Fyf * cosf(steer_angle) -
                                   b_dist * Fyr;
                float yaw_accel = yaw_moment /
                                  (r_gyr_sq > 0.01f ? r_gyr_sq : 0.01f);
                float vy_dot = Fyf * cosf(steer_angle) + Fyr - v * k->yaw_rate;

                k->slip = game_clampf(fmaxf(over_f, over_r), 0.0f, 1.0f);
                /* sliding this hard bleeds real speed: the same
                 * progressive curve understeer scrub always used, now
                 * driven by the true slip angle rather than a yaw_cap
                 * ratio — most of a hard drift's own cost on tarmac,
                 * where there is no loose-surface force to spend it on
                 * instead */
                if (k->slip > 0.0f)
                    v -= g->settings.understeer_scrub *
                         (1.0f + g->settings.understeer_scrub_curve *
                                 k->slip) *
                         mu_a * k->slip * dt;
                else
                    k->slip = 0.0f;

                k->yaw_rate += yaw_accel * dt;
                k->yaw_rate = game_clampf(k->yaw_rate, -8.0f, 8.0f);
                k->vy += vy_dot * dt;
                k->vy = game_clampf(k->vy, -40.0f, 40.0f);

                /* stability control's real job: a fully-gripped tire simply
                 * cannot sustain the runaway yaw_rate/vy combination that
                 * pushes both axles past their peak at once and keeps them
                 * there — that only happens on paper, when a single hard
                 * frame (a tight corner taken too fast) launches the state
                 * past what the force curve can pull back on its own. Real
                 * ESC brakes individual wheels to cut the excess yaw
                 * directly rather than waiting for the slip-angle curve to
                 * do it; model that directly here as a fast blend toward
                 * the kinematic (fully-gripped, no-slide) reference whenever
                 * the handbrake isn't deliberately overriding it. This is
                 * the backstop that keeps an AI driver — which never lifts,
                 * never countersteers, and never releases a handbrake it
                 * never pulled — from ever getting stuck oscillating at the
                 * hard safety clamps instead of recovering the corner. */
                if (!k->drifting) {
                    /* the reference is kinematic (zero-slip) turning *capped
                     * by the same lateral grip limit every other axle force
                     * in this function already respects* — not the raw
                     * unclamped kinematic formula, which at full lock and
                     * real speed asks for far more yaw rate than any tire
                     * could ever deliver and would drag the state toward an
                     * equally unrealistic number instead of a safe one. */
                    float kinematic_yaw = (fabsf(s->wheelbase) > 0.01f)
                                       ? v * tanf(steer_angle) / s->wheelbase
                                       : 0.0f;
                    float grip_cap = mu_a / (fabsf(v) > 1.0f ? fabsf(v) : 1.0f);
                    float ref_yaw = game_clampf(kinematic_yaw, -grip_cap,
                                                grip_cap);
                    float blend = game_clampf(
                        g->settings.stability_control_strength * 6.0f * dt,
                        0.0f, 1.0f);
                    k->yaw_rate += (ref_yaw - k->yaw_rate) * blend;
                    k->vy += (0.0f - k->vy) * blend;
                }
            }
        }
        k->heading = game_angle_wrap(k->heading + k->yaw_rate * dt);
    }
    k->steer_vis += (steer - k->steer_vis) * 10.0f * dt;
    k->speed = v;

    /* --- integrate --- */
    {
        /* the car's real velocity vector, not just its heading: a
         * sliding car (k->vy != 0) travels somewhere other than exactly
         * where its nose points, same as a real one drifting through a
         * bend */
        float ch = cosf(k->heading), sh = sinf(k->heading);
        k->x += (ch * v - sh * k->vy) * dt;
        k->z += (sh * v + ch * k->vy) * dt;
    }

    /* --- track relation --- */
    {
        int seg;
        float frac, lat, y, newp, d, wall_here;

        track_locate(t, k->x, k->z, k->seg, &seg, &frac, &lat, &y);

        /* the road is not the same width all the way round */
        wall_here = track_wall_half(t, seg);
        if (t->has_walls) {
            /* guardrail: the car is held on the road, and pays in speed */
            if (fabsf(lat) > wall_here) {
                float clamped = game_clampf(lat, -wall_here, wall_here);
                float excess = lat - clamped;
                k->x += t->dz[seg] * excess;
                k->z -= t->dx[seg] * excess;
                k->speed *= (1.0f - 1.2f * dt);
                if (was_inside)
                    k->hit_wall = 1;
                lat = clamped;
            }
        } else if (fabsf(lat) > wall_here) {
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

        /*
         * The wrong-way marshal. Judged on net progress along the
         * centerline rather than heading, so a car that spins but is
         * still net moving forward is left alone. Humans only — the AI's
         * own reverse-out recovery must never be penalised, and a
         * finished car is being driven by the cool-down routine, which
         * never selects reverse on its own.
         */
        if (k->human >= 0 && !k->finished) {
            k->wrong_way_t += (d < -0.02f) ? dt : -dt;
            if (k->wrong_way_t < 0.0f) k->wrong_way_t = 0.0f;
            if (k->wrong_way_t > g->settings.wrong_way_seconds * 2.0f)
                k->wrong_way_t = g->settings.wrong_way_seconds * 2.0f;
            if (k->wrong_way_t >= g->settings.wrong_way_seconds)
                k->wrong_way = 1;
            else if (k->wrong_way_t <= 0.0f)
                k->wrong_way = 0;
        } else {
            k->wrong_way_t = 0.0f;
            k->wrong_way = 0;
        }
        k->seg = seg;
        k->lat = lat;
        if (k->fall_t <= 0.0f)
            k->y = y;          /* on the road; while falling, k->y is the
                                * height the fall branch is writing */
        k->lap = (int)floorf(k->total_progress / (float)t->n);

        /* remember the last checkpoint reached while safely on the road */
        if (k->fall_t <= 0.0f && fabsf(lat) <= track_road_half(t, seg) + 1.0f) {
            int cp = track_checkpoint_for(t, seg);
            if (cp >= 0)
                k->last_checkpoint = cp;
        }
    }

    /* --- fished out of the void, back at the last checkpoint --- */
    if (k->fall_t > g->settings.fall_seconds) {
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
        k->vy = 0.0f;
        k->yaw_rate = 0.0f;
        k->gear = 0;
        k->shift_t = 0.0f;
        k->turbo_spool = 0.0f;
        k->drifting = 0;
        k->slip = 0.0f;
        k->fall_t = 0.0f;
        k->respawned = 1;
        k->falls++;
        k->respawn_t = g->settings.respawn_black_seconds +
                       g->settings.respawn_fade_seconds;
        k->invincible_t = k->respawn_t + g->settings.invincible_seconds;
        k->overcommit_t = 0.0f;
        k->seg = cseg;
        k->lat = 0.0f;
        k->wrong_way_t = 0.0f;   /* faced the right way by the teleport */
        k->wrong_way = 0;

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
            if (a->fall_t > 0.0f || b->fall_t > 0.0f ||
                a->respawn_t > 0.0f || b->respawn_t > 0.0f ||
                a->invincible_t > 0.0f || b->invincible_t > 0.0f)
                continue;
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
    int i;

    if (!(dt > 0.0f)) return;      /* also rejects NaN */
    if (dt > 0.1f) dt = 0.1f;

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

        if (k->finished) {
            /*
             * Past the flag, a stand-in driver brings the car home. This
             * is for the AI too: a finished AI car left racing carries on
             * attacking corners it has no reason to attack, and falls off
             * the mountain doing it.
             */
            cooldown_control(g, k, &in, dt);
        } else if (k->human >= 0) {
            in = inputs[k->human];
        } else {
            /* ease onto the tactical line rather than darting sideways,
             * and give more room to a human who has been leaning on us */
            float want = ai_tactical_line(g, k);
            /* how tightly this driver actually tracks their own ideal
             * line: skill is what turns "knows the racing line exists"
             * into "adheres to it" — a low-skill driver still eases
             * toward the same apex ai_tactical_line found, just more
             * slowly, so it is measurably still settling into a corner
             * a sharper driver already committed to */
            float line_ease = 1.1f + 1.5f * ai_skill01(k->ai_skill);
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
                              game_clampf(dt * line_ease, 0.0f, 1.0f);
            ai_control(g, k, &in, dt);
            scale = ai_power_scale(g, k);
        }
        {
            int lap_before = k->lap;

            kart_step(g, k, &in, dt, scale);

            /*
             * A lap is only a lap when the car drives across the line:
             * exactly one more than it had, and long enough ago to be
             * real. Anything else — a respawn, a car rolling backwards
             * over the line — leaves the stopwatch alone.
             */
            /*
             * The grid is behind the line, so the very first crossing
             * starts lap one rather than completing anything: the car has
             * not been round yet. Counting it timed the rollout as a lap.
             */
            /*
             * ...and only a lap the car has not already been credited
             * with. Falling off just after the line puts it back before
             * the line, and driving over it again must not buy a second
             * copy of the same lap — which is how a 1.8 s "lap" appeared.
             */
            if (k->lap == lap_before + 1 && k->lap > k->laps_done &&
                !k->finished) {
                float t_lap = g->race_t - k->lap_start_t;
                if (t_lap > 1.0f) {
                    k->last_lap_time = t_lap;
                    if (k->best_lap_time <= 0.0f ||
                        t_lap < k->best_lap_time) {
                        k->best_lap_time = t_lap;
                        k->lap_best_event = 1;
                    }
                    if (k->human >= 0)
                        session_best_lap_record(g->cfg.track_id,
                                                k->human, t_lap);
                    k->laps_done++;
                    k->lap_event = 1;
                }
                k->lap_start_t = g->race_t;
            } else if (k->lap != lap_before) {
                k->lap_start_t = g->race_t;   /* the lap was not driven */
            }
        }

        if (k->human < 0)
            ai_learn(g, k);

        if (!k->finished && k->lap >= g->track.laps) {
            k->finished = 1;
            k->finish_time = g->race_t;
            k->cooldown_t = 0.0f;
            k->cooldown_v0 = fabsf(k->speed);
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
