/*
 * draw_rig.inc — drawing the tractor and its trailers.
 *
 * #include'd directly into main.c (not a separate translation unit) so
 * it can share main.c's quad()/draw_box()/shade() helpers without
 * exposing them outside this file. All dimensions here are real
 * meters, taken straight from TruckSpec/TrailerSpec, unlike WiiKart's
 * car model which scaled an abstract "car unit" — a truck's numbers are
 * already the right units to draw with.
 */

#define DECK_HEIGHT 1.2f    /* trailer floor height off the ground, m   */
#define CHASSIS_HEIGHT 1.1f /* tractor frame rail height off the ground */
#define WHEEL_RADIUS 0.52f

static const u8 CAB_COLOR[3]     = {  55,  95, 165 };
static const u8 HOOD_COLOR[3]    = {  35,  60, 110 };
static const u8 WHEEL_COLOR[3]   = {  22,  22,  25 };
static const u8 BOX_COLOR[3]     = { 225, 225, 218 };
static const u8 FLATBED_COLOR[3] = { 150, 130,  85 };
static const u8 CARGO_COLOR[3]   = { 140,  95,  55 };
static const u8 TANKER_COLOR[3]  = { 205, 210, 215 };
static const u8 LOWBOY_COLOR[3]  = {  75,  78,  82 };
static const u8 LOAD_COLOR[3]    = { 100, 110,  85 };

static void draw_wheel_pair(float cx, float cy_ground, float cz, float yaw,
                            float track)
{
    float fx = cosf(yaw), fz = sinf(yaw);
    float lx = -fz, lz = fx;
    int s;
    for (s = -1; s <= 1; s += 2) {
        float wx = cx + lx * track * (float)s;
        float wz = cz + lz * track * (float)s;
        draw_box(wx, cy_ground + WHEEL_RADIUS, wz, yaw, 0.0f,
                WHEEL_RADIUS, WHEEL_RADIUS, 0.32f,
                WHEEL_COLOR[0], WHEEL_COLOR[1], WHEEL_COLOR[2]);
    }
}

static void draw_shadow(float cx, float cy_ground, float cz, float yaw,
                        float half_fwd, float half_lat)
{
    float fx = cosf(yaw), fz = sinf(yaw);
    float lx = -fz, lz = fx;
    float hf = half_fwd * 1.05f, hl = half_lat * 1.15f;
    quad(cx + fx * hf + lx * hl, cy_ground + 0.03f, cz + fz * hf + lz * hl,
        cx + fx * hf - lx * hl, cy_ground + 0.03f, cz + fz * hf - lz * hl,
        cx - fx * hf - lx * hl, cy_ground + 0.03f, cz - fz * hf - lz * hl,
        cx - fx * hf + lx * hl, cy_ground + 0.03f, cz - fz * hf + lz * hl,
        0, 0, 0, 85);
}

static void draw_tractor(float cx, float cy_ground, float cz, float yaw,
                         float pitch, const TruckSpec *ts)
{
    float fx = cosf(yaw), fz = sinf(yaw);
    float half_len = ts->body_length * 0.5f;
    float half_wid = ts->body_width * 0.5f;
    float front_wheel_x = half_len - 0.55f;
    float rear_wheel_x = front_wheel_x - ts->wheelbase;

    draw_shadow(cx, cy_ground, cz, yaw, half_len, half_wid);

    /* cab */
    draw_box(cx, cy_ground + CHASSIS_HEIGHT + 0.95f, cz, yaw, pitch,
            half_len * 0.55f, 0.95f, half_wid,
            CAB_COLOR[0], CAB_COLOR[1], CAB_COLOR[2]);
    /* hood, lower and further forward */
    draw_box(cx + fx * (half_len * 0.45f), cy_ground + CHASSIS_HEIGHT + 0.55f,
            cz + fz * (half_len * 0.45f), yaw, pitch,
            half_len * 0.42f, 0.55f, half_wid * 0.85f,
            HOOD_COLOR[0], HOOD_COLOR[1], HOOD_COLOR[2]);

    draw_wheel_pair(cx + fx * front_wheel_x, cy_ground, cz + fz * front_wheel_x,
                    yaw, half_wid * 0.92f);
    draw_wheel_pair(cx + fx * rear_wheel_x, cy_ground, cz + fz * rear_wheel_x,
                    yaw, half_wid * 0.92f);
}

static void draw_trailer(float cx, float cy_ground, float cz, float yaw,
                         float pitch, const TrailerSpec *tr)
{
    float half_len = tr->length * 0.5f;
    float half_wid = tr->width * 0.5f;

    draw_shadow(cx, cy_ground, cz, yaw, half_len, half_wid);
    draw_wheel_pair(cx - cosf(yaw) * (half_len - 0.7f), cy_ground,
                    cz - sinf(yaw) * (half_len - 0.7f), yaw,
                    half_wid * 0.9f);

    switch (tr->type) {
    case TRAILER_FLATBED: {
        float deck_h = DECK_HEIGHT;
        draw_box(cx, cy_ground + deck_h, cz, yaw, pitch, half_len, 0.15f,
                half_wid, FLATBED_COLOR[0], FLATBED_COLOR[1],
                FLATBED_COLOR[2]);
        {
            float fx = cosf(yaw), fz = sinf(yaw);
            int i;
            static const float offs[3] = { -0.45f, 0.0f, 0.42f };
            for (i = 0; i < 3; i++) {
                float ox = cx + fx * half_len * offs[i];
                float oz = cz + fz * half_len * offs[i];
                draw_box(ox, cy_ground + deck_h + 0.55f, oz, yaw, pitch,
                        half_len * 0.28f, 0.55f, half_wid * 0.62f,
                        CARGO_COLOR[0], CARGO_COLOR[1], CARGO_COLOR[2]);
            }
        }
        break;
    }
    case TRAILER_TANKER: {
        float mid_h = DECK_HEIGHT + half_wid;
        draw_box(cx, mid_h, cz, yaw, pitch, half_len, half_wid * 0.95f,
                half_wid, TANKER_COLOR[0], TANKER_COLOR[1], TANKER_COLOR[2]);
        draw_box(cx, mid_h + half_wid * 0.75f, cz, yaw, pitch,
                half_len * 0.9f, half_wid * 0.35f, half_wid * 0.72f,
                shade(TANKER_COLOR[0], 1.1f), shade(TANKER_COLOR[1], 1.1f),
                shade(TANKER_COLOR[2], 1.1f));
        break;
    }
    case TRAILER_LOWBOY: {
        float deck_h = DECK_HEIGHT * 0.5f;
        draw_box(cx, cy_ground + deck_h, cz, yaw, pitch, half_len, 0.20f,
                half_wid, LOWBOY_COLOR[0], LOWBOY_COLOR[1], LOWBOY_COLOR[2]);
        draw_box(cx, cy_ground + deck_h + 0.9f, cz, yaw, pitch,
                half_len * 0.7f, 0.85f, half_wid * 1.12f,
                LOAD_COLOR[0], LOAD_COLOR[1], LOAD_COLOR[2]);
        break;
    }
    case TRAILER_BOX:
    case TRAILER_PUP:
    default: {
        float deck_h = DECK_HEIGHT;
        float body_half_h = fmaxf((tr->height - deck_h) * 0.5f, 0.4f);
        draw_box(cx, cy_ground + deck_h + body_half_h, cz, yaw, pitch,
                half_len, body_half_h, half_wid,
                BOX_COLOR[0], BOX_COLOR[1], BOX_COLOR[2]);
        break;
    }
    }
}

/* ground pitch under a body, from a finite difference of the yard's own
 * height field along that body's own heading — same technique rig_step
 * uses for grade gravity, here purely for how the model tilts to draw */
static float body_pitch(const Yard *y, float x, float z, float heading)
{
    const float EPS = 0.4f;
    float h0 = yard_ground_at(y, x, z);
    float h1 = yard_ground_at(y, x + cosf(heading) * EPS,
                              z + sinf(heading) * EPS);
    return atanf((h1 - h0) / EPS);
}

static void draw_rig(const Yard *y, const Rig *r, const RigSpec *spec)
{
    const TruckSpec *ts = &truck_specs[spec->truck_idx];
    int i;

    draw_tractor(r->x, r->y, r->z, r->heading,
                body_pitch(y, r->x, r->z, r->heading), ts);

    for (i = 0; i < spec->n_trailers; i++) {
        const TrailerSpec *tr = &trailer_specs[spec->trailer_idx[i]];
        float cx, cz, hx, hz, gx, gz;
        rig_trailer_pos(r, spec, i, &cx, &cz);
        rig_hitch_pos(r, spec, i - 1, &hx, &hz);
        gx = (cx + hx) * 0.5f; gz = (cz + hz) * 0.5f;
        draw_trailer(gx, yard_ground_at(y, gx, gz), gz,
                    r->trailer_heading[i],
                    body_pitch(y, gx, gz, r->trailer_heading[i]), tr);
    }
}
