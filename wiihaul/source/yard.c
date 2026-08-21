/* WiiHaul yards: course geometry, collision and scoring. Plain C99. */
#include <math.h>
#include <string.h>
#include "yard.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define DEG2RAD (float)(M_PI / 180.0)

/* ------------------------------------------------------------------ */
/* OBB geometry                                                       */
/* ------------------------------------------------------------------ */

static void obb_axes(const OBB *b, float *fx, float *fz, float *lx, float *lz)
{
    *fx = cosf(b->heading); *fz = sinf(b->heading);
    *lx = -*fz;             *lz = *fx;
}

static float obb_project_radius(const OBB *b, float ax, float az)
{
    float fx, fz, lx, lz;
    obb_axes(b, &fx, &fz, &lx, &lz);
    return fabsf(fx * ax + fz * az) * b->half_fwd +
          fabsf(lx * ax + lz * az) * b->half_lat;
}

int obb_overlap(const OBB *a, const OBB *b)
{
    float dx = b->cx - a->cx, dz = b->cz - a->cz;
    float afx, afz, alx, alz, bfx, bfz, blx, blz;
    float axes[4][2];
    int i;

    obb_axes(a, &afx, &afz, &alx, &alz);
    obb_axes(b, &bfx, &bfz, &blx, &blz);
    axes[0][0] = afx; axes[0][1] = afz;
    axes[1][0] = alx; axes[1][1] = alz;
    axes[2][0] = bfx; axes[2][1] = bfz;
    axes[3][0] = blx; axes[3][1] = blz;

    for (i = 0; i < 4; i++) {
        float ax = axes[i][0], az = axes[i][1];
        float dist = fabsf(dx * ax + dz * az);
        if (dist > obb_project_radius(a, ax, az) +
                  obb_project_radius(b, ax, az))
            return 0;
    }
    return 1;
}

int obb_circle_overlap(const OBB *a, float cx, float cz, float radius)
{
    float fx, fz, lx, lz;
    float dx = cx - a->cx, dz = cz - a->cz;
    float local_f, local_l, nearx, nearz, ddx, ddz;

    obb_axes(a, &fx, &fz, &lx, &lz);
    local_f = haul_clampf(dx * fx + dz * fz, -a->half_fwd, a->half_fwd);
    local_l = haul_clampf(dx * lx + dz * lz, -a->half_lat, a->half_lat);
    nearx = a->cx + local_f * fx + local_l * lx;
    nearz = a->cz + local_f * fz + local_l * lz;
    ddx = cx - nearx; ddz = cz - nearz;
    return (ddx * ddx + ddz * ddz) <= radius * radius;
}

float obb_signed_distance(const OBB *b, float x, float z)
{
    float fx, fz, lx, lz;
    float dx = x - b->cx, dz = z - b->cz;
    float pf, pl, qf, ql, outf, outl, outside, inside;

    obb_axes(b, &fx, &fz, &lx, &lz);
    pf = dx * fx + dz * fz;
    pl = dx * lx + dz * lz;
    qf = fabsf(pf) - b->half_fwd;
    ql = fabsf(pl) - b->half_lat;
    outf = fmaxf(qf, 0.0f);
    outl = fmaxf(ql, 0.0f);
    outside = sqrtf(outf * outf + outl * outl);
    inside = fminf(fmaxf(qf, ql), 0.0f);
    return outside + inside;
}

int rig_obbs(const Rig *r, const RigSpec *spec, OBB *out, int max_out)
{
    const TruckSpec *ts = &truck_specs[spec->truck_idx];
    int n = 0, i;

    if (n < max_out) {
        float fx = cosf(r->heading), fz = sinf(r->heading);
        float forward_offset = ts->body_length * 0.5f - ts->hitch_setback;
        out[n].cx = r->x + fx * forward_offset;
        out[n].cz = r->z + fz * forward_offset;
        out[n].heading = r->heading;
        out[n].half_fwd = ts->body_length * 0.5f;
        out[n].half_lat = ts->body_width * 0.5f;
        n++;
    }
    for (i = 0; i < spec->n_trailers && n < max_out; i++) {
        const TrailerSpec *tr = &trailer_specs[spec->trailer_idx[i]];
        float px, pz, hx, hz;
        rig_trailer_pos(r, spec, i, &px, &pz);
        rig_hitch_pos(r, spec, i - 1, &hx, &hz);
        out[n].cx = (px + hx) * 0.5f;
        out[n].cz = (pz + hz) * 0.5f;
        out[n].heading = r->trailer_heading[i];
        out[n].half_fwd = tr->length * 0.5f;
        out[n].half_lat = tr->width * 0.5f;
        n++;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* Yard construction helpers                                          */
/* ------------------------------------------------------------------ */

static void add_cone(Yard *y, float x, float z)
{
    if (y->n_cones < YARD_MAX_CONES) {
        y->cone_x[y->n_cones] = x;
        y->cone_z[y->n_cones] = z;
        y->n_cones++;
    }
}

static void add_lane_cones(Yard *y, float x0, float x1, float z,
                           float spacing)
{
    float x;
    for (x = x0; x <= x1 + 0.01f; x += spacing)
        add_cone(y, x, z);
}

static void add_boundary(Yard *y, float ax, float az, float bx, float bz,
                         float nx, float nz)
{
    if (y->n_boundaries < YARD_MAX_BOUNDARIES) {
        YardBoundary *b = &y->boundaries[y->n_boundaries++];
        float len = sqrtf(nx * nx + nz * nz);
        if (len < 1.0e-6f) len = 1.0f;
        b->ax = ax; b->az = az; b->bx = bx; b->bz = bz;
        b->nx = nx / len; b->nz = nz / len;
    }
}

static void add_obstacle(Yard *y, float cx, float cz, float heading,
                         float half_fwd, float half_lat, float height,
                         int kind)
{
    if (y->n_obstacles < YARD_MAX_OBSTACLES) {
        YardObstacle *o = &y->obstacles[y->n_obstacles++];
        o->box.cx = cx; o->box.cz = cz; o->box.heading = heading;
        o->box.half_fwd = half_fwd; o->box.half_lat = half_lat;
        o->height = height; o->kind = kind;
    }
}

/* Every scenario shares these unless overridden. */
static void yard_defaults(Yard *y, int id)
{
    memset(y, 0, sizeof(*y));
    y->id = id;
    y->bounds_half_x = 24.0f;
    y->bounds_half_z = 14.0f;
    y->target.heading_tolerance_deg = 10.0f;
    y->target.hold_seconds = 1.5f;
    y->max_pullups_free = 3;
    y->par_time_s = 60.0f;
    y->difficulty_stars = 1;
    y->min_trailers = 1;
}

/* ------------------------------------------------------------------ */
/* Scenarios                                                           */
/* ------------------------------------------------------------------ */

static void build_straight_back(Yard *y)
{
    strcpy(y->name, "STRAIGHT BACK");
    strcpy(y->blurb, "PULL UP, THEN BACK STRAIGHT INTO THE LANE");
    y->bounds_half_x = 22.0f; y->bounds_half_z = 8.0f;
    y->start_x = 26.0f; y->start_z = 0.0f; y->start_heading = 0.0f;

    add_boundary(y, 26.0f, 1.9f, 0.0f, 1.9f, 0.0f, -1.0f);
    add_boundary(y, 26.0f, -1.9f, 0.0f, -1.9f, 0.0f, 1.0f);
    add_lane_cones(y, 2.0f, 24.0f, 1.9f, 4.0f);
    add_lane_cones(y, 2.0f, 24.0f, -1.9f, 4.0f);

    y->target.cx = 4.0f; y->target.cz = 0.0f; y->target.heading = 0.0f;
    y->target.half_fwd = 7.0f; y->target.half_lat = 1.6f;

    y->max_pullups_free = 2; y->par_time_s = 40.0f; y->difficulty_stars = 1;
}

static void build_offset_back(Yard *y, int left)
{
    /* left/right are relative to the tractor's heading axis, same
     * convention as STEER_LEFT/STEER_RIGHT: +Z is to the right of
     * heading 0, so "left" offsets toward -Z. */
    float side = left ? -1.0f : 1.0f;
    float oz = 4.0f * side;

    strcpy(y->name, left ? "OFFSET BACK LEFT" : "OFFSET BACK RIGHT");
    strcpy(y->blurb, "BACK AROUND THE CORNER INTO THE OFFSET LANE");
    y->bounds_half_x = 22.0f; y->bounds_half_z = 12.0f;
    y->start_x = 26.0f; y->start_z = 0.0f; y->start_heading = 0.0f;

    add_obstacle(y, 15.0f, 2.2f * side, 0.0f, 1.6f, 1.6f, 2.2f,
                OBSTACLE_DUMPSTER);
    add_boundary(y, 12.0f, oz + 1.9f, 0.0f, oz + 1.9f, 0.0f, -1.0f);
    add_boundary(y, 12.0f, oz - 1.9f, 0.0f, oz - 1.9f, 0.0f, 1.0f);
    add_lane_cones(y, 1.0f, 11.0f, oz + 1.9f, 4.0f);
    add_lane_cones(y, 1.0f, 11.0f, oz - 1.9f, 4.0f);

    y->target.cx = 4.0f; y->target.cz = oz; y->target.heading = 0.0f;
    y->target.half_fwd = 6.0f; y->target.half_lat = 1.6f;

    y->max_pullups_free = 3; y->par_time_s = 60.0f; y->difficulty_stars = 2;
}

static void build_parallel_park(Yard *y)
{
    strcpy(y->name, "PARALLEL PARK");
    strcpy(y->blurb, "SET THE RIG ALONGSIDE THE CURB, WHEELS IN");
    y->bounds_half_x = 26.0f; y->bounds_half_z = 10.0f;
    y->start_x = 22.0f; y->start_z = 3.5f; y->start_heading = 0.0f;

    add_obstacle(y, 1.0f, -3.3f, 0.0f, 26.0f, 0.3f, 0.25f, OBSTACLE_CURB);
    add_obstacle(y, -13.0f, -1.5f, 0.0f, 6.0f, 1.4f, 3.6f, OBSTACLE_TRAILER);
    add_obstacle(y, 15.0f, -1.5f, 0.0f, 6.0f, 1.4f, 3.6f, OBSTACLE_TRAILER);
    add_boundary(y, 24.0f, 5.4f, -16.0f, 5.4f, 0.0f, -1.0f);

    y->target.cx = 1.0f; y->target.cz = -1.4f; y->target.heading = 0.0f;
    y->target.half_fwd = 10.5f; y->target.half_lat = 1.7f;

    y->max_pullups_free = 4; y->par_time_s = 75.0f; y->difficulty_stars = 3;
}

static void build_alley_dock(Yard *y)
{
    strcpy(y->name, "ALLEY DOCK");
    strcpy(y->blurb, "SWING WIDE, THEN BACK 90 INTO THE ALLEY");
    y->bounds_half_x = 32.0f; y->bounds_half_z = 30.0f;
    y->start_x = 14.0f; y->start_z = 10.0f; y->start_heading = 0.0f;

    add_obstacle(y, -9.0f, -6.0f, (float)M_PI * 0.5f, 9.0f, 0.4f, 4.2f,
                OBSTACLE_BUILDING);
    add_obstacle(y, -3.0f, -6.0f, (float)M_PI * 0.5f, 9.0f, 0.4f, 4.2f,
                OBSTACLE_BUILDING);
    add_cone(y, -9.0f, 1.5f); add_cone(y, -3.0f, 1.5f);
    add_cone(y, -9.0f, -13.5f); add_cone(y, -3.0f, -13.5f);

    y->target.cx = -6.0f; y->target.cz = -6.0f;
    y->target.heading = (float)M_PI * 0.5f;
    y->target.half_fwd = 7.0f; y->target.half_lat = 1.8f;
    y->target.heading_tolerance_deg = 12.0f;

    y->max_pullups_free = 5; y->par_time_s = 90.0f; y->difficulty_stars = 4;
}

static void build_slalom_pull(Yard *y)
{
    static const float xs[] = { -10.0f, -4.0f, 2.0f, 8.0f, 14.0f, 20.0f };
    unsigned i;

    strcpy(y->name, "CONE SLALOM");
    strcpy(y->blurb, "PULL FORWARD THROUGH THE WEAVE - MIND THE TAIL SWING");
    y->bounds_half_x = 30.0f; y->bounds_half_z = 10.0f;
    y->start_x = -20.0f; y->start_z = 0.0f; y->start_heading = 0.0f;

    for (i = 0; i < sizeof(xs) / sizeof(xs[0]); i++)
        add_cone(y, xs[i], (i % 2 == 0) ? 3.0f : -3.0f);

    y->target.cx = 26.0f; y->target.cz = 0.0f; y->target.heading = 0.0f;
    y->target.half_fwd = 6.0f; y->target.half_lat = 3.0f;
    y->target.heading_tolerance_deg = 20.0f;
    y->target.hold_seconds = 0.5f;

    y->max_pullups_free = 1; y->par_time_s = 40.0f; y->difficulty_stars = 2;
}

static void build_hill_start(Yard *y)
{
    strcpy(y->name, "HILL START");
    strcpy(y->blurb, "HOLD ON THE GRADE, THEN PULL AWAY WITHOUT ROLLING BACK");
    y->bounds_half_x = 26.0f; y->bounds_half_z = 8.0f;
    y->start_x = 5.0f; y->start_z = 0.0f; y->start_heading = 0.0f;

    y->grade.kind = GRADE_RAMP;
    y->grade.x0 = 0.0f; y->grade.x1 = 20.0f;
    y->grade.y0 = 0.0f; y->grade.y1 = 2.6f;

    add_boundary(y, 0.0f, 1.9f, 22.0f, 1.9f, 0.0f, -1.0f);
    add_boundary(y, 0.0f, -1.9f, 22.0f, -1.9f, 0.0f, 1.0f);

    y->target.cx = 16.0f; y->target.cz = 0.0f; y->target.heading = 0.0f;
    y->target.half_fwd = 5.0f; y->target.half_lat = 1.9f;
    y->target.hold_seconds = 2.0f;

    y->max_pullups_free = 2; y->par_time_s = 50.0f; y->difficulty_stars = 3;
}

static void build_haul_route(Yard *y)
{
    strcpy(y->name, "HAUL ROUTE");
    strcpy(y->blurb, "RUN THE CORNERS, THEN DOCK AT THE WAREHOUSE");
    y->bounds_half_x = 50.0f; y->bounds_half_z = 40.0f;
    y->start_x = -36.0f; y->start_z = -20.0f; y->start_heading = 0.0f;

    /* a dog-leg street: east, then a corner north, then a final corner
     * east into the dock — obstacles stand in for the corner buildings */
    add_obstacle(y, -6.0f, -24.0f, 0.0f, 4.0f, 4.0f, 5.0f, OBSTACLE_BUILDING);
    add_obstacle(y, -6.0f, 10.0f, 0.0f, 4.0f, 4.0f, 5.0f, OBSTACLE_BUILDING);
    add_obstacle(y, 22.0f, 24.0f, 0.0f, 6.0f, 4.0f, 5.0f, OBSTACLE_BUILDING);

    add_lane_cones(y, -34.0f, -8.0f, -16.0f, 6.0f);
    add_lane_cones(y, -34.0f, -8.0f, -24.0f, 6.0f);

    y->target.cx = 30.0f; y->target.cz = 16.0f; y->target.heading = 0.0f;
    y->target.half_fwd = 7.0f; y->target.half_lat = 2.0f;
    y->target.heading_tolerance_deg = 14.0f;

    y->max_pullups_free = 6; y->par_time_s = 120.0f; y->difficulty_stars = 3;
}

static void build_doubles_dock(Yard *y)
{
    strcpy(y->name, "DOUBLES DOCK");
    strcpy(y->blurb, "TWO PUPS, ONE DOCK - WATCH THE REAR TRAILER SWING");
    y->bounds_half_x = 26.0f; y->bounds_half_z = 10.0f;
    y->start_x = 28.0f; y->start_z = 0.0f; y->start_heading = 0.0f;
    y->min_trailers = 2;

    add_boundary(y, 28.0f, 2.1f, 0.0f, 2.1f, 0.0f, -1.0f);
    add_boundary(y, 28.0f, -2.1f, 0.0f, -2.1f, 0.0f, 1.0f);
    add_lane_cones(y, 2.0f, 26.0f, 2.1f, 4.0f);
    add_lane_cones(y, 2.0f, 26.0f, -2.1f, 4.0f);

    y->target.cx = 4.0f; y->target.cz = 0.0f; y->target.heading = 0.0f;
    y->target.half_fwd = 6.0f; y->target.half_lat = 1.8f;

    y->max_pullups_free = 4; y->par_time_s = 55.0f; y->difficulty_stars = 4;
}

void yard_init(int scenario_id, Yard *y)
{
    yard_defaults(y, scenario_id);
    switch (scenario_id) {
    case SCENARIO_STRAIGHT_BACK:     build_straight_back(y); break;
    case SCENARIO_OFFSET_BACK_LEFT:  build_offset_back(y, 1); break;
    case SCENARIO_OFFSET_BACK_RIGHT: build_offset_back(y, 0); break;
    case SCENARIO_PARALLEL_PARK:     build_parallel_park(y); break;
    case SCENARIO_ALLEY_DOCK:        build_alley_dock(y); break;
    case SCENARIO_SLALOM_PULL:       build_slalom_pull(y); break;
    case SCENARIO_HILL_START:        build_hill_start(y); break;
    case SCENARIO_HAUL_ROUTE:        build_haul_route(y); break;
    case SCENARIO_DOUBLES_DOCK:      build_doubles_dock(y); break;
    default:                         build_straight_back(y); break;
    }
}

const char *yard_name(int scenario_id)
{
    switch (scenario_id) {
    case SCENARIO_STRAIGHT_BACK:     return "STRAIGHT BACK";
    case SCENARIO_OFFSET_BACK_LEFT:  return "OFFSET BACK LEFT";
    case SCENARIO_OFFSET_BACK_RIGHT: return "OFFSET BACK RIGHT";
    case SCENARIO_PARALLEL_PARK:     return "PARALLEL PARK";
    case SCENARIO_ALLEY_DOCK:        return "ALLEY DOCK";
    case SCENARIO_SLALOM_PULL:       return "CONE SLALOM";
    case SCENARIO_HILL_START:        return "HILL START";
    case SCENARIO_HAUL_ROUTE:        return "HAUL ROUTE";
    case SCENARIO_DOUBLES_DOCK:      return "DOUBLES DOCK";
    default:                         return "?";
    }
}

float yard_ground_at(const Yard *y, float x, float z)
{
    (void)z;
    if (y->grade.kind == GRADE_RAMP) {
        float x0 = y->grade.x0, x1 = y->grade.x1;
        float t;
        if (x <= x0) return y->grade.y0;
        if (x >= x1) return y->grade.y1;
        t = (x - x0) / (x1 - x0);
        return y->grade.y0 + (y->grade.y1 - y->grade.y0) * t;
    }
    return 0.0f;
}

/* ------------------------------------------------------------------ */
/* Scoring                                                             */
/* ------------------------------------------------------------------ */

void yard_attempt_reset(YardAttempt *a)
{
    memset(a, 0, sizeof(*a));
    a->result = ATTEMPT_IN_PROGRESS;
    a->score = 100;
}

const char *yard_grade_name(int grade)
{
    switch (grade) {
    case GRADE_GOLD:   return "GOLD";
    case GRADE_SILVER: return "SILVER";
    case GRADE_BRONZE: return "BRONZE";
    default:           return "NO PASS";
    }
}

static int grade_from_score(int score)
{
    if (score >= 90) return GRADE_GOLD;
    if (score >= 75) return GRADE_SILVER;
    if (score >= 55) return GRADE_BRONZE;
    return GRADE_FAIL_MARK;
}

void yard_attempt_update(YardAttempt *a, const Yard *y, const Rig *r,
                         const RigSpec *spec, float dt)
{
    OBB boxes[1 + MAX_TRAILERS];
    int n_boxes, i, j;
    int all_in_target;

    if (a->result != ATTEMPT_IN_PROGRESS)
        return;

    a->elapsed_t += dt;
    a->cone_event = 0;
    a->boundary_event = 0;

    n_boxes = rig_obbs(r, spec, boxes, 1 + MAX_TRAILERS);

    /* obstacles: any solid contact fails the attempt outright */
    for (i = 0; i < n_boxes && !a->obstacle_hit; i++)
        for (j = 0; j < y->n_obstacles; j++)
            if (obb_overlap(&boxes[i], &y->obstacles[j].box)) {
                a->obstacle_hit = 1;
                break;
            }

    /* cones: knocked over the first time any box touches them */
    for (i = 0; i < y->n_cones; i++) {
        if (a->cone_hit[i]) continue;
        for (j = 0; j < n_boxes; j++) {
            if (obb_circle_overlap(&boxes[j], y->cone_x[i], y->cone_z[i],
                                   0.35f)) {
                a->cone_hit[i] = 1;
                a->cone_hits++;
                a->cone_event = 1;
                break;
            }
        }
    }

    /* boundaries: every box corner must stay on the legal side. Scored
     * on the rising edge only (not-touching -> touching), so leaning on
     * a line for a while costs one penalty, not sixty a second — the
     * same shape as the cone latch above, just per-frame instead of
     * per-attempt since a rig can legitimately cross back and forth. */
    for (i = 0; i < y->n_boundaries; i++) {
        const YardBoundary *b = &y->boundaries[i];
        float dx = b->bx - b->ax, dz = b->bz - b->az;
        float len2 = dx * dx + dz * dz;
        int touching = 0;

        for (j = 0; j < n_boxes && !touching; j++) {
            float fx = cosf(boxes[j].heading), fz = sinf(boxes[j].heading);
            float lx = -fz, lz = fx;
            int c;
            for (c = 0; c < 4 && !touching; c++) {
                float sf = (c & 1) ? 1.0f : -1.0f;
                float sl = (c & 2) ? 1.0f : -1.0f;
                float px = boxes[j].cx + fx * boxes[j].half_fwd * sf +
                          lx * boxes[j].half_lat * sl;
                float pz = boxes[j].cz + fz * boxes[j].half_fwd * sf +
                          lz * boxes[j].half_lat * sl;
                float side = (px - b->ax) * b->nx + (pz - b->az) * b->nz;
                float along = (len2 > 1.0e-6f)
                    ? ((px - b->ax) * dx + (pz - b->az) * dz) / len2
                    : 0.0f;
                if (side < 0.0f && along > -0.1f && along < 1.1f)
                    touching = 1;
            }
        }
        if (touching && !a->boundary_touching[i]) {
            a->boundary_touches++;
            a->boundary_event = 1;
        }
        a->boundary_touching[i] = touching;
    }

    /* target: the last body in the chain (the rearmost trailer, or the
     * tractor itself for a bobtail) has to settle inside the zone —
     * real docking is graded on where the TRAILER ends up, not the
     * tractor, so a target zone is sized around the trailer's own
     * footprint rather than the whole rig's much longer span. Heading
     * is still the tractor's own, since that is what "aligned with the
     * dock" actually means for the unit doing the steering. */
    all_in_target =
        obb_signed_distance(&(OBB){ y->target.cx, y->target.cz,
                                    y->target.heading, y->target.half_fwd,
                                    y->target.half_lat },
                            boxes[n_boxes - 1].cx,
                            boxes[n_boxes - 1].cz) <= 0.05f;
    {
        float dh = fabsf(haul_angle_wrap(r->heading - y->target.heading));
        if (dh > y->target.heading_tolerance_deg * DEG2RAD)
            all_in_target = 0;
        if (fabsf(r->speed) > 0.05f)
            all_in_target = 0;
    }

    a->in_target = all_in_target;
    if (all_in_target) a->in_target_t += dt; else a->in_target_t = 0.0f;

    /* score, live: start at 100, dock a point per cone, two per fresh
     * boundary touch (throttled below), five per pull-up past the free
     * allowance — mirrors how a real skills test is graded */
    {
        int score = 100;
        int extra_pullups = r->pullups - y->max_pullups_free;
        score -= a->cone_hits * 4;
        score -= (a->boundary_touches > 40 ? 40 : a->boundary_touches) * 1;
        if (extra_pullups > 0) score -= extra_pullups * 5;
        if (score < 0) score = 0;
        a->score = score;
    }

    if (a->obstacle_hit) {
        a->result = ATTEMPT_FAIL;
        a->grade = GRADE_FAIL_MARK;
    } else if (a->in_target_t >= y->target.hold_seconds) {
        a->result = ATTEMPT_PASS;
        a->grade = grade_from_score(a->score);
    }
}
