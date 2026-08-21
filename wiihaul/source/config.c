/* WiiHaul JSON configuration: small, dependency-free, C99. */
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

/* ------------------------------------------------------------------ */
/* A minimal recursive-descent JSON reader. No token tree: each parser
 * below reads directly off a `const char **` cursor, matching known
 * field names as it goes and skipping anything it does not recognize —
 * forward-compatible with a hand-edited file that has stray fields or a
 * newer field this build predates.                                    */
/* ------------------------------------------------------------------ */

static int set_error(char *error, int cap, const char *message)
{
    if (error && cap > 0) {
        snprintf(error, (size_t)cap, "%s", message);
        error[cap - 1] = '\0';
    }
    return 0;
}

static void skip_ws(const char **p)
{
    while (isspace((unsigned char)**p)) (*p)++;
}

static int match_char(const char **p, char c)
{
    skip_ws(p);
    if (**p == c) { (*p)++; return 1; }
    return 0;
}

static int parse_string(const char **p, char *out, int cap)
{
    int n = 0;
    skip_ws(p);
    if (**p != '"') return 0;
    (*p)++;
    while (**p && **p != '"') {
        char c = **p;
        if (c == '\\') {
            (*p)++;
            if (!**p) return 0;
            switch (**p) {
            case 'n': c = '\n'; break;
            case 't': c = '\t'; break;
            default:  c = **p;  break;
            }
        }
        if (out && n < cap - 1) out[n] = c;
        n++;
        (*p)++;
    }
    if (**p != '"') return 0;
    (*p)++;
    if (out) out[(n < cap) ? n : cap - 1] = '\0';
    return 1;
}

static int parse_number(const char **p, float *out)
{
    char *end;
    double v;
    skip_ws(p);
    v = strtod(*p, &end);
    if (end == *p) return 0;
    *out = (float)v;
    *p = end;
    return 1;
}

static int parse_bool(const char **p, int *out)
{
    skip_ws(p);
    if (strncmp(*p, "true", 4) == 0)  { *p += 4; *out = 1; return 1; }
    if (strncmp(*p, "false", 5) == 0) { *p += 5; *out = 0; return 1; }
    return 0;
}

static int skip_value(const char **p)
{
    skip_ws(p);
    if (**p == '"') { char tmp[256]; return parse_string(p, tmp, sizeof(tmp)); }
    if (**p == '{') {
        (*p)++;
        skip_ws(p);
        if (**p == '}') { (*p)++; return 1; }
        for (;;) {
            char key[64];
            if (!parse_string(p, key, sizeof(key))) return 0;
            if (!match_char(p, ':')) return 0;
            if (!skip_value(p)) return 0;
            skip_ws(p);
            if (**p == '}') { (*p)++; return 1; }
            if (!match_char(p, ',')) return 0;
        }
    }
    if (**p == '[') {
        (*p)++;
        skip_ws(p);
        if (**p == ']') { (*p)++; return 1; }
        for (;;) {
            if (!skip_value(p)) return 0;
            skip_ws(p);
            if (**p == ']') { (*p)++; return 1; }
            if (!match_char(p, ',')) return 0;
        }
    }
    if (strncmp(*p, "true", 4) == 0)  { *p += 4; return 1; }
    if (strncmp(*p, "false", 5) == 0) { *p += 5; return 1; }
    if (strncmp(*p, "null", 4) == 0)  { *p += 4; return 1; }
    { float dummy; return parse_number(p, &dummy); }
}

/* Read a JSON array of numbers into out[], up to max entries. *count is
 * set to how many were read. */
static int parse_number_array(const char **p, float *out, int max,
                              int *count)
{
    *count = 0;
    if (!match_char(p, '[')) return 0;
    skip_ws(p);
    if (**p == ']') { (*p)++; return 1; }
    for (;;) {
        float v;
        if (!parse_number(p, &v)) return 0;
        if (*count < max) out[(*count)++] = v;
        skip_ws(p);
        if (**p == ']') { (*p)++; return 1; }
        if (!match_char(p, ',')) return 0;
    }
}

/* ------------------------------------------------------------------ */
/* Fixed-schema object readers                                        */
/* ------------------------------------------------------------------ */

#define FIELD_STR(key, dst, cap) \
    if (strcmp(k, key) == 0) { if (!parse_string(&p, dst, cap)) \
        return set_error(error, error_cap, "bad string for " key); }
#define FIELD_NUM(key, dst) \
    else if (strcmp(k, key) == 0) { if (!parse_number(&p, &(dst))) \
        return set_error(error, error_cap, "bad number for " key); }

static int parse_truck_spec(const char **pp, TruckSpec *t, char *error,
                            int error_cap)
{
    const char *p = *pp;
    memset(t, 0, sizeof(*t));
    t->nominal_rpm = 0.5f;
    if (!match_char(&p, '{'))
        return set_error(error, error_cap, "expected '{' in truck");
    skip_ws(&p);
    if (*p == '}') { p++; *pp = p; return 1; }
    for (;;) {
        char k[64];
        if (!parse_string(&p, k, sizeof(k)))
            return set_error(error, error_cap, "expected key in truck");
        if (!match_char(&p, ':'))
            return set_error(error, error_cap, "expected ':' in truck");

        if (strcmp(k, "name") == 0) {
            if (!parse_string(&p, t->name, sizeof(t->name)))
                return set_error(error, error_cap, "bad truck name");
        }
        FIELD_NUM("mass_kg", t->mass_kg)
        FIELD_NUM("power_hp", t->power_hp)
        FIELD_NUM("wheelbase", t->wheelbase)
        FIELD_NUM("body_length", t->body_length)
        FIELD_NUM("body_width", t->body_width)
        FIELD_NUM("hitch_setback", t->hitch_setback)
        FIELD_NUM("max_steer_deg", t->max_steer_deg)
        FIELD_NUM("reverse_top_mps", t->reverse_top_mps)
        FIELD_NUM("brake_decel_ref", t->brake_decel_ref)
        FIELD_NUM("ref_mass_kg", t->ref_mass_kg)
        FIELD_NUM("cd_a", t->cd_a)
        FIELD_NUM("nominal_rpm", t->nominal_rpm)
        else if (strcmp(k, "gear_top") == 0) {
            if (!parse_number_array(&p, t->gear_top, MAX_GEARS,
                                    &t->n_gears))
                return set_error(error, error_cap, "bad gear_top array");
        } else {
            if (!skip_value(&p))
                return set_error(error, error_cap, "bad value in truck");
        }

        skip_ws(&p);
        if (*p == '}') { p++; break; }
        if (!match_char(&p, ','))
            return set_error(error, error_cap, "expected ',' in truck");
    }
    if (t->n_gears < 1)
        return set_error(error, error_cap, "truck needs at least one gear");
    *pp = p;
    return 1;
}

static int trailer_type_from_name(const char *name)
{
    if (strcmp(name, "box") == 0)     return TRAILER_BOX;
    if (strcmp(name, "flatbed") == 0) return TRAILER_FLATBED;
    if (strcmp(name, "tanker") == 0)  return TRAILER_TANKER;
    if (strcmp(name, "lowboy") == 0)  return TRAILER_LOWBOY;
    if (strcmp(name, "pup") == 0)     return TRAILER_PUP;
    return -1;
}

static int parse_trailer_spec(const char **pp, TrailerSpec *t, char *error,
                              int error_cap)
{
    const char *p = *pp;
    memset(t, 0, sizeof(*t));
    if (!match_char(&p, '{'))
        return set_error(error, error_cap, "expected '{' in trailer");
    skip_ws(&p);
    if (*p == '}') { p++; *pp = p; return 1; }
    for (;;) {
        char k[64];
        if (!parse_string(&p, k, sizeof(k)))
            return set_error(error, error_cap, "expected key in trailer");
        if (!match_char(&p, ':'))
            return set_error(error, error_cap, "expected ':' in trailer");

        if (strcmp(k, "name") == 0) {
            if (!parse_string(&p, t->name, sizeof(t->name)))
                return set_error(error, error_cap, "bad trailer name");
        } else if (strcmp(k, "type") == 0) {
            char type_name[24];
            int type;
            if (!parse_string(&p, type_name, sizeof(type_name)))
                return set_error(error, error_cap, "bad trailer type");
            type = trailer_type_from_name(type_name);
            if (type < 0)
                return set_error(error, error_cap, "unknown trailer type");
            t->type = type;
        }
        FIELD_NUM("length", t->length)
        FIELD_NUM("hitch_offset", t->hitch_offset)
        FIELD_NUM("width", t->width)
        FIELD_NUM("height", t->height)
        FIELD_NUM("empty_mass_kg", t->empty_mass_kg)
        FIELD_NUM("max_cargo_kg", t->max_cargo_kg)
        FIELD_NUM("jackknife_limit_deg", t->jackknife_limit_deg)
        FIELD_NUM("slosh_strength", t->slosh_strength)
        else {
            if (!skip_value(&p))
                return set_error(error, error_cap, "bad value in trailer");
        }

        skip_ws(&p);
        if (*p == '}') { p++; break; }
        if (!match_char(&p, ','))
            return set_error(error, error_cap, "expected ',' in trailer");
    }
    *pp = p;
    return 1;
}

static int find_truck_by_name(const char *name)
{
    int i;
    for (i = 0; i < truck_spec_count; i++)
        if (strcmp(truck_specs[i].name, name) == 0) return i;
    return -1;
}

static int find_trailer_by_name(const char *name)
{
    int i;
    for (i = 0; i < trailer_spec_count; i++)
        if (strcmp(trailer_specs[i].name, name) == 0) return i;
    return -1;
}

static int parse_rig_spec(const char **pp, RigSpec *r, char *error,
                          int error_cap)
{
    const char *p = *pp;
    memset(r, 0, sizeof(*r));
    r->truck_idx = -1;
    if (!match_char(&p, '{'))
        return set_error(error, error_cap, "expected '{' in rig");
    skip_ws(&p);
    if (*p == '}') { p++; *pp = p; return 1; }
    for (;;) {
        char k[64];
        if (!parse_string(&p, k, sizeof(k)))
            return set_error(error, error_cap, "expected key in rig");
        if (!match_char(&p, ':'))
            return set_error(error, error_cap, "expected ':' in rig");

        if (strcmp(k, "name") == 0) {
            if (!parse_string(&p, r->name, sizeof(r->name)))
                return set_error(error, error_cap, "bad rig name");
        } else if (strcmp(k, "truck") == 0) {
            char name[RIG_NAME_LEN];
            if (!parse_string(&p, name, sizeof(name)))
                return set_error(error, error_cap, "bad rig truck ref");
            r->truck_idx = find_truck_by_name(name);
            if (r->truck_idx < 0)
                return set_error(error, error_cap, "unknown truck in rig");
        } else if (strcmp(k, "trailers") == 0) {
            if (!match_char(&p, '['))
                return set_error(error, error_cap, "expected '[' trailers");
            skip_ws(&p);
            if (*p != ']') {
                for (;;) {
                    char name[RIG_NAME_LEN];
                    int idx;
                    if (!parse_string(&p, name, sizeof(name)))
                        return set_error(error, error_cap,
                                         "bad trailer ref in rig");
                    idx = find_trailer_by_name(name);
                    if (idx < 0)
                        return set_error(error, error_cap,
                                         "unknown trailer in rig");
                    if (r->n_trailers < MAX_TRAILERS)
                        r->trailer_idx[r->n_trailers++] = idx;
                    skip_ws(&p);
                    if (*p == ']') break;
                    if (!match_char(&p, ','))
                        return set_error(error, error_cap,
                                         "expected ',' in trailers");
                }
            }
            if (!match_char(&p, ']'))
                return set_error(error, error_cap, "expected ']' trailers");
        }
        FIELD_NUM("cargo_mass_kg", r->cargo_mass_kg)
        else if (strcmp(k, "difficulty_stars") == 0) {
            float v;
            if (!parse_number(&p, &v))
                return set_error(error, error_cap, "bad difficulty_stars");
            r->difficulty_stars = (int)v;
        } else {
            if (!skip_value(&p))
                return set_error(error, error_cap, "bad value in rig");
        }

        skip_ws(&p);
        if (*p == '}') { p++; break; }
        if (!match_char(&p, ','))
            return set_error(error, error_cap, "expected ',' in rig");
    }
    if (r->truck_idx < 0)
        return set_error(error, error_cap, "rig missing a valid truck");
    if (r->n_trailers < 1)
        return set_error(error, error_cap, "rig needs at least one trailer");
    *pp = p;
    return 1;
}

int config_load_rigs_text(const char *json, char *error, int error_cap)
{
    const char *p = json;
    TruckSpec trucks[MAX_TRUCK_SPECS];
    TrailerSpec trailers[MAX_TRAILER_SPECS];
    RigSpec rigs[MAX_RIG_SPECS];
    int n_trucks = 0, n_trailers = 0, n_rigs = 0;

    if (!json) return set_error(error, error_cap, "no JSON text");
    if (!match_char(&p, '{'))
        return set_error(error, error_cap, "rigs.json must be an object");

    skip_ws(&p);
    if (*p != '}') {
        for (;;) {
            char k[64];
            if (!parse_string(&p, k, sizeof(k)))
                return set_error(error, error_cap, "expected top-level key");
            if (!match_char(&p, ':'))
                return set_error(error, error_cap, "expected ':'");

            if (strcmp(k, "trucks") == 0) {
                if (!match_char(&p, '['))
                    return set_error(error, error_cap, "expected '['");
                skip_ws(&p);
                if (*p != ']') {
                    for (;;) {
                        if (n_trucks >= MAX_TRUCK_SPECS)
                            return set_error(error, error_cap,
                                             "too many trucks");
                        if (!parse_truck_spec(&p, &trucks[n_trucks], error,
                                             error_cap))
                            return 0;
                        n_trucks++;
                        skip_ws(&p);
                        if (*p == ']') break;
                        if (!match_char(&p, ','))
                            return set_error(error, error_cap,
                                             "expected ',' after truck");
                    }
                }
                if (!match_char(&p, ']'))
                    return set_error(error, error_cap, "expected ']'");
            } else if (strcmp(k, "trailers") == 0) {
                if (!match_char(&p, '['))
                    return set_error(error, error_cap, "expected '['");
                skip_ws(&p);
                if (*p != ']') {
                    for (;;) {
                        if (n_trailers >= MAX_TRAILER_SPECS)
                            return set_error(error, error_cap,
                                             "too many trailers");
                        if (!parse_trailer_spec(&p, &trailers[n_trailers],
                                                error, error_cap))
                            return 0;
                        n_trailers++;
                        skip_ws(&p);
                        if (*p == ']') break;
                        if (!match_char(&p, ','))
                            return set_error(error, error_cap,
                                             "expected ',' after trailer");
                    }
                }
                if (!match_char(&p, ']'))
                    return set_error(error, error_cap, "expected ']'");
            } else if (strcmp(k, "rigs") == 0) {
                /* deferred: rigs reference trucks/trailers by name, so
                 * commit those two arrays first — see below */
                if (!skip_value(&p))
                    return set_error(error, error_cap, "bad rigs array");
            } else {
                if (!skip_value(&p))
                    return set_error(error, error_cap, "bad top-level value");
            }

            skip_ws(&p);
            if (*p == '}') { p++; break; }
            if (!match_char(&p, ','))
                return set_error(error, error_cap, "expected ',' or '}'");
        }
    } else {
        p++;
    }

    if (n_trucks < 1)
        return set_error(error, error_cap, "rigs.json needs at least one truck");
    if (n_trailers < 1)
        return set_error(error, error_cap,
                         "rigs.json needs at least one trailer");

    /* commit trucks/trailers now so a second pass can resolve rig
     * references by name */
    memcpy(truck_specs, trucks, sizeof(trucks[0]) * (size_t)n_trucks);
    truck_spec_count = n_trucks;
    memcpy(trailer_specs, trailers, sizeof(trailers[0]) * (size_t)n_trailers);
    trailer_spec_count = n_trailers;

    p = json;
    match_char(&p, '{');
    skip_ws(&p);
    if (*p != '}') {
        for (;;) {
            char k[64];
            parse_string(&p, k, sizeof(k));
            match_char(&p, ':');
            if (strcmp(k, "rigs") == 0) {
                if (!match_char(&p, '['))
                    return set_error(error, error_cap, "expected '['");
                skip_ws(&p);
                if (*p != ']') {
                    for (;;) {
                        if (n_rigs >= MAX_RIG_SPECS)
                            return set_error(error, error_cap,
                                             "too many rigs");
                        if (!parse_rig_spec(&p, &rigs[n_rigs], error,
                                           error_cap))
                            return 0;
                        n_rigs++;
                        skip_ws(&p);
                        if (*p == ']') break;
                        if (!match_char(&p, ','))
                            return set_error(error, error_cap,
                                             "expected ',' after rig");
                    }
                }
                match_char(&p, ']');
            } else {
                skip_value(&p);
            }
            skip_ws(&p);
            if (*p == '}') break;
            match_char(&p, ',');
        }
    }

    if (n_rigs < 1)
        return set_error(error, error_cap, "rigs.json needs at least one rig");

    memcpy(rig_specs, rigs, sizeof(rigs[0]) * (size_t)n_rigs);
    rig_spec_count = n_rigs;
    return 1;
}

static int parse_haul_settings(const char **pp, HaulSettings *s, char *error,
                               int error_cap)
{
    const char *p = *pp;
    if (!match_char(&p, '{'))
        return set_error(error, error_cap, "settings.json must be an object");
    skip_ws(&p);
    if (*p == '}') { p++; *pp = p; return 1; }
    for (;;) {
        char k[64];
        if (!parse_string(&p, k, sizeof(k)))
            return set_error(error, error_cap, "expected key in settings");
        if (!match_char(&p, ':'))
            return set_error(error, error_cap, "expected ':' in settings");

        if (0) { }
        FIELD_NUM("steer_rate_on_dps", s->steer_rate_on_dps)
        FIELD_NUM("steer_rate_center_dps", s->steer_rate_center_dps)
        FIELD_NUM("steer_speed_fade", s->steer_speed_fade)
        FIELD_NUM("rolling_resistance", s->rolling_resistance)
        FIELD_NUM("drivetrain_efficiency", s->drivetrain_efficiency)
        FIELD_NUM("shift_seconds", s->shift_seconds)
        FIELD_NUM("brake_mass_floor", s->brake_mass_floor)
        FIELD_NUM("jackknife_warn_frac", s->jackknife_warn_frac)
        FIELD_NUM("jackknife_danger_frac", s->jackknife_danger_frac)
        FIELD_NUM("slosh_response", s->slosh_response)
        FIELD_NUM("slosh_yaw_feedback", s->slosh_yaw_feedback)
        FIELD_NUM("cam_distance_base_m", s->cam_distance_base_m)
        FIELD_NUM("cam_distance_per_rig_length", s->cam_distance_per_rig_length)
        FIELD_NUM("cam_height_m", s->cam_height_m)
        FIELD_NUM("cam_min_height_m", s->cam_min_height_m)
        FIELD_NUM("cam_look_height_m", s->cam_look_height_m)
        FIELD_NUM("cam_follow_smoothing", s->cam_follow_smoothing)
        FIELD_NUM("cam_look_smoothing", s->cam_look_smoothing)
        FIELD_NUM("cam_reverse_deadzone_mps", s->cam_reverse_deadzone_mps)
        FIELD_NUM("cam_reverse_full_mps", s->cam_reverse_full_mps)
        FIELD_NUM("cam_reverse_orbit_rate_dps", s->cam_reverse_orbit_rate_dps)
        FIELD_NUM("cam_reverse_smoothing", s->cam_reverse_smoothing)
        FIELD_NUM("cam_reverse_tail_bias", s->cam_reverse_tail_bias)
        FIELD_NUM("cam_corner_lean", s->cam_corner_lean)
        else {
            if (!skip_value(&p))
                return set_error(error, error_cap, "bad value in settings");
        }

        skip_ws(&p);
        if (*p == '}') { p++; break; }
        if (!match_char(&p, ','))
            return set_error(error, error_cap, "expected ',' in settings");
    }
    *pp = p;
    return 1;
}

int config_load_settings_text(HaulSettings *settings, const char *json,
                              char *error, int error_cap)
{
    HaulSettings tmp;
    const char *p = json;
    if (!json) return set_error(error, error_cap, "no JSON text");
    haul_settings_defaults(&tmp);
    if (!parse_haul_settings(&p, &tmp, error, error_cap))
        return 0;
    *settings = tmp;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Controls                                                            */
/* ------------------------------------------------------------------ */

static const char *action_names[CONTROL_ACTION_COUNT] = {
    "steer_left", "steer_right", "accel", "reverse", "brake",
    "parking_brake", "horn", "gear_up", "gear_down", "menu", "confirm",
    "back"
};

const char *control_action_name(int action)
{
    if (action < 0 || action >= CONTROL_ACTION_COUNT) return "?";
    return action_names[action];
}

static int action_from_name(const char *name)
{
    int i;
    for (i = 0; i < CONTROL_ACTION_COUNT; i++)
        if (strcmp(action_names[i], name) == 0) return i;
    return -1;
}

static const struct { const char *name; int code; } key_names[] = {
    { "SPACE", GAME_KEY_SPACE }, { "ENTER", GAME_KEY_ENTER },
    { "ESCAPE", GAME_KEY_ESCAPE }, { "LEFT", GAME_KEY_LEFT },
    { "RIGHT", GAME_KEY_RIGHT }, { "UP", GAME_KEY_UP },
    { "DOWN", GAME_KEY_DOWN }
};
#define N_KEY_NAMES (int)(sizeof(key_names) / sizeof(key_names[0]))

const char *control_key_name(int key_code)
{
    int i;
    for (i = 0; i < N_KEY_NAMES; i++)
        if (key_names[i].code == key_code) return key_names[i].name;
    return "?";
}

static int key_from_name(const char *name, int *out)
{
    int i;
    if (strlen(name) == 1 && isalpha((unsigned char)name[0])) {
        *out = toupper((unsigned char)name[0]);
        return 1;
    }
    for (i = 0; i < N_KEY_NAMES; i++)
        if (strcmp(key_names[i].name, name) == 0) {
            *out = key_names[i].code;
            return 1;
        }
    return 0;
}

static const struct { const char *name; unsigned int mask; }
gc_names[] = {
    { "a", GC_INPUT_A }, { "b", GC_INPUT_B }, { "x", GC_INPUT_X },
    { "y", GC_INPUT_Y }, { "z", GC_INPUT_Z }, { "l", GC_INPUT_L },
    { "r", GC_INPUT_R }, { "start", GC_INPUT_START },
    { "dpad_left", GC_INPUT_DPAD_LEFT }, { "dpad_right", GC_INPUT_DPAD_RIGHT },
    { "dpad_up", GC_INPUT_DPAD_UP }, { "dpad_down", GC_INPUT_DPAD_DOWN }
};
#define N_GC_NAMES (int)(sizeof(gc_names) / sizeof(gc_names[0]))

const char *control_gamecube_name(unsigned int mask)
{
    int i;
    for (i = 0; i < N_GC_NAMES; i++)
        if (gc_names[i].mask == mask) return gc_names[i].name;
    return "?";
}

static int gc_from_name(const char *name, unsigned int *out)
{
    int i;
    for (i = 0; i < N_GC_NAMES; i++)
        if (strcmp(gc_names[i].name, name) == 0) {
            *out = gc_names[i].mask;
            return 1;
        }
    return 0;
}

void control_config_defaults(ControlConfig *c)
{
    memset(c, 0, sizeof(*c));
    c->show_input_overlay = 1;

    c->keyboard[CONTROL_STEER_LEFT][0]  = 'A';
    c->keyboard[CONTROL_STEER_LEFT][1]  = GAME_KEY_LEFT;
    c->keyboard[CONTROL_STEER_RIGHT][0] = 'D';
    c->keyboard[CONTROL_STEER_RIGHT][1] = GAME_KEY_RIGHT;
    c->keyboard[CONTROL_ACCEL][0]       = 'W';
    c->keyboard[CONTROL_ACCEL][1]       = GAME_KEY_UP;
    c->keyboard[CONTROL_REVERSE][0]     = 'S';
    c->keyboard[CONTROL_REVERSE][1]     = GAME_KEY_DOWN;
    c->keyboard[CONTROL_BRAKE][0]       = GAME_KEY_SPACE;
    c->keyboard[CONTROL_PARKING_BRAKE][0] = 'P';
    c->keyboard[CONTROL_HORN][0]        = 'H';
    c->keyboard[CONTROL_GEAR_UP][0]     = 'E';
    c->keyboard[CONTROL_GEAR_DOWN][0]   = 'Q';
    c->keyboard[CONTROL_MENU][0]        = GAME_KEY_ESCAPE;
    c->keyboard[CONTROL_CONFIRM][0]     = GAME_KEY_ENTER;
    c->keyboard[CONTROL_BACK][0]        = GAME_KEY_ESCAPE;

    c->gamecube[CONTROL_STEER_LEFT]    = GC_INPUT_DPAD_LEFT;
    c->gamecube[CONTROL_STEER_RIGHT]   = GC_INPUT_DPAD_RIGHT;
    c->gamecube[CONTROL_ACCEL]         = GC_INPUT_A;
    c->gamecube[CONTROL_REVERSE]       = GC_INPUT_B;
    c->gamecube[CONTROL_BRAKE]         = GC_INPUT_X;
    c->gamecube[CONTROL_PARKING_BRAKE] = GC_INPUT_Y;
    c->gamecube[CONTROL_HORN]          = GC_INPUT_Z;
    c->gamecube[CONTROL_GEAR_UP]       = GC_INPUT_R;
    c->gamecube[CONTROL_GEAR_DOWN]     = GC_INPUT_L;
    c->gamecube[CONTROL_MENU]          = GC_INPUT_START;
    c->gamecube[CONTROL_CONFIRM]       = GC_INPUT_A;
    c->gamecube[CONTROL_BACK]          = GC_INPUT_B;

    strcpy(c->xbox_label[CONTROL_STEER_LEFT],  "LEFT STICK");
    strcpy(c->xbox_label[CONTROL_STEER_RIGHT], "LEFT STICK");
    strcpy(c->xbox_label[CONTROL_ACCEL],       "A");
    strcpy(c->xbox_label[CONTROL_REVERSE],     "B");
    strcpy(c->xbox_label[CONTROL_BRAKE],       "X");
    strcpy(c->xbox_label[CONTROL_PARKING_BRAKE], "Y");
    strcpy(c->xbox_label[CONTROL_HORN],        "LB");
    strcpy(c->xbox_label[CONTROL_GEAR_UP],     "RB");
    strcpy(c->xbox_label[CONTROL_GEAR_DOWN],   "LT");
    strcpy(c->xbox_label[CONTROL_MENU],        "START");
    strcpy(c->xbox_label[CONTROL_CONFIRM],     "A");
    strcpy(c->xbox_label[CONTROL_BACK],        "B");
}

static int parse_controls(const char **pp, ControlConfig *c, char *error,
                          int error_cap)
{
    const char *p = *pp;
    if (!match_char(&p, '{'))
        return set_error(error, error_cap, "controls.json must be an object");
    skip_ws(&p);
    if (*p == '}') { p++; *pp = p; return 1; }
    for (;;) {
        char k[64];
        if (!parse_string(&p, k, sizeof(k)))
            return set_error(error, error_cap, "expected key in controls");
        if (!match_char(&p, ':'))
            return set_error(error, error_cap, "expected ':' in controls");

        if (strcmp(k, "show_input_overlay") == 0) {
            if (!parse_bool(&p, &c->show_input_overlay))
                return set_error(error, error_cap, "bad show_input_overlay");
        } else if (strcmp(k, "actions") == 0) {
            if (!match_char(&p, '{'))
                return set_error(error, error_cap, "expected '{' actions");
            skip_ws(&p);
            if (*p != '}') {
                for (;;) {
                    char action_name[32];
                    int action;
                    if (!parse_string(&p, action_name, sizeof(action_name)))
                        return set_error(error, error_cap, "bad action key");
                    if (!match_char(&p, ':'))
                        return set_error(error, error_cap, "expected ':'");
                    action = action_from_name(action_name);

                    if (!match_char(&p, '{'))
                        return set_error(error, error_cap,
                                         "expected '{' for an action");
                    skip_ws(&p);
                    if (*p != '}') {
                        for (;;) {
                            char fk[32];
                            if (!parse_string(&p, fk, sizeof(fk)))
                                return set_error(error, error_cap,
                                                 "bad action field key");
                            if (!match_char(&p, ':'))
                                return set_error(error, error_cap,
                                                 "expected ':'");
                            if (strcmp(fk, "keyboard") == 0) {
                                if (!match_char(&p, '['))
                                    return set_error(error, error_cap,
                                                     "expected '['");
                                skip_ws(&p);
                                if (*p != ']') {
                                    int n = 0;
                                    for (;;) {
                                        char name[16]; int code;
                                        if (!parse_string(&p, name,
                                                          sizeof(name)))
                                            return set_error(error,
                                                error_cap, "bad key name");
                                        if (key_from_name(name, &code) &&
                                           action >= 0 &&
                                           n < CONTROL_MAX_BINDS)
                                            c->keyboard[action][n++] = code;
                                        skip_ws(&p);
                                        if (*p == ']') break;
                                        if (!match_char(&p, ','))
                                            return set_error(error,
                                                error_cap, "expected ','");
                                    }
                                }
                                if (!match_char(&p, ']'))
                                    return set_error(error, error_cap,
                                                     "expected ']'");
                            } else if (strcmp(fk, "gamecube") == 0) {
                                unsigned int bits = 0;
                                if (!match_char(&p, '['))
                                    return set_error(error, error_cap,
                                                     "expected '['");
                                skip_ws(&p);
                                if (*p != ']') {
                                    for (;;) {
                                        char name[16]; unsigned int m;
                                        if (!parse_string(&p, name,
                                                          sizeof(name)))
                                            return set_error(error,
                                                error_cap, "bad gc name");
                                        if (gc_from_name(name, &m))
                                            bits |= m;
                                        skip_ws(&p);
                                        if (*p == ']') break;
                                        if (!match_char(&p, ','))
                                            return set_error(error,
                                                error_cap, "expected ','");
                                    }
                                }
                                if (!match_char(&p, ']'))
                                    return set_error(error, error_cap,
                                                     "expected ']'");
                                if (action >= 0) c->gamecube[action] = bits;
                            } else if (strcmp(fk, "xbox_label") == 0) {
                                char label[CONTROL_LABEL_LEN];
                                if (!parse_string(&p, label, sizeof(label)))
                                    return set_error(error, error_cap,
                                                     "bad xbox_label");
                                if (action >= 0)
                                    strcpy(c->xbox_label[action], label);
                            } else {
                                if (!skip_value(&p))
                                    return set_error(error, error_cap,
                                                     "bad action field");
                            }
                            skip_ws(&p);
                            if (*p == '}') { p++; break; }
                            if (!match_char(&p, ','))
                                return set_error(error, error_cap,
                                                 "expected ','");
                        }
                    } else {
                        p++;
                    }

                    skip_ws(&p);
                    if (*p == '}') break;
                    if (!match_char(&p, ','))
                        return set_error(error, error_cap, "expected ','");
                }
            }
            if (!match_char(&p, '}'))
                return set_error(error, error_cap, "expected '}' actions");
        } else {
            if (!skip_value(&p))
                return set_error(error, error_cap, "bad value in controls");
        }

        skip_ws(&p);
        if (*p == '}') { p++; break; }
        if (!match_char(&p, ','))
            return set_error(error, error_cap, "expected ',' in controls");
    }
    *pp = p;
    return 1;
}

int config_load_controls_text(ControlConfig *controls, const char *json,
                              char *error, int error_cap)
{
    ControlConfig tmp;
    const char *p = json;
    if (!json) return set_error(error, error_cap, "no JSON text");
    control_config_defaults(&tmp);
    if (!parse_controls(&p, &tmp, error, error_cap))
        return 0;
    *controls = tmp;
    return 1;
}

/* ------------------------------------------------------------------ */
/* File loaders                                                        */
/* ------------------------------------------------------------------ */

#define JSON_MAX_BYTES (64 * 1024)

static int read_whole_file(const char *path, char *error, int error_cap,
                           char **out)
{
    FILE *f = fopen(path, "rb");
    long len;
    char *buf;

    *out = NULL;
    if (!f) return set_error(error, error_cap, "could not open file");
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 ||
       fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return set_error(error, error_cap, "could not size file");
    }
    if (len > JSON_MAX_BYTES) {
        fclose(f);
        return set_error(error, error_cap, "file too large");
    }
    buf = (char *)malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return set_error(error, error_cap, "out of memory");
    }
    if (len > 0 && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(buf);
        return set_error(error, error_cap, "short read");
    }
    buf[len] = '\0';
    fclose(f);
    *out = buf;
    return 1;
}

int config_load_rigs_file(const char *path, char *error, int error_cap)
{
    char *buf;
    int ok;
    if (!read_whole_file(path, error, error_cap, &buf)) return 0;
    ok = config_load_rigs_text(buf, error, error_cap);
    free(buf);
    return ok;
}

int config_load_settings_file(HaulSettings *settings, const char *path,
                              char *error, int error_cap)
{
    char *buf;
    int ok;
    if (!read_whole_file(path, error, error_cap, &buf)) return 0;
    ok = config_load_settings_text(settings, buf, error, error_cap);
    free(buf);
    return ok;
}

int config_load_controls_file(ControlConfig *controls, const char *path,
                              char *error, int error_cap)
{
    char *buf;
    int ok;
    if (!read_whole_file(path, error, error_cap, &buf)) return 0;
    ok = config_load_controls_text(controls, buf, error, error_cap);
    free(buf);
    return ok;
}
