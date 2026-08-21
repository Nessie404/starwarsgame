/*
 * hud.inc — the 7-segment-style HUD font (copied verbatim from WiiKart,
 * which is pure GX drawing with no game-specific coupling) plus
 * WiiHaul's own HUD composition: speed, gear, the articulation gauge,
 * score/pull-ups, the scenario banner and the pass/fail result screen.
 * #include'd into main.c — see draw_rig.inc's header.
 */

static int glyph_mask(char c)
{
    switch (c) {
    case '0': return 63;   case '1': return 6;    case '2': return 91;
    case '3': return 79;   case '4': return 102;  case '5': return 109;
    case '6': return 125;  case '7': return 7;    case '8': return 127;
    case '9': return 111;
    case 'A': return 119;  case 'B': return 124;  case 'C': return 57;
    case 'D': return 94;   case 'E': return 121;  case 'F': return 113;
    case 'G': return 61;   case 'H': return 118;  case 'I': return 48;
    case 'J': return 30;
    case 'L': return 56;   case 'N': return 84;   case 'O': return 63;
    case 'P': return 115;  case 'R': return 80;   case 'S': return 109;
    case 'T': return 120;  case 'U': return 62;   case 'Y': return 110;
    case 'Z': return 91;
    case '-': return 64;
    default:  return 0;
    }
}

static void hud_rect(float x, float y, float w, float h,
                     u8 r, u8 g, u8 b, u8 a)
{
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position3f32(x,     y,     -5.0f); GX_Color4u8(r, g, b, a);
    GX_Position3f32(x + w, y,     -5.0f); GX_Color4u8(r, g, b, a);
    GX_Position3f32(x + w, y + h, -5.0f); GX_Color4u8(r, g, b, a);
    GX_Position3f32(x,     y + h, -5.0f); GX_Color4u8(r, g, b, a);
    GX_End();
}

static void hud_diag(float x0, float y0, float x1, float y1, float thick,
                     u8 r, u8 g, u8 b, u8 a)
{
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    float px, py;
    if (len < 0.001f) return;
    px = -dy * thick / (2.0f * len);
    py =  dx * thick / (2.0f * len);
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position3f32(x0 + px, y0 + py, -5.0f); GX_Color4u8(r, g, b, a);
    GX_Position3f32(x1 + px, y1 + py, -5.0f); GX_Color4u8(r, g, b, a);
    GX_Position3f32(x1 - px, y1 - py, -5.0f); GX_Color4u8(r, g, b, a);
    GX_Position3f32(x0 - px, y0 - py, -5.0f); GX_Color4u8(r, g, b, a);
    GX_End();
}

static void hud_glyph(float x, float y, float w, float h, char c,
                      u8 r, u8 g, u8 b, u8 a)
{
    float t = w * 0.22f;
    int m;

    if (c == '.') {
        hud_rect(x, y + h - t, t, t, r, g, b, a);
        return;
    }
    if (c == ':') {
        hud_rect(x, y + h * 0.30f, t, t, r, g, b, a);
        hud_rect(x, y + h * 0.70f, t, t, r, g, b, a);
        return;
    }
    if (c == '/') {
        GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
        GX_Position3f32(x + w - t, y,     -5.0f); GX_Color4u8(r, g, b, a);
        GX_Position3f32(x + w,     y,     -5.0f); GX_Color4u8(r, g, b, a);
        GX_Position3f32(x + t,     y + h, -5.0f); GX_Color4u8(r, g, b, a);
        GX_Position3f32(x,         y + h, -5.0f); GX_Color4u8(r, g, b, a);
        GX_End();
        return;
    }
    if (c == 'X') {
        hud_diag(x + t * 0.5f, y + t * 0.4f,
                 x + w - t * 0.5f, y + h - t * 0.4f,
                 t, r, g, b, a);
        hud_diag(x + w - t * 0.5f, y + t * 0.4f,
                 x + t * 0.5f, y + h - t * 0.4f,
                 t, r, g, b, a);
        return;
    }
    if (c == 'K') {
        hud_rect(x, y, t, h, r, g, b, a);
        hud_diag(x + t * 0.5f, y + h * 0.52f,
                 x + w - t * 0.3f, y + t * 0.4f,
                 t, r, g, b, a);
        hud_diag(x + t * 0.5f, y + h * 0.48f,
                 x + w - t * 0.3f, y + h - t * 0.4f,
                 t, r, g, b, a);
        return;
    }
    if (c == 'M') {
        hud_rect(x, y, t, h, r, g, b, a);
        hud_rect(x + w - t, y, t, h, r, g, b, a);
        hud_diag(x + t * 0.5f, y + t * 0.4f,
                 x + w * 0.5f, y + h * 0.50f,
                 t, r, g, b, a);
        hud_diag(x + w - t * 0.5f, y + t * 0.4f,
                 x + w * 0.5f, y + h * 0.50f,
                 t, r, g, b, a);
        return;
    }
    if (c == 'Q') {
        m = glyph_mask('O');
        if (m & 1)  hud_rect(x + t, y, w - 2.0f * t, t, r, g, b, a);
        if (m & 2)  hud_rect(x + w - t, y + t * 0.5f, t,
                             h * 0.5f - t, r, g, b, a);
        if (m & 4)  hud_rect(x + w - t, y + h * 0.5f + t * 0.5f, t,
                             h * 0.5f - t, r, g, b, a);
        if (m & 8)  hud_rect(x + t, y + h - t, w - 2.0f * t, t,
                             r, g, b, a);
        if (m & 16) hud_rect(x, y + h * 0.5f + t * 0.5f, t,
                             h * 0.5f - t, r, g, b, a);
        if (m & 32) hud_rect(x, y + t * 0.5f, t, h * 0.5f - t,
                             r, g, b, a);
        hud_diag(x + w * 0.55f, y + h * 0.62f,
                 x + w, y + h, t, r, g, b, a);
        return;
    }
    if (c == 'V') {
        hud_diag(x + t * 0.4f, y + t * 0.3f,
                 x + w * 0.5f, y + h - t * 0.3f,
                 t, r, g, b, a);
        hud_diag(x + w - t * 0.4f, y + t * 0.3f,
                 x + w * 0.5f, y + h - t * 0.3f,
                 t, r, g, b, a);
        return;
    }
    if (c == 'W') {
        hud_diag(x + t * 0.3f, y + t * 0.3f,
                 x + w * 0.28f, y + h - t * 0.3f,
                 t, r, g, b, a);
        hud_diag(x + w * 0.28f, y + h - t * 0.3f,
                 x + w * 0.50f, y + h * 0.58f,
                 t, r, g, b, a);
        hud_diag(x + w * 0.50f, y + h * 0.58f,
                 x + w * 0.72f, y + h - t * 0.3f,
                 t, r, g, b, a);
        hud_diag(x + w * 0.72f, y + h - t * 0.3f,
                 x + w - t * 0.3f, y + t * 0.3f,
                 t, r, g, b, a);
        return;
    }

    m = glyph_mask(c);
    if (m & 1)   hud_rect(x + t, y, w - 2.0f * t, t, r, g, b, a);
    if (m & 2)   hud_rect(x + w - t, y + t * 0.5f, t, h * 0.5f - t, r, g, b, a);
    if (m & 4)   hud_rect(x + w - t, y + h * 0.5f + t * 0.5f, t,
                          h * 0.5f - t, r, g, b, a);
    if (m & 8)   hud_rect(x + t, y + h - t, w - 2.0f * t, t, r, g, b, a);
    if (m & 16)  hud_rect(x, y + h * 0.5f + t * 0.5f, t, h * 0.5f - t,
                          r, g, b, a);
    if (m & 32)  hud_rect(x, y + t * 0.5f, t, h * 0.5f - t, r, g, b, a);
    if (m & 64)  hud_rect(x + t, y + h * 0.5f - t * 0.5f, w - 2.0f * t, t,
                          r, g, b, a);
}

static void hud_text(float x, float y, float cw, float ch, const char *s,
                     u8 r, u8 g, u8 b, u8 a)
{
    while (*s) {
        if (*s == '.') {
            hud_glyph(x, y, cw, ch, *s, r, g, b, a);
            x += cw * 0.55f;
        } else {
            hud_glyph(x, y, cw, ch, *s, r, g, b, a);
            x += cw * 1.35f;
        }
        s++;
    }
}

static float hud_text_width(float cw, const char *s)
{
    float w = 0.0f;
    while (*s) {
        w += (*s == '.') ? cw * 0.55f : cw * 1.35f;
        s++;
    }
    return w;
}

/* ------------------------------------------------------------------ */
/* WiiHaul HUD content                                                 */
/* ------------------------------------------------------------------ */

static void hud_ortho_fullscreen(void)
{
    Mtx44 ortho;
    guOrtho(ortho, 0.0f, (f32)rmode->efbHeight, 0.0f, (f32)rmode->fbWidth,
           0.0f, 300.0f);
    GX_LoadProjectionMtx(ortho, GX_ORTHOGRAPHIC);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    {
        Mtx ident;
        guMtxIdentity(ident);
        GX_LoadPosMtxImm(ident, GX_PNMTX0);
    }
}

static const char *jackknife_frac_glyph(float frac)
{
    if (frac >= 0.95f) return "JACKKNIFE";
    if (frac >= 0.55f) return "CAUTION";
    return "";
}

/* the articulation gauge: a horizontal bar, a center tick, and a needle
 * that swings with the worst joint currently on the rig */
static void draw_articulation_gauge(float x, float y, float w, float h)
{
    float worst_signed = 0.0f, worst_frac = 0.0f;
    int i;
    u8 nr = 90, ng = 220, nb = 110;

    for (i = 0; i < game.rig.n_trailers; i++) {
        const TrailerSpec *tr = &trailer_specs[game.spec.trailer_idx[i]];
        float limit = tr->jackknife_limit_deg * ((float)M_PI / 180.0f);
        float artic = rig_articulation(&game.rig, i);
        float frac = (limit > 0.01f) ? fabsf(artic) / limit : 0.0f;
        if (frac > worst_frac) {
            worst_frac = frac;
            worst_signed = (limit > 0.01f) ? artic / limit : 0.0f;
        }
    }
    if (worst_frac >= app_settings.jackknife_danger_frac) { nr = 235; ng = 60; nb = 55; }
    else if (worst_frac >= app_settings.jackknife_warn_frac) { nr = 235; ng = 185; nb = 40; }

    hud_rect(x, y, w, h, 30, 30, 35, 170);
    hud_rect(x + w * 0.5f - 1.5f, y, 3.0f, h, 90, 90, 100, 220);
    {
        float needle_x = x + w * 0.5f + haul_clampf(worst_signed, -1.0f, 1.0f)
                        * (w * 0.5f - 6.0f);
        hud_rect(needle_x - 3.0f, y + 2.0f, 6.0f, h - 4.0f, nr, ng, nb, 255);
    }
    hud_text(x, y - 16.0f, 7.0f, 12.0f, "ARTICULATION", 190, 195, 205, 220);
    {
        const char *warn = jackknife_frac_glyph(worst_frac);
        if (warn[0])
            hud_text(x + w + 10.0f, y - 2.0f, 8.0f, 14.0f, warn, nr, ng, nb,
                    255);
    }
}

static void hud_number(float x, float y, float cw, float ch, int value,
                       u8 r, u8 g, u8 b, u8 a)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", value);
    hud_text(x, y, cw, ch, buf, r, g, b, a);
}

static void draw_hud_attempt(void)
{
    float W = (float)rmode->fbWidth, H = (float)rmode->efbHeight;
    char buf[64];
    int mph = (int)(fabsf(game.rig.speed) * MPS_TO_MPH + 0.5f);

    hud_ortho_fullscreen();

    /* speed + gear/direction, bottom left */
    hud_number(24.0f, H - 74.0f, 20.0f, 34.0f, mph, 230, 235, 245, 250);
    hud_text(24.0f, H - 40.0f, 8.0f, 14.0f, "MPH", 170, 178, 195, 220);
    if (game.rig.jackknifed)
        hud_text(110.0f, H - 66.0f, 12.0f, 20.0f, "STUCK", 235, 90, 80, 255);
    else if (game.rig.speed < -0.05f)
        hud_text(110.0f, H - 66.0f, 14.0f, 24.0f, "R", 235, 180, 60, 255);
    else if (game.rig.speed > 0.05f) {
        hud_number(110.0f, H - 66.0f, 14.0f, 24.0f, game.rig.gear + 1,
                  210, 215, 225, 250);
    } else {
        hud_text(110.0f, H - 66.0f, 12.0f, 20.0f, "N", 170, 178, 195, 220);
    }

    /* articulation gauge, bottom center */
    draw_articulation_gauge(W * 0.5f - 90.0f, H - 54.0f, 180.0f, 20.0f);

    /* score / cones / pull-ups, top left */
    snprintf(buf, sizeof(buf), "SCORE %d", game.attempt.score);
    hud_text(24.0f, 24.0f, 10.0f, 17.0f, buf, 225, 230, 240, 245);
    snprintf(buf, sizeof(buf), "CONES %d", game.attempt.cone_hits);
    hud_text(24.0f, 46.0f, 8.0f, 14.0f, buf, 200, 150, 90, 225);
    snprintf(buf, sizeof(buf), "PULL-UPS %d/%d", game.rig.pullups,
             game.yard.max_pullups_free);
    hud_text(24.0f, 64.0f, 8.0f, 14.0f, buf, 190, 195, 210, 210);

    /* timer, top right — plain seconds, since every scenario here runs
     * well under two minutes at par */
    {
        int secs = (int)game.attempt.elapsed_t;
        int hund = (int)((game.attempt.elapsed_t - (float)secs) * 100.0f);
        snprintf(buf, sizeof(buf), "%d.%02d", secs, hund);
        hud_text(W - 150.0f, 24.0f, 10.0f, 17.0f, buf, 200, 210, 225, 235);
    }

    /* scenario name banner, top center */
    hud_text(W * 0.5f - hud_text_width(9.0f, game.yard.name) * 0.5f, 24.0f,
             9.0f, 15.0f, game.yard.name, 210, 215, 225, 230);

    if (game.state == STATE_COUNTDOWN) {
        int n = (int)ceilf(game.countdown);
        if (n < 1) n = 1;
        hud_number(W * 0.5f - 15.0f, H * 0.5f - 60.0f, 30.0f, 50.0f, n,
                  235, 235, 245, 245);
        hud_text(W * 0.5f - hud_text_width(9.0f, game.yard.blurb) * 0.5f,
                 H * 0.5f + 10.0f, 6.5f, 11.0f, game.yard.blurb,
                 190, 195, 210, 220);
    }
}

static void draw_result_overlay(void)
{
    float W = (float)rmode->fbWidth, H = (float)rmode->efbHeight;
    const char *verdict = (game.attempt.result == ATTEMPT_PASS)
        ? "PASS" : "NO PASS";
    const u8 *vc = (game.attempt.result == ATTEMPT_PASS)
        ? (const u8[3]){ 60, 200, 100 } : (const u8[3]){ 225, 80, 70 };
    char buf[48];

    hud_rect(0.0f, 0.0f, W, H, 5, 5, 8, 150);
    hud_text(W * 0.5f - hud_text_width(24.0f, verdict) * 0.5f, H * 0.3f - 40.0f,
             24.0f, 42.0f, verdict, vc[0], vc[1], vc[2], 255);

    if (game.attempt.result == ATTEMPT_PASS) {
        const char *grade = yard_grade_name(game.attempt.grade);
        hud_text(W * 0.5f - hud_text_width(14.0f, grade) * 0.5f,
                 H * 0.3f + 24.0f, 14.0f, 24.0f, grade, 225, 225, 235, 240);
    }

    snprintf(buf, sizeof(buf), "SCORE %d", game.attempt.score);
    hud_text(W * 0.5f - hud_text_width(11.0f, buf) * 0.5f, H * 0.3f + 66.0f,
             11.0f, 19.0f, buf, 200, 205, 220, 230);

    if (!pause_open) {
        hud_text(W * 0.5f - hud_text_width(8.0f, "A RETRY") * 0.5f,
                 H * 0.68f, 8.0f, 14.0f, "A RETRY", 190, 195, 210, 220);
        hud_text(W * 0.5f - hud_text_width(8.0f, "B MENU") * 0.5f,
                 H * 0.68f + 20.0f, 8.0f, 14.0f, "B MENU", 190, 195, 210, 220);
    }
}
