/*
 * frames.inc — the two top-level per-frame functions main()'s loop
 * calls: menu_frame (title/scenario/rig select) and attempt_frame
 * (countdown, live driving, pass/fail, pause). #include'd into main.c
 * — see draw_rig.inc's header.
 */

static void menu_frame(float dt)
{
    (void)dt;
    hud_ortho_fullscreen();

    switch (menu_screen) {
    case SCREEN_TITLE:
        if (menu_activate()) {
            menu_screen = SCREEN_SCENARIO;
            menu_msg[0] = '\0';
        }
        draw_menu_title();
        break;

    case SCREEN_SCENARIO: {
        int d = menu_dcursor();
        if (d) {
            sel_scenario = (sel_scenario + d + SCENARIO_COUNT) %
                          SCENARIO_COUNT;
            menu_msg[0] = '\0';
        }
        if (menu_back()) {
            menu_screen = SCREEN_TITLE;
            menu_msg[0] = '\0';
        }
        if (menu_activate()) {
            if (career_scenario_unlocked(&career, sel_scenario)) {
                Yard sy;
                yard_init(sel_scenario, &sy);
                if (rig_specs[sel_rig].n_trailers < sy.min_trailers) {
                    int i;
                    for (i = 0; i < rig_spec_count; i++)
                        if (rig_specs[i].n_trailers >= sy.min_trailers) {
                            sel_rig = i;
                            break;
                        }
                }
                menu_screen = SCREEN_RIG;
                menu_msg[0] = '\0';
            } else {
                snprintf(menu_msg, sizeof(menu_msg), "PASS THE ONE BEFORE");
            }
        }
        draw_menu_scenario();
        break;
    }

    case SCREEN_RIG: {
        Yard sy;
        int d = menu_dcursor();
        yard_init(sel_scenario, &sy);
        if (d) {
            int tries = 0;
            do {
                sel_rig = (sel_rig + d + rig_spec_count) % rig_spec_count;
                tries++;
            } while (rig_specs[sel_rig].n_trailers < sy.min_trailers &&
                    tries < rig_spec_count);
        }
        if (menu_back())
            menu_screen = SCREEN_SCENARIO;
        if (menu_activate() &&
           rig_specs[sel_rig].n_trailers >= sy.min_trailers) {
            GameConfig cfg;
            memset(&cfg, 0, sizeof(cfg));
            cfg.scenario_id = sel_scenario;
            cfg.rig_idx = sel_rig;
            cfg.settings = &app_settings;
            game_init(&game, &cfg);
            app_state = APP_ATTEMPT;
            pause_open = 0;
        }
        draw_menu_rig();
        break;
    }
    }
}

static int prev_horn = 0;

static void attempt_frame(float dt)
{
    Input in;

    if (pause_open) {
        int d = menu_dcursor();
        if (d) pause_choice = (pause_choice + d + 3) % 3;
        if (menu_back()) pause_open = 0;
        if (menu_activate()) {
            if (pause_choice == 0) {
                pause_open = 0;
            } else if (pause_choice == 1) {
                GameConfig cfg = game.cfg;
                game_init(&game, &cfg);
                pause_open = 0;
            } else {
                app_state = APP_MENU;
                menu_screen = SCREEN_SCENARIO;
                pause_open = 0;
            }
        }
        draw_scene(dt);
        draw_hud_attempt();
        draw_pause_overlay();
        return;
    }

    if (menu_pressed() && game.state != STATE_FINISHED) {
        pause_open = 1;
        pause_choice = 0;
        draw_scene(dt);
        draw_hud_attempt();
        draw_pause_overlay();
        return;
    }

    if (game.state == STATE_FINISHED) {
        if (menu_activate()) {
            GameConfig cfg = game.cfg;
            game_init(&game, &cfg);
        } else if (menu_back()) {
            app_state = APP_MENU;
            menu_screen = SCREEN_SCENARIO;
        }
        draw_scene(dt);
        draw_hud_attempt();
        draw_result_overlay();
        return;
    }

    read_input(&in, dt);
    game_update(&game, &in, dt);

    if (in.horn && !prev_horn) audio_beep(320.0f, 260, 190);
    prev_horn = in.horn;

    if (game.jackknife_event) audio_beep(160.0f, 240, 210);
    if (game.cone_event)      audio_beep(720.0f, 90, 170);
    if (game.boundary_event)  audio_beep(360.0f, 140, 150);
    if (game.pass_event) {
        career_record_attempt(&career, game.cfg.scenario_id,
                              game.attempt.grade);
        audio_beep(880.0f, 300, 215);
    }
    if (game.fail_event) {
        career_record_attempt(&career, game.cfg.scenario_id,
                              GRADE_FAIL_MARK);
        audio_beep(120.0f, 420, 215);
    }

    draw_scene(dt);
    draw_hud_attempt();
}
