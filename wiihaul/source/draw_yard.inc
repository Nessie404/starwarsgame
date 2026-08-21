/*
 * draw_yard.inc — the ground, obstacles, cones, boundary lines and
 * target zone. #include'd into main.c — see draw_rig.inc's header.
 */

static const u8 GROUND_COLOR[3]    = { 108, 110, 112 };
static const u8 TARGET_COLOR[3]    = { 70, 130, 210 };
static const u8 TARGET_IN_COLOR[3] = { 60, 190, 90 };
static const u8 LINE_COLOR[3]      = { 225, 200, 40 };
static const u8 CONE_COLOR[3]      = { 230, 110, 30 };

static const u8 obstacle_color[OBSTACLE_KIND_COUNT][3] = {
    { 190, 190, 195 },   /* TRAILER (a parked rig, blocking the space)  */
    { 140, 120, 100 },   /* BUILDING                                    */
    { 200, 200, 195 },   /* CURB                                        */
    { 60, 130, 70 },     /* DUMPSTER                                    */
    { 90, 90, 95 }       /* POLE                                        */
};

static void draw_ground(const Yard *y)
{
    if (y->grade.kind == GRADE_RAMP) {
        const int SEGS = 12;
        float x0 = y->grade.x0, x1 = y->grade.x1;
        float step = (x1 - x0) / SEGS;
        int i;
        float flat_before = x0 - y->bounds_half_x * 2.0f;
        float flat_after  = x1 + y->bounds_half_x * 2.0f;

        ground_quad((flat_before + x0) * 0.5f, 0.0f,
                   (x0 - flat_before) * 0.5f, y->bounds_half_z * 2.0f,
                   y->grade.y0, GROUND_COLOR[0], GROUND_COLOR[1],
                   GROUND_COLOR[2], 255);
        for (i = 0; i < SEGS; i++) {
            float xa = x0 + step * (float)i;
            float xb = xa + step;
            float ya = yard_ground_at(y, xa, 0.0f);
            float yb = yard_ground_at(y, xb, 0.0f);
            quad(xa, ya, -y->bounds_half_z * 2.0f,
                xb, yb, -y->bounds_half_z * 2.0f,
                xb, yb,  y->bounds_half_z * 2.0f,
                xa, ya,  y->bounds_half_z * 2.0f,
                GROUND_COLOR[0], GROUND_COLOR[1], GROUND_COLOR[2], 255);
        }
        ground_quad((flat_after + x1) * 0.5f, 0.0f,
                   (flat_after - x1) * 0.5f, y->bounds_half_z * 2.0f,
                   y->grade.y1, GROUND_COLOR[0], GROUND_COLOR[1],
                   GROUND_COLOR[2], 255);
        return;
    }
    ground_quad(0.0f, 0.0f, y->bounds_half_x * 2.0f,
               y->bounds_half_z * 2.0f, 0.0f,
               GROUND_COLOR[0], GROUND_COLOR[1], GROUND_COLOR[2], 255);
}

static void draw_obstacles(const Yard *y)
{
    int i;
    for (i = 0; i < y->n_obstacles; i++) {
        const YardObstacle *o = &y->obstacles[i];
        const u8 *col = obstacle_color[o->kind % OBSTACLE_KIND_COUNT];
        float ground = yard_ground_at(y, o->box.cx, o->box.cz);
        draw_box(o->box.cx, ground + o->height * 0.5f, o->box.cz,
                o->box.heading, 0.0f, o->box.half_fwd, o->height * 0.5f,
                o->box.half_lat, col[0], col[1], col[2]);
    }
}

static void draw_cones_and_lines(const Yard *y, const YardAttempt *a)
{
    int i;
    for (i = 0; i < y->n_boundaries; i++) {
        const YardBoundary *b = &y->boundaries[i];
        float ground = yard_ground_at(y, (b->ax + b->bx) * 0.5f,
                                      (b->az + b->bz) * 0.5f);
        ground_line(b->ax, b->az, b->bx, b->bz, ground + 0.02f, 0.12f,
                   LINE_COLOR[0], LINE_COLOR[1], LINE_COLOR[2], 235);
    }
    for (i = 0; i < y->n_cones; i++) {
        float ground = yard_ground_at(y, y->cone_x[i], y->cone_z[i]);
        int down = a && a->cone_hit[i];
        draw_cone(y->cone_x[i], ground, y->cone_z[i], down ? 0.28f : 0.22f,
                 down ? 0.20f : 0.42f, CONE_COLOR[0], CONE_COLOR[1],
                 CONE_COLOR[2]);
    }
}

static void draw_target(const Yard *y, const YardAttempt *a)
{
    const YardTarget *t = &y->target;
    const u8 *col = (a && a->in_target) ? TARGET_IN_COLOR : TARGET_COLOR;
    float fx = cosf(t->heading), fz = sinf(t->heading);
    float lx = -fz, lz = fx;
    float ground = yard_ground_at(y, t->cx, t->cz);
    float ax = t->cx + fx * t->half_fwd + lx * t->half_lat;
    float az = t->cz + fz * t->half_fwd + lz * t->half_lat;
    float bx = t->cx + fx * t->half_fwd - lx * t->half_lat;
    float bz = t->cz + fz * t->half_fwd - lz * t->half_lat;
    float cx = t->cx - fx * t->half_fwd - lx * t->half_lat;
    float cz = t->cz - fz * t->half_fwd - lz * t->half_lat;
    float dx = t->cx - fx * t->half_fwd + lx * t->half_lat;
    float dz = t->cz - fz * t->half_fwd + lz * t->half_lat;

    quad(ax, ground + 0.015f, az, bx, ground + 0.015f, bz,
        cx, ground + 0.015f, cz, dx, ground + 0.015f, dz,
        col[0], col[1], col[2], 90);
    ground_line(ax, az, bx, bz, ground + 0.02f, 0.10f, col[0], col[1],
               col[2], 220);
    ground_line(bx, bz, cx, cz, ground + 0.02f, 0.10f, col[0], col[1],
               col[2], 220);
    ground_line(cx, cz, dx, dz, ground + 0.02f, 0.10f, col[0], col[1],
               col[2], 220);
    ground_line(dx, dz, ax, az, ground + 0.02f, 0.10f, col[0], col[1],
               col[2], 220);
}

static void draw_yard(const Yard *y, const YardAttempt *a)
{
    draw_ground(y);
    draw_target(y, a);
    draw_obstacles(y);
    draw_cones_and_lines(y, a);
}
