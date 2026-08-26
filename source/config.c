/* WiiKart JSON configuration: small, dependency-free, C99. */
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#define JSON_MAX_BYTES  (64 * 1024)
#define JSON_MAX_TOKENS 2048

typedef enum {
    JT_UNDEFINED = 0,
    JT_OBJECT,
    JT_ARRAY,
    JT_STRING,
    JT_PRIMITIVE
} JsonType;

typedef struct {
    JsonType type;
    int start;
    int end;
    int parent;
} JsonToken;

static void json_skip_space(const char **at)
{
    while (isspace((unsigned char)**at))
        (*at)++;
}

static int json_validate_string(const char **at)
{
    const char *p = *at;
    if (*p++ != '"')
        return 0;
    while (*p && *p != '"') {
        if ((unsigned char)*p < 0x20)
            return 0;
        if (*p++ == '\\') {
            int i;
            char escape = *p++;
            if (!escape || !strchr("\"\\/bfnrtu", escape))
                return 0;
            if (escape == 'u') {
                for (i = 0; i < 4; i++)
                    if (!isxdigit((unsigned char)*p++))
                        return 0;
            }
        }
    }
    if (*p != '"')
        return 0;
    *at = p + 1;
    return 1;
}

static int json_validate_number(const char **at)
{
    const char *p = *at;
    if (*p == '-') p++;
    if (*p == '0') {
        p++;
    } else {
        if (*p < '1' || *p > '9')
            return 0;
        while (isdigit((unsigned char)*p)) p++;
    }
    if (*p == '.') {
        p++;
        if (!isdigit((unsigned char)*p)) return 0;
        while (isdigit((unsigned char)*p)) p++;
    }
    if (*p == 'e' || *p == 'E') {
        p++;
        if (*p == '+' || *p == '-') p++;
        if (!isdigit((unsigned char)*p)) return 0;
        while (isdigit((unsigned char)*p)) p++;
    }
    *at = p;
    return 1;
}

static int json_validate_value(const char **at, int depth)
{
    const char *p = *at;
    if (depth > 64)
        return 0;
    json_skip_space(&p);
    if (*p == '{') {
        p++;
        json_skip_space(&p);
        if (*p == '}') {
            *at = p + 1;
            return 1;
        }
        for (;;) {
            if (!json_validate_string(&p)) return 0;
            json_skip_space(&p);
            if (*p++ != ':') return 0;
            if (!json_validate_value(&p, depth + 1)) return 0;
            json_skip_space(&p);
            if (*p == '}') {
                *at = p + 1;
                return 1;
            }
            if (*p++ != ',') return 0;
            json_skip_space(&p);
        }
    }
    if (*p == '[') {
        p++;
        json_skip_space(&p);
        if (*p == ']') {
            *at = p + 1;
            return 1;
        }
        for (;;) {
            if (!json_validate_value(&p, depth + 1)) return 0;
            json_skip_space(&p);
            if (*p == ']') {
                *at = p + 1;
                return 1;
            }
            if (*p++ != ',') return 0;
            json_skip_space(&p);
        }
    }
    if (*p == '"') {
        if (!json_validate_string(&p)) return 0;
        *at = p;
        return 1;
    }
    if (strncmp(p, "true", 4) == 0) {
        *at = p + 4;
        return 1;
    }
    if (strncmp(p, "false", 5) == 0) {
        *at = p + 5;
        return 1;
    }
    if (strncmp(p, "null", 4) == 0) {
        *at = p + 4;
        return 1;
    }
    if (!json_validate_number(&p))
        return 0;
    *at = p;
    return 1;
}

static int json_syntax_valid(const char *json)
{
    const char *at = json;
    if (!json || !json_validate_value(&at, 0))
        return 0;
    json_skip_space(&at);
    return *at == '\0';
}

static int set_error(char *error, int cap, const char *message)
{
    if (error && cap > 0) {
        snprintf(error, (size_t)cap, "%s", message);
        error[cap - 1] = '\0';
    }
    return 0;
}

static int new_token(JsonToken *tokens, int cap, int *count,
                     JsonType type, int start, int parent)
{
    JsonToken *t;
    if (*count >= cap)
        return -1;
    t = &tokens[*count];
    t->type = type;
    t->start = start;
    t->end = -1;
    t->parent = parent;
    return (*count)++;
}

static int json_tokenize(const char *json, JsonToken *tokens, int cap,
                         char *error, int error_cap)
{
    int count = 0;
    int parent = -1;
    int i;

    if (!json)
        return set_error(error, error_cap, "EMPTY JSON");
    if (!json_syntax_valid(json))
        return set_error(error, error_cap, "MALFORMED JSON");

    for (i = 0; json[i]; i++) {
        unsigned char c = (unsigned char)json[i];
        if (isspace(c) || c == ':' || c == ',')
            continue;

        if (c == '{' || c == '[') {
            int idx = new_token(tokens, cap, &count,
                                c == '{' ? JT_OBJECT : JT_ARRAY,
                                i, parent);
            if (idx < 0)
                return set_error(error, error_cap, "JSON TOO COMPLEX");
            parent = idx;
            continue;
        }

        if (c == '}' || c == ']') {
            JsonType want = c == '}' ? JT_OBJECT : JT_ARRAY;
            if (parent < 0 || tokens[parent].type != want)
                return set_error(error, error_cap, "MISMATCHED JSON");
            tokens[parent].end = i + 1;
            parent = tokens[parent].parent;
            continue;
        }

        if (c == '"') {
            int start = ++i;
            int idx;
            while (json[i] && json[i] != '"') {
                if ((unsigned char)json[i] < 0x20)
                    return set_error(error, error_cap, "BAD JSON STRING");
                if (json[i] == '\\') {
                    i++;
                    if (!json[i])
                        return set_error(error, error_cap,
                                         "BAD JSON ESCAPE");
                }
                i++;
            }
            if (!json[i])
                return set_error(error, error_cap, "OPEN JSON STRING");
            idx = new_token(tokens, cap, &count, JT_STRING, start, parent);
            if (idx < 0)
                return set_error(error, error_cap, "JSON TOO COMPLEX");
            tokens[idx].end = i;
            continue;
        }

        {
            int start = i;
            int idx;
            while (json[i] && !isspace((unsigned char)json[i]) &&
                   json[i] != ',' && json[i] != ']' && json[i] != '}')
                i++;
            if (i == start)
                return set_error(error, error_cap, "BAD JSON VALUE");
            idx = new_token(tokens, cap, &count, JT_PRIMITIVE, start, parent);
            if (idx < 0)
                return set_error(error, error_cap, "JSON TOO COMPLEX");
            tokens[idx].end = i;
            i--;
        }
    }

    if (parent >= 0)
        return set_error(error, error_cap, "UNCLOSED JSON");
    if (count < 1 || tokens[0].end < 0)
        return set_error(error, error_cap, "NO JSON DOCUMENT");
    return count;
}

static int token_equals(const char *json, const JsonToken *t,
                        const char *text)
{
    int n = t->end - t->start;
    return t->type == JT_STRING && n == (int)strlen(text) &&
           strncmp(json + t->start, text, (size_t)n) == 0;
}

static int object_get(const char *json, const JsonToken *tokens, int count,
                      int object, const char *key)
{
    int i, expecting_key = 1;
    if (object < 0 || object >= count || tokens[object].type != JT_OBJECT)
        return -1;
    for (i = object + 1; i + 1 < count &&
         tokens[i].start < tokens[object].end; i++) {
        if (tokens[i].parent != object)
            continue;
        if (expecting_key && tokens[i].type == JT_STRING &&
            token_equals(json, &tokens[i], key) && i + 1 < count &&
            tokens[i + 1].parent == object)
            return i + 1;
        /* Direct children of a valid JSON object alternate key/value.
         * Without this bit, a string value that happened to equal a key
         * could be mistaken for the following pair's key. */
        expecting_key = !expecting_key;
    }
    return -1;
}

static int array_length(const JsonToken *tokens, int count, int array)
{
    int i, n = 0;
    if (array < 0 || array >= count || tokens[array].type != JT_ARRAY)
        return -1;
    for (i = array + 1; i < count && tokens[i].start < tokens[array].end; i++)
        if (tokens[i].parent == array)
            n++;
    return n;
}

static int array_item(const JsonToken *tokens, int count, int array, int at)
{
    int i, n = 0;
    if (array < 0 || array >= count || tokens[array].type != JT_ARRAY)
        return -1;
    for (i = array + 1; i < count && tokens[i].start < tokens[array].end; i++) {
        if (tokens[i].parent != array)
            continue;
        if (n++ == at)
            return i;
    }
    return -1;
}

static int token_string(const char *json, const JsonToken *t,
                        char *out, int cap)
{
    int i, n = 0;
    if (t->type != JT_STRING || cap < 1)
        return 0;
    for (i = t->start; i < t->end; i++) {
        char c = json[i];
        if (c == '\\' && i + 1 < t->end) {
            char e = json[++i];
            if (e == 'n') c = '\n';
            else if (e == 'r') c = '\r';
            else if (e == 't') c = '\t';
            else if (e == '"' || e == '\\' || e == '/') c = e;
            else return 0;       /* unicode escapes are unnecessary here */
        }
        if (n + 1 >= cap)
            return 0;
        out[n++] = c;
    }
    out[n] = '\0';
    return 1;
}

static int token_float(const char *json, const JsonToken *t, float *out)
{
    char buf[64];
    char *end;
    int n = t->end - t->start;
    float value;
    if (t->type != JT_PRIMITIVE || n <= 0 || n >= (int)sizeof(buf))
        return 0;
    memcpy(buf, json + t->start, (size_t)n);
    buf[n] = '\0';
    errno = 0;
    value = strtof(buf, &end);
    if (errno || end == buf || *end != '\0' || !isfinite(value))
        return 0;
    *out = value;
    return 1;
}

static int token_int(const char *json, const JsonToken *t, int *out)
{
    char buf[48];
    char *end;
    long value;
    int n = t->end - t->start;
    if (t->type != JT_PRIMITIVE || n <= 0 || n >= (int)sizeof(buf))
        return 0;
    memcpy(buf, json + t->start, (size_t)n);
    buf[n] = '\0';
    errno = 0;
    value = strtol(buf, &end, 10);
    if (errno || end == buf || *end != '\0' || value < -2147483647L ||
        value > 2147483647L)
        return 0;
    *out = (int)value;
    return 1;
}

static int token_bool(const char *json, const JsonToken *t, int *out)
{
    int n = t->end - t->start;
    if (t->type != JT_PRIMITIVE)
        return 0;
    if (n == 4 && strncmp(json + t->start, "true", 4) == 0) {
        *out = 1;
        return 1;
    }
    if (n == 5 && strncmp(json + t->start, "false", 5) == 0) {
        *out = 0;
        return 1;
    }
    return 0;
}

static int optional_float(const char *json, const JsonToken *tokens,
                          int count, int object, const char *key, float *dst,
                          char *error, int error_cap)
{
    int at = object_get(json, tokens, count, object, key);
    if (at < 0) return 1;
    if (!token_float(json, &tokens[at], dst))
        return set_error(error, error_cap, "SETTING MUST BE NUMBER");
    return 1;
}

static int optional_int(const char *json, const JsonToken *tokens,
                        int count, int object, const char *key, int *dst,
                        char *error, int error_cap)
{
    int at = object_get(json, tokens, count, object, key);
    if (at < 0) return 1;
    if (!token_int(json, &tokens[at], dst))
        return set_error(error, error_cap, "SETTING MUST BE INTEGER");
    return 1;
}

static int load_text_file(const char *path, char **text,
                          char *error, int error_cap)
{
    FILE *f;
    long size;
    char *buf;
    size_t got;

    *text = NULL;
    f = fopen(path, "rb");
    if (!f)
        return set_error(error, error_cap, "CONFIG NOT FOUND");
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return set_error(error, error_cap, "CONFIG SEEK FAILED");
    }
    size = ftell(f);
    if (size < 1 || size > JSON_MAX_BYTES) {
        fclose(f);
        return set_error(error, error_cap, "CONFIG SIZE INVALID");
    }
    rewind(f);
    buf = (char *)malloc((size_t)size + 1u);
    if (!buf) {
        fclose(f);
        return set_error(error, error_cap, "NO CONFIG MEMORY");
    }
    got = fread(buf, 1u, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size) {
        free(buf);
        return set_error(error, error_cap, "CONFIG READ FAILED");
    }
    buf[size] = '\0';
    *text = buf;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Cars                                                               */
/* ------------------------------------------------------------------ */

static int required_float(const char *json, const JsonToken *tokens,
                          int count, int object, const char *key, float *dst,
                          char *error, int error_cap)
{
    int at = object_get(json, tokens, count, object, key);
    if (at < 0 || !token_float(json, &tokens[at], dst))
        return set_error(error, error_cap, "CAR FIELD MISSING");
    return 1;
}

/*
 * Optional per-car drivetrain: which axle(s) put power down. A car with
 * no "drivetrain" block is rear-wheel drive. "type" is "fwd", "rwd" or
 * "awd"; "front_bias" (0..1, AWD only) says how much of that drive goes
 * to the front axle, 0.5 being an even split.
 */
static int read_drivetrain(const char *json, const JsonToken *tokens,
                           int count, int obj, KartSpec *s,
                           char *error, int error_cap)
{
    int at = object_get(json, tokens, count, obj, "drivetrain");
    char type[8];
    int type_at;

    s->drivetrain = DRIVETRAIN_RWD;
    s->awd_front_bias = 0.5f;
    if (at < 0)
        return 1;
    if (tokens[at].type != JT_OBJECT)
        return set_error(error, error_cap, "DRIVETRAIN NEEDS OBJECT");

    type_at = object_get(json, tokens, count, at, "type");
    if (type_at < 0 || !token_string(json, &tokens[type_at], type,
                                     (int)sizeof(type)))
        return set_error(error, error_cap, "DRIVETRAIN NEEDS A TYPE");
    if (strcmp(type, "fwd") == 0)
        s->drivetrain = DRIVETRAIN_FWD;
    else if (strcmp(type, "rwd") == 0)
        s->drivetrain = DRIVETRAIN_RWD;
    else if (strcmp(type, "awd") == 0)
        s->drivetrain = DRIVETRAIN_AWD;
    else
        return set_error(error, error_cap, "UNKNOWN DRIVETRAIN TYPE");

    if (!optional_float(json, tokens, count, at, "front_bias",
                        &s->awd_front_bias, error, error_cap))
        return 0;
    return 1;
}

/*
 * Engine character: "nominal_rpm" is the RPM (shared by every gear —
 * see the KartSpec comment in game.h) where the engine makes the most
 * power, and "aspiration" is "na" (default), "turbo", "supercharged" or
 * "twin_turbo". A car that mentions neither keeps the defaults.
 */
static int read_gearing(const char *json, const JsonToken *tokens,
                        int count, int obj, KartSpec *s,
                        char *error, int error_cap)
{
    int at;

    kart_spec_default_gearing(s);
    if (!optional_float(json, tokens, count, obj, "nominal_rpm",
                        &s->nominal_rpm, error, error_cap))
        return 0;

    at = object_get(json, tokens, count, obj, "aspiration");
    s->aspiration = ASPIRATION_NA;
    if (at >= 0) {
        char aspiration[16];
        if (!token_string(json, &tokens[at], aspiration,
                          (int)sizeof(aspiration)))
            return set_error(error, error_cap, "BAD ASPIRATION");
        if (strcmp(aspiration, "na") == 0)
            s->aspiration = ASPIRATION_NA;
        else if (strcmp(aspiration, "turbo") == 0)
            s->aspiration = ASPIRATION_TURBO;
        else if (strcmp(aspiration, "supercharged") == 0)
            s->aspiration = ASPIRATION_SUPERCHARGED;
        else if (strcmp(aspiration, "twin_turbo") == 0)
            s->aspiration = ASPIRATION_TWIN_TURBO;
        else
            return set_error(error, error_cap, "UNKNOWN ASPIRATION");
    }
    return 1;
}

int config_load_cars_text(const char *json, char *error, int error_cap)
{
    JsonToken *tokens;
    KartSpec candidate[MAX_KART_SPECS];
    int count, cars, n, i;

    tokens = (JsonToken *)malloc(sizeof(*tokens) * JSON_MAX_TOKENS);
    if (!tokens) return set_error(error, error_cap, "NO JSON MEMORY");
    count = json_tokenize(json, tokens, JSON_MAX_TOKENS, error, error_cap);
    if (count <= 0) { free(tokens); return 0; }
    cars = tokens[0].type == JT_ARRAY
               ? 0 : object_get(json, tokens, count, 0, "cars");
    n = array_length(tokens, count, cars);
    if (n < 1 || n > MAX_KART_SPECS) {
        free(tokens);
        return set_error(error, error_cap, "BAD CAR COUNT");
    }
    memset(candidate, 0, sizeof(candidate));

    for (i = 0; i < n; i++) {
        KartSpec *s = &candidate[i];
        int obj = array_item(tokens, count, cars, i);
        int name_at, gears_at, ng, g;
        if (obj < 0 || tokens[obj].type != JT_OBJECT) {
            free(tokens);
            return set_error(error, error_cap, "CAR MUST BE OBJECT");
        }
        name_at = object_get(json, tokens, count, obj, "name");
        if (name_at < 0 || !token_string(json, &tokens[name_at], s->name,
                                         (int)sizeof(s->name))) {
            free(tokens);
            return set_error(error, error_cap, "BAD CAR NAME");
        }
        for (g = 0; s->name[g]; g++)
            s->name[g] = (char)toupper((unsigned char)s->name[g]);
        if (!required_float(json, tokens, count, obj, "mass_kg",
                            &s->mass_kg, error, error_cap) ||
            !required_float(json, tokens, count, obj, "power_hp",
                            &s->power_hp, error, error_cap) ||
            /* kept for backward compatibility and as a documented
             * estimate, but kart_step no longer reads this for the
             * actual physics — braking is traction-limited (mu_trac)
             * now, the same as everything else a tire does, so there is
             * no free per-car dial to shorten stopping distance with */
            !required_float(json, tokens, count, obj,
                            "brake_distance_100_kph_m", &s->brake_dist_100,
                            error, error_cap) ||
            !required_float(json, tokens, count, obj, "lateral_grip_g",
                            &s->lat_g, error, error_cap) ||
            !required_float(json, tokens, count, obj, "drag_area_m2",
                            &s->cd_a, error, error_cap) ||
            !required_float(json, tokens, count, obj, "wheelbase_m",
                            &s->wheelbase, error, error_cap) ||
            !required_float(json, tokens, count, obj, "offroad_grip",
                            &s->offroad_grip, error, error_cap)) {
            free(tokens);
            return 0;
        }
        gears_at = object_get(json, tokens, count, obj,
                              "gear_top_speeds_kph");
        ng = array_length(tokens, count, gears_at);
        if (ng < 1 || ng > MAX_GEARS) {
            free(tokens);
            return set_error(error, error_cap, "BAD GEAR COUNT");
        }
        s->n_gears = ng;
        for (g = 0; g < ng; g++) {
            float kph;
            int at = array_item(tokens, count, gears_at, g);
            if (at < 0 || !token_float(json, &tokens[at], &kph)) {
                free(tokens);
                return set_error(error, error_cap, "BAD GEAR SPEED");
            }
            s->gear_top[g] = kph / 3.6f;
        }
        if (!read_gearing(json, tokens, count, obj, s, error,
                          error_cap)) {
            free(tokens);
            return 0;
        }
        if (!read_drivetrain(json, tokens, count, obj, s, error,
                             error_cap)) {
            free(tokens);
            return 0;
        }
        if (!kart_spec_validate(s, error, error_cap)) {
            free(tokens);
            return 0;
        }
        for (g = 0; g < i; g++) {
            if (strcmp(candidate[g].name, s->name) == 0) {
                free(tokens);
                return set_error(error, error_cap, "DUPLICATE CAR NAME");
            }
        }
    }

    memcpy(kart_specs, candidate, sizeof(candidate));
    kart_spec_count = n;
    free(tokens);
    if (error && error_cap > 0) error[0] = '\0';
    return 1;
}

int config_load_cars_file(const char *path, char *error, int error_cap)
{
    char *text;
    int ok;
    if (!load_text_file(path, &text, error, error_cap)) return 0;
    ok = config_load_cars_text(text, error, error_cap);
    free(text);
    return ok;
}

/*
 * The save half of the pair above: serialize a kart spec array back
 * into a cars.json document, field for field, so config_load_cars_text
 * reads back exactly what was meant, not just something close to it.
 * nominal_rpm and aspiration are always written out explicitly (never
 * omitted to fall back on the compiled default) because some shipped
 * cars — FORMULA, TRUCK, MUSCLE — are deliberately tuned away from
 * that default and a round trip must not quietly flatten them back to
 * it. Returns the number of bytes written, or 0 if the roster didn't
 * fit in buf_cap.
 */
int config_write_cars_text(const KartSpec *specs, int count,
                           char *buf, int buf_cap)
{
    int i, g, n, pos = 0;

    if (count < 1 || count > MAX_KART_SPECS || buf_cap < 1)
        return 0;

#define EMIT(...)                                                     \
    do {                                                              \
        n = snprintf(buf + pos, (size_t)(buf_cap - pos), __VA_ARGS__); \
        if (n < 0 || pos + n >= buf_cap) return 0;                    \
        pos += n;                                                     \
    } while (0)

    EMIT("{\n  \"cars\": [\n");
    for (i = 0; i < count; i++) {
        const KartSpec *s = &specs[i];
        char dt[64];

        if (s->drivetrain == DRIVETRAIN_AWD)
            snprintf(dt, sizeof(dt),
                    "{ \"type\": \"awd\", \"front_bias\": %.3f }",
                    (double)s->awd_front_bias);
        else
            snprintf(dt, sizeof(dt), "{ \"type\": \"%s\" }",
                    s->drivetrain == DRIVETRAIN_FWD ? "fwd" : "rwd");

        EMIT("    {\n"
             "      \"name\": \"%s\",\n"
             "      \"mass_kg\": %.3f,\n"
             "      \"power_hp\": %.3f,\n"
             "      \"brake_distance_100_kph_m\": %.3f,\n"
             "      \"lateral_grip_g\": %.3f,\n"
             "      \"drag_area_m2\": %.3f,\n"
             "      \"wheelbase_m\": %.3f,\n"
             "      \"offroad_grip\": %.3f,\n"
             "      \"gear_top_speeds_kph\": [",
             s->name, (double)s->mass_kg, (double)s->power_hp,
             (double)s->brake_dist_100, (double)s->lat_g, (double)s->cd_a,
             (double)s->wheelbase, (double)s->offroad_grip);
        for (g = 0; g < s->n_gears; g++)
            EMIT("%s%.3f", g ? ", " : "", (double)(s->gear_top[g] * 3.6f));
        EMIT("],\n"
             "      \"nominal_rpm\": %.1f,\n"
             "      \"aspiration\": \"%s\",\n"
             "      \"drivetrain\": %s\n"
             "    }%s\n",
             (double)s->nominal_rpm,
             s->aspiration == ASPIRATION_TURBO ? "turbo" :
             s->aspiration == ASPIRATION_SUPERCHARGED ? "supercharged" :
             s->aspiration == ASPIRATION_TWIN_TURBO ? "twin_turbo" :
             "na",
             dt, (i + 1 < count) ? "," : "");
    }
    EMIT("  ]\n}\n");
#undef EMIT
    return pos;
}

/*
 * Write specs[0..count-1] to path as a cars.json document. Used by the
 * in-game car designer to make a saved car (kart_specs_add_custom,
 * already live in memory for the rest of this session) survive past
 * the game closing — without this, "saving" a car would only last
 * until the console was turned off.
 */
int config_save_cars_file(const char *path, const KartSpec *specs,
                          int count, char *error, int error_cap)
{
    char *buf;
    int len;
    FILE *f;
    const int cap = 32768;

    buf = (char *)malloc((size_t)cap);
    if (!buf) return set_error(error, error_cap, "NO SAVE MEMORY");
    len = config_write_cars_text(specs, count, buf, cap);
    if (len <= 0) {
        free(buf);
        return set_error(error, error_cap, "CAR ROSTER TOO BIG TO SAVE");
    }
    f = fopen(path, "wb");
    if (!f) {
        free(buf);
        return set_error(error, error_cap, "COULD NOT OPEN FILE TO SAVE");
    }
    if (fwrite(buf, 1u, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(buf);
        return set_error(error, error_cap, "COULD NOT WRITE FILE");
    }
    fclose(f);
    free(buf);
    if (error && error_cap > 0) error[0] = '\0';
    return 1;
}

/* ------------------------------------------------------------------ */
/* Settings                                                           */
/* ------------------------------------------------------------------ */

static int read_track_settings(GameSettings *s, const char *json,
                               const JsonToken *tokens, int count,
                               int tracks, int id, const char *name,
                               char *error, int error_cap)
{
    int obj = object_get(json, tokens, count, tracks, name);
    if (obj < 0) return 1;
    if (tokens[obj].type != JT_OBJECT)
        return set_error(error, error_cap, "TRACK NEEDS OBJECT");
    return optional_float(json, tokens, count, obj, "width_multiplier",
                          &s->track_width_mult[id], error, error_cap) &&
           optional_float(json, tokens, count, obj, "scale_multiplier",
                          &s->track_scale_mult[id], error, error_cap) &&
           optional_float(json, tokens, count, obj, "elevation_multiplier",
                          &s->track_elevation_mult[id], error, error_cap) &&
           optional_int(json, tokens, count, obj, "laps",
                        &s->track_laps[id], error, error_cap);
}

int config_load_settings_text(GameSettings *settings, const char *json,
                              char *error, int error_cap)
{
    JsonToken *tokens;
    GameSettings s;
    int count, obj;

    if (!settings)
        return set_error(error, error_cap, "NO SETTINGS TARGET");
    s = *settings;
    tokens = (JsonToken *)malloc(sizeof(*tokens) * JSON_MAX_TOKENS);
    if (!tokens) return set_error(error, error_cap, "NO JSON MEMORY");
    count = json_tokenize(json, tokens, JSON_MAX_TOKENS, error, error_cap);
    if (count <= 0 || tokens[0].type != JT_OBJECT) {
        free(tokens);
        if (count > 0) set_error(error, error_cap, "SETTINGS NEED OBJECT");
        return 0;
    }

    obj = object_get(json, tokens, count, 0, "race");
    if (obj >= 0 && tokens[obj].type != JT_OBJECT) {
        set_error(error, error_cap, "RACE NEEDS OBJECT");
        goto fail;
    }
    if (obj >= 0 &&
        (!optional_float(json, tokens, count, obj, "countdown_seconds",
                         &s.countdown_seconds, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "target_distance_m",
                         &s.target_race_distance_m, error, error_cap) ||
         !optional_int(json, tokens, count, obj, "minimum_laps",
                       &s.min_laps, error, error_cap) ||
         !optional_int(json, tokens, count, obj, "maximum_laps",
                       &s.max_laps, error, error_cap))) goto fail;

    obj = object_get(json, tokens, count, 0, "physics");
    if (obj >= 0 && tokens[obj].type != JT_OBJECT) {
        set_error(error, error_cap, "PHYSICS NEEDS OBJECT");
        goto fail;
    }
    if (obj >= 0 &&
        (!optional_float(json, tokens, count, obj,
                         "rolling_resistance", &s.rolling_resistance,
                         error, error_cap) ||
         !optional_float(json, tokens, count, obj,
                         "drivetrain_efficiency", &s.drivetrain_efficiency,
                         error, error_cap) ||
         !optional_float(json, tokens, count, obj, "shift_seconds",
                         &s.shift_seconds, error, error_cap))) goto fail;

    obj = object_get(json, tokens, count, 0, "steering");
    if (obj >= 0 && tokens[obj].type != JT_OBJECT) {
        set_error(error, error_cap, "STEERING NEEDS OBJECT");
        goto fail;
    }
    if (obj >= 0 &&
        (!optional_float(json, tokens, count, obj, "wind_on_rate",
                         &s.steer_rate_on, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "self_center_rate",
                         &s.steer_rate_center, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "speed_fade",
                         &s.steer_speed_fade, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "response_curve",
                         &s.steer_curve, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "max_angle_deg",
                         &s.steer_max_angle_deg, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "speed_taper",
                         &s.steer_speed_taper, error, error_cap))) goto fail;

    obj = object_get(json, tokens, count, 0, "tires");
    if (obj >= 0 && tokens[obj].type != JT_OBJECT) {
        set_error(error, error_cap, "TIRES NEEDS OBJECT");
        goto fail;
    }
    if (obj >= 0) {
        static const char *names[TIRE_COMPOUNDS] = { "medium", "soft", "hard" };
        int i;
        for (i = 0; i < TIRE_COMPOUNDS; i++) {
            int tire = object_get(json, tokens, count, obj, names[i]);
            if (tire >= 0 && tokens[tire].type != JT_OBJECT) {
                set_error(error, error_cap, "TIRE NEEDS OBJECT");
                goto fail;
            }
            if (tire >= 0 &&
                (!optional_float(json, tokens, count, tire,
                                 "grip_multiplier", &s.tire_grip_mult[i],
                                 error, error_cap) ||
                 !optional_float(json, tokens, count, tire,
                                 "drag_multiplier", &s.tire_drag_mult[i],
                                 error, error_cap) ||
                 !optional_float(json, tokens, count, tire,
                                 "rolling_multiplier",
                                 &s.tire_rolling_mult[i], error, error_cap) ||
                 !optional_float(json, tokens, count, tire,
                                 "wear_rate_per_second",
                                 &s.tire_wear_rate[i], error, error_cap) ||
                 !optional_float(json, tokens, count, tire,
                                 "grip_lost_when_worn",
                                 &s.tire_wear_grip_loss[i], error,
                                 error_cap) ||
                 !optional_float(json, tokens, count, tire, "optimal_temp_c",
                                 &s.tire_temp_optimal[i], error, error_cap) ||
                 !optional_float(json, tokens, count, tire, "temp_window_c",
                                 &s.tire_temp_window[i], error, error_cap) ||
                 !optional_float(json, tokens, count, tire, "heat_rate",
                                 &s.tire_heat_rate[i], error, error_cap) ||
                 !optional_float(json, tokens, count, tire, "cool_rate",
                                 &s.tire_cool_rate[i], error, error_cap) ||
                 !optional_float(json, tokens, count, tire,
                                 "off_window_grip",
                                 &s.tire_off_window_grip[i], error,
                                 error_cap))) goto fail;
        }
    }

    if (obj >= 0 &&
        !optional_float(json, tokens, count, obj, "ambient_c",
                        &s.tire_ambient_c, error, error_cap)) goto fail;

    obj = object_get(json, tokens, count, 0, "weather");
    if (obj >= 0 && tokens[obj].type != JT_OBJECT) {
        set_error(error, error_cap, "WEATHER NEEDS OBJECT");
        goto fail;
    }
    if (obj >= 0 &&
        (!optional_float(json, tokens, count, obj, "snow_to_ice_seconds",
                         &s.weather_snow_to_ice_s, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "ice_to_puddle_seconds",
                         &s.weather_ice_to_puddle_s, error,
                         error_cap))) goto fail;
    if (obj >= 0) {
        static const char *names[TIRE_COMPOUNDS] = { "medium", "soft", "hard" };
        int i;
        for (i = 0; i < TIRE_COMPOUNDS; i++) {
            int tire = object_get(json, tokens, count, obj, names[i]);
            if (tire >= 0 && tokens[tire].type != JT_OBJECT) {
                set_error(error, error_cap, "WEATHER TIRE NEEDS OBJECT");
                goto fail;
            }
            if (tire >= 0 &&
                (!optional_float(json, tokens, count, tire, "snow_grip",
                                 &s.weather_snow_grip[i], error,
                                 error_cap) ||
                 !optional_float(json, tokens, count, tire, "ice_grip",
                                 &s.weather_ice_grip[i], error,
                                 error_cap) ||
                 !optional_float(json, tokens, count, tire, "puddle_grip",
                                 &s.weather_puddle_grip[i], error,
                                 error_cap))) goto fail;
        }
    }
    if (obj >= 0 &&
        !optional_float(json, tokens, count, obj, "puddle_drag_multiplier",
                        &s.weather_puddle_drag_mult, error, error_cap))
        goto fail;

    obj = object_get(json, tokens, count, 0, "turbo");
    if (obj >= 0 && tokens[obj].type != JT_OBJECT) {
        set_error(error, error_cap, "TURBO NEEDS OBJECT");
        goto fail;
    }
    if (obj >= 0 &&
        (!optional_float(json, tokens, count, obj, "spool_rate_per_second",
                         &s.turbo_spool_rate, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "decay_rate_per_second",
                         &s.turbo_spool_decay_rate, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "max_power_bonus",
                         &s.turbo_max_power_bonus, error,
                         error_cap))) goto fail;

    obj = object_get(json, tokens, count, 0, "understeer");
    if (obj >= 0 && tokens[obj].type != JT_OBJECT) {
        set_error(error, error_cap, "UNDERSTEER NEEDS OBJECT");
        goto fail;
    }
    if (obj >= 0 &&
        (!optional_float(json, tokens, count, obj, "scrub",
                         &s.understeer_scrub, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "scrub_curve",
                         &s.understeer_scrub_curve, error,
                         error_cap))) goto fail;

    obj = object_get(json, tokens, count, 0, "drift");
    if (obj >= 0 && tokens[obj].type != JT_OBJECT) {
        set_error(error, error_cap, "DRIFT NEEDS OBJECT");
        goto fail;
    }
    if (obj >= 0 &&
        (!optional_float(json, tokens, count, obj, "peak_slip_deg",
                         &s.slip_peak_deg, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "falloff_range",
                         &s.slip_falloff_range, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "floor_frac",
                         &s.slip_floor_frac, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "weight_transfer_coeff",
                         &s.weight_transfer_coeff, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "yaw_inertia_mult",
                         &s.yaw_inertia_mult, error, error_cap) ||
         !optional_float(json, tokens, count, obj,
                         "stability_control_strength",
                         &s.stability_control_strength, error,
                         error_cap) ||
         !optional_float(json, tokens, count, obj, "loose_surface_bonus",
                         &s.drift_loose_surface_bonus, error,
                         error_cap))) goto fail;

    obj = object_get(json, tokens, count, 0, "ai");
    if (obj >= 0 && tokens[obj].type != JT_OBJECT) {
        set_error(error, error_cap, "AI NEEDS OBJECT");
        goto fail;
    }
    if (obj >= 0 &&
        (!optional_float(json, tokens, count, obj, "skill_multiplier",
                         &s.ai_skill_mult, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "braking_multiplier",
                         &s.ai_brake_mult, error, error_cap) ||
         !optional_float(json, tokens, count, obj,
                         "unguarded_line_room", &s.ai_unguarded_line_room,
                         error, error_cap) ||
         !optional_float(json, tokens, count, obj,
                         "overcommit_chance_per_corner",
                         &s.ai_overcommit_chance, error, error_cap) ||
         !optional_float(json, tokens, count, obj,
                         "overcommit_min_curvature",
                         &s.ai_overcommit_min_curvature, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "overcommit_seconds",
                         &s.ai_overcommit_seconds, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "overcommit_overshoot_m",
                         &s.ai_overcommit_overshoot_m,
                         error, error_cap))) goto fail;

    obj = object_get(json, tokens, count, 0, "hills");
    if (obj >= 0 && tokens[obj].type != JT_OBJECT) {
        set_error(error, error_cap, "HILLS NEED OBJECT");
        goto fail;
    }
    if (obj >= 0 &&
        (!optional_float(json, tokens, count, obj, "gravity_multiplier",
                         &s.grade_gravity_mult, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "load_effect",
                         &s.grade_load_effect, error, error_cap))) goto fail;

    obj = object_get(json, tokens, count, 0, "instruments");
    if (obj >= 0 && tokens[obj].type != JT_OBJECT) {
        set_error(error, error_cap, "INSTRUMENTS NEED OBJECT");
        goto fail;
    }
    if (obj >= 0 &&
        (!optional_float(json, tokens, count, obj, "tacho_idle_rpm",
                         &s.tacho_idle_rpm, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "tacho_redline_rpm",
                         &s.tacho_redline_rpm, error, error_cap))) goto fail;

    obj = object_get(json, tokens, count, 0, "camera");
    if (obj >= 0 && tokens[obj].type != JT_OBJECT) {
        set_error(error, error_cap, "CAMERA NEEDS OBJECT");
        goto fail;
    }
    if (obj >= 0 &&
        (!optional_float(json, tokens, count, obj, "distance_m",
                         &s.cam_distance_m, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "height_m",
                         &s.cam_height_m, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "min_height_above_road_m",
                         &s.cam_min_height_m, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "look_ahead_m",
                         &s.cam_look_ahead_m, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "look_ahead_per_mps",
                         &s.cam_look_ahead_per_mps, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "look_ahead_max_m",
                         &s.cam_look_ahead_max_m, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "look_height_m",
                         &s.cam_look_height_m, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "look_min_height_m",
                         &s.cam_look_min_height_m, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "follow_smoothing",
                         &s.cam_follow_smoothing, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "look_smoothing",
                         &s.cam_look_smoothing, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "reverse_deadzone_mps",
                         &s.cam_reverse_deadzone_mps, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "reverse_full_mps",
                         &s.cam_reverse_full_mps, error, error_cap) ||
         !optional_float(json, tokens, count, obj,
                         "reverse_orbit_rate_deg_per_s",
                         &s.cam_reverse_orbit_rate_dps, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "reverse_smoothing",
                         &s.cam_reverse_smoothing, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "pitch_influence",
                         &s.cam_pitch_influence, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "pitch_smoothing",
                         &s.cam_pitch_smoothing, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "pitch_min_deg",
                         &s.cam_pitch_min_deg, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "pitch_max_deg",
                         &s.cam_pitch_max_deg, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "corner_lean",
                         &s.cam_corner_lean, error, error_cap))) goto fail;

    obj = object_get(json, tokens, count, 0, "respawn");
    if (obj >= 0 && tokens[obj].type != JT_OBJECT) {
        set_error(error, error_cap, "RESPAWN NEEDS OBJECT");
        goto fail;
    }
    if (obj >= 0 &&
        (!optional_float(json, tokens, count, obj, "visible_fall_seconds",
                         &s.fall_seconds, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "black_hold_seconds",
                         &s.respawn_black_seconds, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "fade_in_seconds",
                         &s.respawn_fade_seconds, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "invincible_seconds",
                         &s.invincible_seconds, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "flash_hz",
                         &s.invincible_flash_hz, error, error_cap))) goto fail;

    obj = object_get(json, tokens, count, 0, "wrong_way");
    if (obj >= 0 && tokens[obj].type != JT_OBJECT) {
        set_error(error, error_cap, "WRONG_WAY NEEDS OBJECT");
        goto fail;
    }
    if (obj >= 0 &&
        (!optional_float(json, tokens, count, obj, "seconds",
                         &s.wrong_way_seconds, error, error_cap) ||
         !optional_float(json, tokens, count, obj, "power",
                         &s.wrong_way_power, error, error_cap))) goto fail;

    obj = object_get(json, tokens, count, 0, "tracks");
    if (obj >= 0 && tokens[obj].type != JT_OBJECT) {
        set_error(error, error_cap, "TRACKS NEED OBJECT");
        goto fail;
    }
    if (obj >= 0 &&
        (!read_track_settings(&s, json, tokens, count, obj, TRACK_CLASSIC,
                              "classic", error, error_cap) ||
         !read_track_settings(&s, json, tokens, count, obj, TRACK_BERTHOUD,
                              "berthoud", error, error_cap) ||
         !read_track_settings(&s, json, tokens, count, obj, TRACK_LOVELAND,
                              "loveland", error, error_cap) ||
         !read_track_settings(&s, json, tokens, count, obj, TRACK_KENOSHA,
                              "kenosha", error, error_cap) ||
         !read_track_settings(&s, json, tokens, count, obj, TRACK_MONARCH,
                              "monarch", error, error_cap) ||
         !read_track_settings(&s, json, tokens, count, obj, TRACK_BREAKNECK,
                              "breakneck", error, error_cap) ||
         !read_track_settings(&s, json, tokens, count, obj, TRACK_GUANELLA,
                              "guanella", error, error_cap) ||
         !read_track_settings(&s, json, tokens, count, obj, TRACK_BERTHOUD2,
                              "berthoud2", error, error_cap) ||
         !read_track_settings(&s, json, tokens, count, obj, TRACK_BULLRING,
                              "bullring", error, error_cap))) goto fail;

    if (!game_settings_validate(&s, error, error_cap)) goto fail;
    *settings = s;
    free(tokens);
    if (error && error_cap > 0) error[0] = '\0';
    return 1;

fail:
    free(tokens);
    return 0;
}

int config_load_settings_file(GameSettings *settings, const char *path,
                              char *error, int error_cap)
{
    char *text;
    int ok;
    if (!load_text_file(path, &text, error, error_cap)) return 0;
    ok = config_load_settings_text(settings, text, error, error_cap);
    free(text);
    return ok;
}

/* ------------------------------------------------------------------ */
/* Controls                                                           */
/* ------------------------------------------------------------------ */

static const char *action_keys[CONTROL_ACTION_COUNT] = {
    "steer_left", "steer_right", "accelerate", "brake", "handbrake",
    "shift_up", "shift_down", "boost", "race_menu", "menu_confirm",
    "menu_back"
};

static const char *action_labels[CONTROL_ACTION_COUNT] = {
    "LEFT", "RIGHT", "GAS", "BRAKE", "HANDBRAKE",
    "SHIFT UP", "SHIFT DOWN", "BOOST", "MENU", "CONFIRM", "BACK"
};

const char *control_action_name(int action)
{
    if (action < 0 || action >= CONTROL_ACTION_COUNT) return "";
    return action_labels[action];
}

const char *control_key_name(int key_code)
{
    static char letter[2];
    if (key_code >= 'A' && key_code <= 'Z') {
        letter[0] = (char)key_code;
        letter[1] = '\0';
        return letter;
    }
    switch (key_code) {
    case GAME_KEY_SPACE:  return "SPACE";
    case GAME_KEY_ENTER:  return "ENTER";
    case GAME_KEY_ESCAPE: return "ESC";
    case GAME_KEY_LEFT:   return "LEFT";
    case GAME_KEY_RIGHT:  return "RIGHT";
    case GAME_KEY_UP:     return "UP";
    case GAME_KEY_DOWN:   return "DOWN";
    default:              return "-";
    }
}

const char *control_gamecube_name(unsigned int mask)
{
    static char labels[4][64];
    static unsigned int next_label;
    static const struct {
        unsigned int bit;
        const char *name;
    } buttons[] = {
        { GC_INPUT_A, "A" }, { GC_INPUT_B, "B" },
        { GC_INPUT_X, "X" }, { GC_INPUT_Y, "Y" },
        { GC_INPUT_Z, "Z" }, { GC_INPUT_L, "L" },
        { GC_INPUT_R, "R" }, { GC_INPUT_START, "START" }
    };
    const unsigned int stick = GC_INPUT_STICK_LEFT | GC_INPUT_STICK_RIGHT;
    const unsigned int dpad = GC_INPUT_DPAD_LEFT | GC_INPUT_DPAD_RIGHT |
                              GC_INPUT_DPAD_UP | GC_INPUT_DPAD_DOWN;
    char *out = labels[next_label++ & 3u];
    int i, first = 1;

    if (!mask)
        return "-";
    snprintf(out, sizeof(labels[0]), "GC ");
#define ADD_NAME(name)                                                     \
    do {                                                                   \
        size_t used = strlen(out);                                         \
        if (used + 1u < sizeof(labels[0]))                                \
            snprintf(out + used, sizeof(labels[0]) - used, "%s%s",       \
                     first ? "" : "/", name);                            \
        first = 0;                                                         \
    } while (0)
    for (i = 0; i < (int)(sizeof(buttons) / sizeof(buttons[0])); i++)
        if (mask & buttons[i].bit)
            ADD_NAME(buttons[i].name);
    if (mask & stick) ADD_NAME("STICK");
    if (mask & dpad) ADD_NAME("DPAD");
#undef ADD_NAME
    return out;
}

static void bind_key(ControlConfig *c, int p, int action,
                     int slot, int code)
{
    c->keyboard[p][action][slot] = code;
}

void control_config_defaults(ControlConfig *c)
{
    memset(c, 0, sizeof(*c));
    c->show_input_overlay = 0;

    bind_key(c, 0, CONTROL_STEER_LEFT, 0, 'A');
    bind_key(c, 0, CONTROL_STEER_LEFT, 1, GAME_KEY_LEFT);
    bind_key(c, 0, CONTROL_STEER_RIGHT, 0, 'D');
    bind_key(c, 0, CONTROL_STEER_RIGHT, 1, GAME_KEY_RIGHT);
    bind_key(c, 0, CONTROL_ACCEL, 0, 'W');
    bind_key(c, 0, CONTROL_ACCEL, 1, GAME_KEY_UP);
    bind_key(c, 0, CONTROL_BRAKE, 0, 'S');
    bind_key(c, 0, CONTROL_BRAKE, 1, GAME_KEY_DOWN);
    bind_key(c, 0, CONTROL_HANDBRAKE, 0, GAME_KEY_SPACE);
    bind_key(c, 0, CONTROL_GEAR_UP, 0, 'E');
    bind_key(c, 0, CONTROL_GEAR_DOWN, 0, 'Q');
    bind_key(c, 0, CONTROL_BOOST, 0, 'F');
    bind_key(c, 0, CONTROL_RACE_MENU, 0, 'R');
    bind_key(c, 0, CONTROL_MENU_CONFIRM, 0, GAME_KEY_ENTER);
    bind_key(c, 0, CONTROL_MENU_BACK, 0, GAME_KEY_ESCAPE);

    bind_key(c, 1, CONTROL_STEER_LEFT, 0, 'J');
    bind_key(c, 1, CONTROL_STEER_RIGHT, 0, 'L');
    bind_key(c, 1, CONTROL_ACCEL, 0, 'I');
    bind_key(c, 1, CONTROL_BRAKE, 0, 'K');
    bind_key(c, 1, CONTROL_HANDBRAKE, 0, 'P');
    bind_key(c, 1, CONTROL_GEAR_UP, 0, 'O');
    bind_key(c, 1, CONTROL_GEAR_DOWN, 0, 'U');
    bind_key(c, 1, CONTROL_BOOST, 0, 'H');
    bind_key(c, 1, CONTROL_RACE_MENU, 0, 'R');

    c->gamecube[CONTROL_STEER_LEFT] = GC_INPUT_STICK_LEFT |
                                             GC_INPUT_DPAD_LEFT;
    c->gamecube[CONTROL_STEER_RIGHT] = GC_INPUT_STICK_RIGHT |
                                              GC_INPUT_DPAD_RIGHT;
    c->gamecube[CONTROL_ACCEL] = GC_INPUT_A | GC_INPUT_X;
    c->gamecube[CONTROL_BRAKE] = GC_INPUT_B;
    c->gamecube[CONTROL_HANDBRAKE] = GC_INPUT_Z;
    c->gamecube[CONTROL_GEAR_UP] = GC_INPUT_R;
    c->gamecube[CONTROL_GEAR_DOWN] = GC_INPUT_L;
    c->gamecube[CONTROL_BOOST] = GC_INPUT_Y;
    c->gamecube[CONTROL_RACE_MENU] = GC_INPUT_START;
    c->gamecube[CONTROL_MENU_CONFIRM] = GC_INPUT_A | GC_INPUT_Z;
    c->gamecube[CONTROL_MENU_BACK] = GC_INPUT_B;

    snprintf(c->xbox_label[CONTROL_STEER_LEFT], CONTROL_LABEL_LEN, "LS LEFT");
    snprintf(c->xbox_label[CONTROL_STEER_RIGHT], CONTROL_LABEL_LEN, "LS RIGHT");
    snprintf(c->xbox_label[CONTROL_ACCEL], CONTROL_LABEL_LEN, "RT");
    snprintf(c->xbox_label[CONTROL_BRAKE], CONTROL_LABEL_LEN, "LT");
    snprintf(c->xbox_label[CONTROL_HANDBRAKE], CONTROL_LABEL_LEN, "B");
    snprintf(c->xbox_label[CONTROL_GEAR_UP], CONTROL_LABEL_LEN, "RB");
    snprintf(c->xbox_label[CONTROL_GEAR_DOWN], CONTROL_LABEL_LEN, "LB");
    snprintf(c->xbox_label[CONTROL_BOOST], CONTROL_LABEL_LEN, "Y");
    snprintf(c->xbox_label[CONTROL_RACE_MENU], CONTROL_LABEL_LEN, "START");
    snprintf(c->xbox_label[CONTROL_MENU_CONFIRM], CONTROL_LABEL_LEN, "A/RT");
    snprintf(c->xbox_label[CONTROL_MENU_BACK], CONTROL_LABEL_LEN, "LT");
}

static int key_code_from_name(const char *name)
{
    char up[16];
    int i;
    for (i = 0; name[i] && i + 1 < (int)sizeof(up); i++)
        up[i] = (char)toupper((unsigned char)name[i]);
    up[i] = '\0';
    if (up[0] && !up[1] && up[0] >= 'A' && up[0] <= 'Z') return up[0];
    if (strcmp(up, "SPACE") == 0) return GAME_KEY_SPACE;
    if (strcmp(up, "ENTER") == 0 || strcmp(up, "RETURN") == 0)
        return GAME_KEY_ENTER;
    if (strcmp(up, "ESC") == 0 || strcmp(up, "ESCAPE") == 0)
        return GAME_KEY_ESCAPE;
    if (strcmp(up, "LEFT") == 0) return GAME_KEY_LEFT;
    if (strcmp(up, "RIGHT") == 0) return GAME_KEY_RIGHT;
    if (strcmp(up, "UP") == 0) return GAME_KEY_UP;
    if (strcmp(up, "DOWN") == 0) return GAME_KEY_DOWN;
    return GAME_KEY_NONE;
}

static unsigned int gc_code_from_name(const char *name)
{
    char up[24];
    int i;
    for (i = 0; name[i] && i + 1 < (int)sizeof(up); i++) {
        char c = (char)toupper((unsigned char)name[i]);
        up[i] = c == ' ' ? '_' : c;
    }
    up[i] = '\0';
    if (strcmp(up, "A") == 0) return GC_INPUT_A;
    if (strcmp(up, "B") == 0) return GC_INPUT_B;
    if (strcmp(up, "X") == 0) return GC_INPUT_X;
    if (strcmp(up, "Y") == 0) return GC_INPUT_Y;
    if (strcmp(up, "Z") == 0) return GC_INPUT_Z;
    if (strcmp(up, "L") == 0) return GC_INPUT_L;
    if (strcmp(up, "R") == 0) return GC_INPUT_R;
    if (strcmp(up, "START") == 0) return GC_INPUT_START;
    if (strcmp(up, "DPAD_LEFT") == 0) return GC_INPUT_DPAD_LEFT;
    if (strcmp(up, "DPAD_RIGHT") == 0) return GC_INPUT_DPAD_RIGHT;
    if (strcmp(up, "DPAD_UP") == 0) return GC_INPUT_DPAD_UP;
    if (strcmp(up, "DPAD_DOWN") == 0) return GC_INPUT_DPAD_DOWN;
    if (strcmp(up, "STICK_LEFT") == 0) return GC_INPUT_STICK_LEFT;
    if (strcmp(up, "STICK_RIGHT") == 0) return GC_INPUT_STICK_RIGHT;
    return 0;
}

static int parse_key_list(const char *json, const JsonToken *tokens,
                          int count, int at, int out[CONTROL_MAX_BINDS],
                          char *error, int error_cap)
{
    int n, i;
    memset(out, 0, sizeof(int) * CONTROL_MAX_BINDS);
    n = tokens[at].type == JT_ARRAY ? array_length(tokens, count, at) : 1;
    if (n < 1 || n > CONTROL_MAX_BINDS)
        return set_error(error, error_cap, "BAD KEY LIST");
    for (i = 0; i < n; i++) {
        int item = tokens[at].type == JT_ARRAY
                       ? array_item(tokens, count, at, i) : at;
        char name[20];
        if (item < 0 || !token_string(json, &tokens[item], name,
                                      (int)sizeof(name)))
            return set_error(error, error_cap, "BAD KEY NAME");
        out[i] = key_code_from_name(name);
        if (!out[i]) return set_error(error, error_cap, "UNKNOWN KEY");
    }
    return 1;
}

static int parse_gc_list(const char *json, const JsonToken *tokens,
                         int count, int at, unsigned int *out,
                         char *error, int error_cap)
{
    int n, i;
    unsigned int mask = 0;
    n = tokens[at].type == JT_ARRAY ? array_length(tokens, count, at) : 1;
    if (n < 1 || n > 8)
        return set_error(error, error_cap, "BAD PAD LIST");
    for (i = 0; i < n; i++) {
        int item = tokens[at].type == JT_ARRAY
                       ? array_item(tokens, count, at, i) : at;
        char name[24];
        unsigned int code;
        if (item < 0 || !token_string(json, &tokens[item], name,
                                      (int)sizeof(name)))
            return set_error(error, error_cap, "BAD PAD NAME");
        code = gc_code_from_name(name);
        if (!code) return set_error(error, error_cap, "UNKNOWN PAD INPUT");
        mask |= code;
    }
    *out = mask;
    return 1;
}

int config_load_controls_text(ControlConfig *controls, const char *json,
                              char *error, int error_cap)
{
    JsonToken *tokens;
    ControlConfig c;
    int count, obj, p, action;

    if (!controls)
        return set_error(error, error_cap, "NO CONTROL TARGET");
    c = *controls;
    tokens = (JsonToken *)malloc(sizeof(*tokens) * JSON_MAX_TOKENS);
    if (!tokens) return set_error(error, error_cap, "NO JSON MEMORY");
    count = json_tokenize(json, tokens, JSON_MAX_TOKENS, error, error_cap);
    if (count <= 0 || tokens[0].type != JT_OBJECT) {
        free(tokens);
        if (count > 0) set_error(error, error_cap, "CONTROLS NEED OBJECT");
        return 0;
    }

    obj = object_get(json, tokens, count, 0, "show_input_overlay");
    if (obj >= 0 && !token_bool(json, &tokens[obj], &c.show_input_overlay)) {
        free(tokens);
        return set_error(error, error_cap, "OVERLAY MUST BE BOOL");
    }

    obj = object_get(json, tokens, count, 0, "keyboard");
    if (obj >= 0) {
        static const char *players[CONTROL_KEYBOARD_PLAYERS] = {
            "player1", "player2"
        };
        if (tokens[obj].type != JT_OBJECT) {
            free(tokens);
            return set_error(error, error_cap, "KEYBOARD NEEDS OBJECT");
        }
        for (p = 0; p < CONTROL_KEYBOARD_PLAYERS; p++) {
            int po = object_get(json, tokens, count, obj, players[p]);
            if (po < 0) continue;
            if (tokens[po].type != JT_OBJECT) {
                free(tokens);
                return set_error(error, error_cap, "PLAYER NEEDS OBJECT");
            }
            for (action = 0; action < CONTROL_ACTION_COUNT; action++) {
                int at = object_get(json, tokens, count, po,
                                    action_keys[action]);
                if (at >= 0 && !parse_key_list(json, tokens, count, at,
                                               c.keyboard[p][action],
                                               error, error_cap)) {
                    free(tokens);
                    return 0;
                }
            }
        }
    }

    obj = object_get(json, tokens, count, 0, "gamecube");
    if (obj >= 0) {
        if (tokens[obj].type != JT_OBJECT) {
            free(tokens);
            return set_error(error, error_cap, "GAMECUBE NEEDS OBJECT");
        }
        for (action = 0; action < CONTROL_ACTION_COUNT; action++) {
            int at = object_get(json, tokens, count, obj, action_keys[action]);
            if (at >= 0 && !parse_gc_list(json, tokens, count, at,
                                          &c.gamecube[action],
                                          error, error_cap)) {
                free(tokens);
                return 0;
            }
        }
    }

    obj = object_get(json, tokens, count, 0, "xbox_recommended");
    if (obj >= 0) {
        if (tokens[obj].type != JT_OBJECT) {
            free(tokens);
            return set_error(error, error_cap, "XBOX NEEDS OBJECT");
        }
        for (action = 0; action < CONTROL_ACTION_COUNT; action++) {
            int at = object_get(json, tokens, count, obj, action_keys[action]);
            if (at >= 0 && !token_string(json, &tokens[at],
                                         c.xbox_label[action],
                                         CONTROL_LABEL_LEN)) {
                free(tokens);
                return set_error(error, error_cap, "BAD XBOX LABEL");
            }
        }
    }

    if (!c.keyboard[0][CONTROL_STEER_LEFT][0] ||
        !c.keyboard[0][CONTROL_STEER_RIGHT][0] ||
        !c.keyboard[0][CONTROL_ACCEL][0] ||
        !c.keyboard[0][CONTROL_BRAKE][0] ||
        !c.gamecube[CONTROL_STEER_LEFT] ||
        !c.gamecube[CONTROL_STEER_RIGHT] ||
        !c.gamecube[CONTROL_ACCEL] || !c.gamecube[CONTROL_BRAKE]) {
        free(tokens);
        return set_error(error, error_cap, "DRIVING INPUT UNBOUND");
    }

    *controls = c;
    free(tokens);
    if (error && error_cap > 0) error[0] = '\0';
    return 1;
}

int config_load_controls_file(ControlConfig *controls, const char *path,
                              char *error, int error_cap)
{
    char *text;
    int ok;
    if (!load_text_file(path, &text, error, error_cap)) return 0;
    ok = config_load_controls_text(controls, text, error, error_cap);
    free(text);
    return ok;
}
