/*
 * menu.inc — scene camera setup and the three menu screens (title,
 * scenario select, rig select) plus the pause overlay. #include'd into
 * main.c — see draw_rig.inc's header.
 */

static float view_aspect(void)
{
    float a = (float)rmode->fbWidth / (float)rmode->efbHeight;
    if (video_widescreen)
        a *= (16.0f / 9.0f) / (4.0f / 3.0f);
    return a;
}

static void draw_scene(float dt)
{
    Mtx view;
    Mtx44 persp;
    guVector eye, up, look;

    (void)dt;
    GX_SetViewport(0.0f, 0.0f, (f32)rmode->fbWidth, (f32)rmode->efbHeight,
                  0.0f, 1.0f);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);

    eye.x = game.cam.x;   eye.y = game.cam.y;   eye.z = game.cam.z;
    up.x = 0.0f;          up.y = 1.0f;          up.z = 0.0f;
    look.x = game.cam.look_x;
    look.y = game.cam.look_y;
    look.z = game.cam.look_z;

    guPerspective(persp, 58.0f, view_aspect(), 0.5f, 900.0f);
    GX_LoadProjectionMtx(persp, GX_PERSPECTIVE);
    guLookAt(view, &eye, &up, &look);
    GX_LoadPosMtxImm(view, GX_PNMTX0);

    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);

    draw_yard(&game.yard, &game.attempt);
    draw_rig(&game.yard, &game.rig, &game.spec);
}

/* ------------------------------------------------------------------ */
/* Menu screens                                                        */
/* ------------------------------------------------------------------ */

static void draw_menu_title(void)
{
    float W = (float)rmode->fbWidth, H = (float)rmode->efbHeight;

    hud_text(W * 0.5f - hud_text_width(26.0f, "WIIHAUL") * 0.5f, 60.0f,
             26.0f, 46.0f, "WIIHAUL", 235, 235, 245, 250);
    hud_text(W * 0.5f - hud_text_width(9.0f, "TRUCK AND TRAILER") * 0.5f,
             120.0f, 9.0f, 15.0f, "TRUCK AND TRAILER", 190, 195, 210, 220);
    hud_text(W * 0.5f - hud_text_width(9.0f, "SKILLS TEST") * 0.5f,
             142.0f, 9.0f, 15.0f, "SKILLS TEST", 190, 195, 210, 220);

    if (menu_msg[0])
        hud_text(W * 0.5f - hud_text_width(9.0f, menu_msg) * 0.5f,
                 H * 0.5f, 9.0f, 15.0f, menu_msg, 235, 200, 90, 235);
    else
        hud_text(W * 0.5f - hud_text_width(9.0f, "PRESS A TO BEGIN") * 0.5f,
                 H * 0.5f, 9.0f, 15.0f, "PRESS A TO BEGIN", 210, 215, 225,
                 230);

    hud_text(24.0f, H - 40.0f, 7.0f, 12.0f, config_banner, 150, 158, 175,
             210);
    if (config_detail[0])
        hud_text(24.0f, H - 24.0f, 6.0f, 10.0f, config_detail, 200, 120,
                 110, 210);
}

static void draw_menu_scenario(void)
{
    float W = (float)rmode->fbWidth, H = (float)rmode->efbHeight;
    float y = 100.0f;
    int i;

    hud_text(W * 0.5f - hud_text_width(16.0f, "SELECT SCENARIO") * 0.5f,
             30.0f, 16.0f, 28.0f, "SELECT SCENARIO", 225, 230, 240, 245);

    for (i = 0; i < SCENARIO_COUNT; i++) {
        Yard y_i;
        int unlocked = career_scenario_unlocked(&career, i);
        u8 r = 190, g = 195, b = 210, a = 210;
        char line[64];

        yard_init(i, &y_i);
        if (i == sel_scenario) { r = 245; g = 220; b = 90; a = 250; }
        if (!unlocked) { r = 90; g = 92; b = 100; a = 180; }

        if (unlocked)
            snprintf(line, sizeof(line), "%s", y_i.name);
        else
            snprintf(line, sizeof(line), "LOCKED");
        hud_text(70.0f, y, 10.0f, 17.0f, line, r, g, b, a);

        if (unlocked && career.attempted[i])
            hud_text(340.0f, y, 8.0f, 14.0f,
                     yard_grade_name(career.best_grade[i]), r, g, b, a);

        y += 30.0f;
    }

    {
        Yard cur;
        yard_init(sel_scenario, &cur);
        hud_text(W * 0.5f - hud_text_width(7.0f, cur.blurb) * 0.5f,
                 H - 70.0f, 7.0f, 12.0f, cur.blurb, 190, 195, 210, 220);
    }

    if (menu_msg[0])
        hud_text(W * 0.5f - hud_text_width(8.0f, menu_msg) * 0.5f,
                 H - 40.0f, 8.0f, 14.0f, menu_msg, 235, 130, 100, 230);
}

static void draw_menu_rig(void)
{
    float W = (float)rmode->fbWidth;
    float y = 100.0f;
    int i;
    Yard scen;

    yard_init(sel_scenario, &scen);
    hud_text(W * 0.5f - hud_text_width(16.0f, "SELECT RIG") * 0.5f,
             30.0f, 16.0f, 28.0f, "SELECT RIG", 225, 230, 240, 245);

    for (i = 0; i < rig_spec_count; i++) {
        char line[80];
        int eligible = rig_specs[i].n_trailers >= scen.min_trailers;
        u8 r = 190, g = 195, b = 210, a = eligible ? 220 : 110;

        if (i == sel_rig && eligible) { r = 245; g = 220; b = 90; a = 250; }

        snprintf(line, sizeof(line), "%s  (%s)", rig_specs[i].name,
                truck_specs[rig_specs[i].truck_idx].name);
        hud_text(70.0f, y, 9.0f, 16.0f, line, r, g, b, a);
        y += 26.0f;
    }

    {
        const RigSpec *rs = &rig_specs[sel_rig];
        char line[64];
        float info_y = y + 20.0f;
        snprintf(line, sizeof(line), "MASS %d LB",
                (int)(rig_spec_total_mass(rs) * KG_TO_LB));
        hud_text(70.0f, info_y, 8.0f, 14.0f, line, 190, 195, 210, 220);
        snprintf(line, sizeof(line), "LENGTH %d FT",
                (int)(rig_spec_total_length(rs) * M_TO_FT));
        hud_text(70.0f, info_y + 20.0f, 8.0f, 14.0f, line, 190, 195, 210,
                 220);
    }
}

static void draw_pause_overlay(void)
{
    float W = (float)rmode->fbWidth, H = (float)rmode->efbHeight;
    static const char *labels[3] = { "RESUME", "RESTART", "QUIT" };
    int i;

    hud_rect(0.0f, 0.0f, W, H, 5, 5, 8, 150);
    hud_text(W * 0.5f - hud_text_width(14.0f, "PAUSED") * 0.5f,
             H * 0.35f, 14.0f, 24.0f, "PAUSED", 230, 230, 240, 245);
    for (i = 0; i < 3; i++) {
        u8 r = (i == pause_choice) ? 245 : 190;
        u8 g = (i == pause_choice) ? 220 : 195;
        u8 b = (i == pause_choice) ? 90 : 210;
        hud_text(W * 0.5f - hud_text_width(11.0f, labels[i]) * 0.5f,
                 H * 0.5f + (float)i * 30.0f, 11.0f, 19.0f, labels[i], r, g,
                 b, 245);
    }
}
