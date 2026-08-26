/*
 * WiiKart — Wii platform layer.
 *
 * Everything libogc-specific lives here: video / GX setup, flat-shaded
 * 3D rendering (elevation, mountainside skirts, guardrails), menus,
 * up-to-4-player split screen, procedural ASND audio (engine, tire
 * squeal, beeps) and input from Wiimote (tilt / D-pad / Nunchuk /
 * Classic Controller), GameCube pads and USB keyboards.
 *
 * Runs on real hardware via the Homebrew Channel and in the Dolphin
 * emulator (File > Open > wiikart.dol).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <wiikeyboard/keyboard.h>   /* pulls in wsksymdef.h keysyms */
#include <asndlib.h>
#include <ogc/conf.h>
#include <fat.h>

#include "game.h"
#include "config.h"
#include "camera.h"

#define DEFAULT_FIFO_SIZE (256 * 1024)

/* every on-screen speed reads in mph — the sim itself stays in m/s, and
 * spec_top_speed_with_settings (game.c) still returns km/h internally */
#define MPS_TO_MPH 2.23694f
#define KPH_TO_MPH 0.621371f
#define M_TO_FT 3.28084f
#define KG_TO_LB 2.20462f

/* If tilt steering feels inverted on your remote, flip this to -1.
 * (Steering polarity itself lives in game.h: STEER_LEFT/STEER_RIGHT.) */
#define TILT_SIGN (+1.0f)

static void *frameBuffer[2] = { NULL, NULL };
static GXRModeObj *rmode = NULL;
static u32 fb = 0;

static Game game;
static u32 frame_no = 0;
static GameSettings app_settings;
static ControlConfig control_config;
static char config_banner[48];
static char config_detail[48];
/*
 * What actually happened when the JSON was looked for, kept in full so the
 * CONFIG screen can show it. "I am not sure the JSON works" is a fair
 * thing to wonder when the only answer on screen is a three-character
 * banner, so the game now says where it looked, what it found, and what
 * the numbers came out as.
 */
static char config_where[72];              /* the folder it read from   */
static char config_file_status[3][40];     /* settings / cars / controls */
static char config_proof[48];              /* a value you can check      */
/* Where cars.json was actually found, if anywhere — the in-game car
 * designer saves back to this exact path. Empty when there was nothing
 * to find it under (no SD card, or a DOL opened directly with no
 * config folder at all): SAVE fails with a clear reason instead of
 * guessing a location nobody asked for. */
static char cars_json_path[320];

/* app flow */
enum { APP_MENU = 0, APP_RACE = 1 };
static int app_state = APP_MENU;
static int race_exit_confirm;       /* leave-race guard; pauses simulation */
static int menu_screen;              /* SCREEN_SETUP / SCREEN_GARAGE     */
/* Temporary cap: split-screen for 3-4 still works (MAX_HUMANS is
 * unchanged and still sizes every per-player array), but the menu only
 * offers up to 2 for now. Raise this back to MAX_HUMANS to reopen it. */
#define MAX_SELECTABLE_PLAYERS 2
static int sel_players = 1;
static int sel_track = 0;
static int sel_laps = 0;             /* 0 = the circuit's own lap count */
static int sel_weather = WEATHER_TOGGLE_OFF; /* off by default, see game.h */
static int sel_spec[MAX_HUMANS] = { 1, 1, 1, 1 };
static int sel_paint[MAX_HUMANS] = { 0, 1, 2, 3 };
static int sel_gearbox[MAX_HUMANS] = { GEARBOX_AUTO, GEARBOX_AUTO,
                                       GEARBOX_AUTO, GEARBOX_AUTO };
static int sel_tire[MAX_HUMANS] = { TIRE_MEDIUM, TIRE_MEDIUM,
                                    TIRE_MEDIUM, TIRE_MEDIUM };
static Track menu_track;
static int menu_track_loaded = -1;

/* per-player camera + rumble */
static CameraState cam[MAX_HUMANS];
static float rumble_t[MAX_HUMANS];
static float controller_lost_t;      /* grace timer for a dropped pad    */
static int prev_countdown_n = -1;    /* last countdown number beeped     */

/* paint shop: indexes match Kart.paint_idx / GameConfig.paint */
static const u8 paint_palette[PAINT_COUNT][3] = {
    { 220,  45,  45 },   /* RED    */
    {  45,  95, 225 },   /* BLUE   */
    {  40, 175,  75 },   /* GREEN  */
    { 240, 200,  50 },   /* GOLD   */
    { 155,  70, 215 },   /* PURPLE */
    { 235, 130,  40 },   /* ORANGE */
    {  40, 190, 195 },   /* TEAL   */
    { 175, 180, 190 },   /* STEEL  */
};
static const char *paint_names[PAINT_COUNT] = {
    "RED", "BLUE", "GREEN", "GOLD", "PURPLE", "ORANGE", "TEAL", "STEEL"
};

static const u8 *kart_color(const Kart *k)
{
    return paint_palette[k->paint_idx % PAINT_COUNT];
}

static const float LX = 0.45f, LY = 0.85f, LZ = 0.28f;

/* ------------------------------------------------------------------ */
/* Editable configuration                                             */
/* ------------------------------------------------------------------ */

static int readable_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int make_config_path(char *out, int cap, const char *root,
                            const char *name)
{
    int n = snprintf(out, (size_t)cap, "%s/config/%s", root, name);
    return n > 0 && n < cap;
}

/*
 * Look for one config file. Every plausible place a person might have put
 * it is tried, in both the layout the release ships (a config folder
 * beside boot.dol) and the flatter one people tend to improvise (the file
 * dropped straight in). Returns 1 and fills `out` with the first hit.
 */
static int find_config_file(const char *name, char *out, int cap,
                            char *root_out, int root_cap, int argc,
                            char **argv)
{
    char roots[8][160];
    int n = 0, i, layout;

    if (argc > 0 && argv && argv[0] && strchr(argv[0], '/')) {
        char *slash;
        snprintf(roots[n], sizeof(roots[0]), "%s", argv[0]);
        slash = strrchr(roots[n], '/');
        if (slash) { *slash = '\0'; n++; }
    }
    snprintf(roots[n++], sizeof(roots[0]), "sd:/apps/wiikart");
    snprintf(roots[n++], sizeof(roots[0]), "usb:/apps/wiikart");
    snprintf(roots[n++], sizeof(roots[0]), "sd:/wiikart");
    snprintf(roots[n++], sizeof(roots[0]), "usb:/wiikart");
    snprintf(roots[n++], sizeof(roots[0]), "sd:");
    snprintf(roots[n++], sizeof(roots[0]), ".");

    for (i = 0; i < n; i++) {
        for (layout = 0; layout < 2; layout++) {
            int written = layout == 0
                ? snprintf(out, (size_t)cap, "%s/config/%s", roots[i], name)
                : snprintf(out, (size_t)cap, "%s/%s", roots[i], name);
            if (written <= 0 || written >= cap)
                continue;
            if (readable_file(out)) {
                if (root_out && root_cap > 0)
                    snprintf(root_out, (size_t)root_cap, "%s%s", roots[i],
                             layout == 0 ? "/CONFIG" : "");
                return 1;
            }
        }
    }
    out[0] = '\0';
    return 0;
}

static void load_editable_config(int argc, char **argv)
{
    char path[320];
    char error[48];
    int loaded = 0, failed = 0, found = 0, i;

    game_settings_defaults(&app_settings);
    control_config_defaults(&control_config);
    kart_specs_reset_defaults();
    config_banner[0] = config_detail[0] = '\0';
    config_where[0] = config_proof[0] = '\0';
    for (i = 0; i < 3; i++)
        snprintf(config_file_status[i], sizeof(config_file_status[0]),
                 "NOT FOUND");

    /* libfat is optional at runtime. Opening the DOL straight in Dolphin
     * with no SD card still works, on compiled-in defaults; a virtual SD
     * card or a real Wii makes the JSON files editable. */
    if (!fatInitDefault())
        snprintf(config_where, sizeof(config_where), "NO SD CARD FOUND");

    /* the first file that turns up names the folder shown on screen */
#define WHERE_SLOT (found ? NULL : config_where)
#define LOAD_ONE(slot, filename, call)                                     \
    do {                                                                   \
        if (find_config_file(filename, path, (int)sizeof(path),            \
                             WHERE_SLOT, (int)sizeof(config_where),        \
                             argc, argv)) {                                \
            found++;                                                       \
            if (call) {                                                    \
                loaded++;                                                  \
                snprintf(config_file_status[slot],                         \
                         sizeof(config_file_status[0]), "LOADED");         \
            } else {                                                       \
                failed++;                                                  \
                snprintf(config_file_status[slot],                         \
                         sizeof(config_file_status[0]), "%s", error);      \
                if (!config_detail[0])                                     \
                    snprintf(config_detail, sizeof(config_detail), "%s",   \
                             error);                                       \
            }                                                              \
        }                                                                  \
    } while (0)

    LOAD_ONE(0, "settings.json",
             config_load_settings_file(&app_settings, path, error,
                                       (int)sizeof(error)));
    LOAD_ONE(1, "cars.json",
             config_load_cars_file(path, error, (int)sizeof(error)));
    snprintf(cars_json_path, sizeof(cars_json_path), "%s", path);
    LOAD_ONE(2, "controls.json",
             config_load_controls_file(&control_config, path, error,
                                       (int)sizeof(error)));
#undef LOAD_ONE
#undef WHERE_SLOT

    /* something you can check against the file you edited */
    snprintf(config_proof, sizeof(config_proof), "%d CARS  %s %d HP",
             kart_spec_count, kart_specs[0].name,
             (int)kart_specs[0].power_hp);

    if (!found) {
        snprintf(config_banner, sizeof(config_banner), "BUILT IN CONFIG");
        if (!config_where[0])
            snprintf(config_where, sizeof(config_where), "NO JSON FOUND");
    } else if (failed) {
        snprintf(config_banner, sizeof(config_banner), "CONFIG ERROR");
    } else {
        snprintf(config_banner, sizeof(config_banner), "CONFIG %d/3", loaded);
    }
}

/* ------------------------------------------------------------------ */
/* Input state (polled once per frame, consumed per player)            */
/* ------------------------------------------------------------------ */

static u32 wheld[MAX_HUMANS], wdown[MAX_HUMANS];
static u32 gheld[MAX_HUMANS], gdown[MAX_HUMANS];
static u32 gc_mask;      /* connected GameCube pads, from PAD_ScanPads  */

/*
 * Keyboards. Two players share one keyboard, because libwiikeyboard
 * merges every attached keyboard into a single event stream — a second
 * physical keyboard produces the same keysyms, so it cannot be told
 * apart. Players three and four therefore need pads (an Xbox pad shows
 * up as a GameCube pad under Dolphin) or Wii Remotes.
 *
 *   Player 1: W/A/S/D drive, E up a gear, Q down a gear, Space handbrake.
 *   Player 2: I/J/K/L drive, O up a gear, U down a gear, P handbrake.
 *
 * Menus are driven by player 1's WASD, Enter to act on the highlighted
 * row and Esc to back out. Steering signs always come from STEER_LEFT /
 * STEER_RIGHT (see game.h) rather than being written by hand here.
 */
static u8 key_actions[CONTROL_KEYBOARD_PLAYERS][CONTROL_ACTION_COUNT];
static u8 key_held[GAME_KEY_DOWN + 1];
static int keyboard_ok = 0;       /* the driver started */
static int keyboard_here = 0;     /* ...and a keyboard is actually plugged in */
static u8 key_confirm_edge, key_back_edge, key_menu_edge;
/* Menu keys are edges, not held state: sampling held actions every tenth
 * frame dropped most taps outright and made the menus feel broken. */
static u8 key_up_edge, key_down_edge, key_left_edge, key_right_edge;
static float key_repeat_t;        /* held-key auto-repeat timer */
static Input shown_input[MAX_HUMANS];
static unsigned int shown_gc[MAX_HUMANS];

static int normalized_key(u32 symbol)
{
    if (symbol >= KS_a && symbol <= KS_z)
        return 'A' + (int)(symbol - KS_a);
    if (symbol >= KS_A && symbol <= KS_Z)
        return 'A' + (int)(symbol - KS_A);
    switch (symbol) {
    case KS_space:  return GAME_KEY_SPACE;
    case KS_Return: return GAME_KEY_ENTER;
    case KS_Escape: return GAME_KEY_ESCAPE;
    case KS_Left:   return GAME_KEY_LEFT;
    case KS_Right:  return GAME_KEY_RIGHT;
    case KS_Up:     return GAME_KEY_UP;
    case KS_Down:   return GAME_KEY_DOWN;
    default:        return GAME_KEY_NONE;
    }
}

static void remember_key_edge(int player, int action, int held)
{
    if (!held || player != 0)
        return;
    if (action == CONTROL_STEER_LEFT)       key_left_edge = 1;
    else if (action == CONTROL_STEER_RIGHT) key_right_edge = 1;
    else if (action == CONTROL_ACCEL)       key_up_edge = 1;
    else if (action == CONTROL_BRAKE)       key_down_edge = 1;
    else if (action == CONTROL_MENU_CONFIRM) key_confirm_edge = 1;
    else if (action == CONTROL_MENU_BACK)    key_back_edge = 1;
    else if (action == CONTROL_RACE_MENU)    key_menu_edge = 1;
}

static void rebuild_key_actions(void)
{
    int p, action, slot;
    memset(key_actions, 0, sizeof(key_actions));
    for (p = 0; p < CONTROL_KEYBOARD_PLAYERS; p++) {
        for (action = 0; action < CONTROL_ACTION_COUNT; action++) {
            for (slot = 0; slot < CONTROL_MAX_BINDS; slot++) {
                int code = control_config.keyboard[p][action][slot];
                if (code > GAME_KEY_NONE && code <= GAME_KEY_DOWN &&
                    key_held[code]) {
                    key_actions[p][action] = 1;
                    break;
                }
            }
        }
    }
}

static void poll_keyboard(void)
{
    keyboard_event ev;

    if (!keyboard_ok)
        return;
    while (KEYBOARD_GetEvent(&ev)) {
        u8 held;
        if (ev.type == KEYBOARD_CONNECTED) {
            keyboard_here = 1;
            memset(key_held, 0, sizeof(key_held));
            rebuild_key_actions();
            continue;
        }
        if (ev.type == KEYBOARD_DISCONNECTED) {
            keyboard_here = 0;
            memset(key_held, 0, sizeof(key_held));
            rebuild_key_actions();
            continue;
        }
        /* a key event can only come from a keyboard that is present */
        keyboard_here = 1;
        if (ev.type != KEYBOARD_PRESSED && ev.type != KEYBOARD_RELEASED)
            continue;
        held = (ev.type == KEYBOARD_PRESSED);
        {
            int code = normalized_key(ev.symbol);
            int p, action, slot;
            int was_held;
            if (!code)
                continue;
            was_held = key_held[code];
            key_held[code] = (u8)held;
            for (p = 0; p < CONTROL_KEYBOARD_PLAYERS; p++) {
                for (action = 0; action < CONTROL_ACTION_COUNT; action++) {
                    for (slot = 0; slot < CONTROL_MAX_BINDS; slot++) {
                        if (control_config.keyboard[p][action][slot] == code) {
                            remember_key_edge(p, action,
                                              held && !was_held);
                            break;
                        }
                    }
                }
            }
            /* Derive actions from every held physical key. If W and the
             * up arrow are both bound to gas, releasing one must not
             * cancel the other. */
            rebuild_key_actions();
        }
    }
}

static void poll_all_inputs(void)
{
    int p;

    WPAD_ScanPads();
    gc_mask = PAD_ScanPads();
    key_confirm_edge = key_back_edge = key_menu_edge = 0;
    key_up_edge = key_down_edge = key_left_edge = key_right_edge = 0;
    poll_keyboard();

    for (p = 0; p < MAX_HUMANS; p++) {
        wheld[p] = WPAD_ButtonsHeld(p);
        wdown[p] = WPAD_ButtonsDown(p);
        gheld[p] = PAD_ButtonsHeld(p);
        gdown[p] = PAD_ButtonsDown(p);
    }

    /* quitting is deliberate: HOME, Z+Start, or the EXIT row in the menu */
    if ((wdown[0] & (WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME)) ||
        ((gheld[0] & PAD_TRIGGER_Z) && (gdown[0] & PAD_BUTTON_START)))
        exit(0);
}

/* signed x deflection (-1..1, right positive) of a wiiuse joystick */
static float stick_x(const joystick_t *js)
{
    if (js->mag < 0.2f)
        return 0.0f;
    return game_clampf(js->mag, 0.0f, 1.0f) *
           sinf(js->ang * ((float)M_PI / 180.0f));
}

static unsigned int gamecube_button_state(u32 held)
{
    unsigned int state = 0;
    if (held & PAD_BUTTON_A) state |= GC_INPUT_A;
    if (held & PAD_BUTTON_B) state |= GC_INPUT_B;
    if (held & PAD_BUTTON_X) state |= GC_INPUT_X;
    if (held & PAD_BUTTON_Y) state |= GC_INPUT_Y;
    if (held & PAD_TRIGGER_Z) state |= GC_INPUT_Z;
    if (held & PAD_TRIGGER_L) state |= GC_INPUT_L;
    if (held & PAD_TRIGGER_R) state |= GC_INPUT_R;
    if (held & PAD_BUTTON_START) state |= GC_INPUT_START;
    if (held & PAD_BUTTON_LEFT) state |= GC_INPUT_DPAD_LEFT;
    if (held & PAD_BUTTON_RIGHT) state |= GC_INPUT_DPAD_RIGHT;
    if (held & PAD_BUTTON_UP) state |= GC_INPUT_DPAD_UP;
    if (held & PAD_BUTTON_DOWN) state |= GC_INPUT_DPAD_DOWN;
    return state;
}

static unsigned int gamecube_state(int p)
{
    unsigned int state = gamecube_button_state(gheld[p]);
    s8 sx = PAD_StickX(p);
    if (sx < -18) state |= GC_INPUT_STICK_LEFT;
    if (sx > 18) state |= GC_INPUT_STICK_RIGHT;
    return state;
}

static unsigned int gamecube_down_state(int p)
{
    return gamecube_button_state(gdown[p]);
}

static SteerAxis steer_axis[MAX_HUMANS];

/* The GameCube/Xbox handbrake control is a press-to-toggle latch, not a
 * hold: one press engages it, a second press releases it, so a drift can
 * be held through a long corner without pinning a finger on the trigger
 * the whole way. Wiimote/Nunchuk/Classic Controller/keyboard handbrake
 * inputs stay hold-based, matching their own documented "hold B"/"hold
 * SPACE" gesture — only the GameCube-sourced contribution to Input.hop
 * goes through this latch. */
static int gc_hop_prev[MAX_HUMANS];
static int gc_hop_latched[MAX_HUMANS];

static void read_player_input(int p, Input *in, float dt)
{
    const WPADData *wd;
    float steer = 0.0f;
    int tilt_ok = 1;
    int wiimote_manual = game.karts[p].gearbox == GEARBOX_MANUAL;

    memset(in, 0, sizeof(*in));

    /* Wiimote (sideways grip) */
    in->accel = (wheld[p] & (WPAD_BUTTON_2 | WPAD_BUTTON_A)) != 0;
    in->brake = (wheld[p] & WPAD_BUTTON_1) != 0;
    in->hop   = (wheld[p] & WPAD_BUTTON_B) != 0;
    in->boost = (wheld[p] & WPAD_BUTTON_MINUS) != 0;
    if (wiimote_manual) {
        /* Mario Kart Wii has no transmission shift buttons, so WiiKart's
         * optional manual gearbox uses the otherwise-free D-pad vertical
         * pair. Tilt remains the primary steering input; left/right still
         * provide digital steering. */
        if (wheld[p] & WPAD_BUTTON_LEFT)  steer += STEER_LEFT;
        if (wheld[p] & WPAD_BUTTON_RIGHT) steer += STEER_RIGHT;
        in->gear_up |= (wheld[p] & WPAD_BUTTON_UP) != 0;
        in->gear_down |= (wheld[p] & WPAD_BUTTON_DOWN) != 0;
    } else {
        if (wheld[p] & (WPAD_BUTTON_UP | WPAD_BUTTON_LEFT))
            steer += STEER_LEFT;
        if (wheld[p] & (WPAD_BUTTON_DOWN | WPAD_BUTTON_RIGHT))
            steer += STEER_RIGHT;
    }

    /* Wiimote expansions */
    wd = WPAD_Data(p);
    if (wd) {
        if (wd->exp.type == WPAD_EXP_NUNCHUK) {
            steer += stick_x(&wd->exp.nunchuk.js) * STEER_RIGHT;
            if (wheld[p] & (WPAD_NUNCHUK_BUTTON_C | WPAD_NUNCHUK_BUTTON_Z))
                in->hop = 1;
            in->brake |= (wheld[p] & WPAD_BUTTON_B) != 0;
            tilt_ok = 0;
        } else if (wd->exp.type == WPAD_EXP_CLASSIC) {
            steer += stick_x(&wd->exp.classic.ljs) * STEER_RIGHT;
            if (wheld[p] & WPAD_CLASSIC_BUTTON_LEFT)  steer += STEER_LEFT;
            if (wheld[p] & WPAD_CLASSIC_BUTTON_RIGHT) steer += STEER_RIGHT;
            in->accel |= (wheld[p] & (WPAD_CLASSIC_BUTTON_A |
                                      WPAD_CLASSIC_BUTTON_X)) != 0;
            in->brake |= (wheld[p] & (WPAD_CLASSIC_BUTTON_B |
                                      WPAD_CLASSIC_BUTTON_Y)) != 0;
            in->hop   |= (wheld[p] & (WPAD_CLASSIC_BUTTON_FULL_R |
                                      WPAD_CLASSIC_BUTTON_FULL_L)) != 0;
            in->boost |= (wheld[p] & WPAD_CLASSIC_BUTTON_MINUS) != 0;
            tilt_ok = 0;
        }
        if (tilt_ok) {
            /* held sideways, rolling the remote like a wheel shows up as
             * pitch; tipping it clockwise steers right */
            float tilt = TILT_SIGN * wd->orient.pitch;
            if (fabsf(tilt) > 7.0f)
                steer += game_clampf(tilt / 45.0f, -1.0f, 1.0f) * STEER_RIGHT;
        }
    }

    /* GameCube controller (= Xbox pads in Dolphin). controls.json maps
     * these emulated inputs to game actions and records the recommended
     * physical Xbox control beside each one. */
    {
        unsigned int gc = gamecube_state(p);
        s8 sx = PAD_StickX(p);
        shown_gc[p] = gc;
        if (sx < -18 &&
            (gc & control_config.gamecube[CONTROL_STEER_LEFT] &
             GC_INPUT_STICK_LEFT))
            steer += game_clampf((float)sx / 90.0f, -1.0f, 1.0f) *
                     STEER_RIGHT;
        else if (sx > 18 &&
                 (gc & control_config.gamecube[CONTROL_STEER_RIGHT] &
                  GC_INPUT_STICK_RIGHT))
            steer += game_clampf((float)sx / 90.0f, -1.0f, 1.0f) *
                     STEER_RIGHT;
        if (gc & control_config.gamecube[CONTROL_STEER_LEFT] &
                 ~GC_INPUT_STICK_LEFT)
            steer += STEER_LEFT;
        if (gc & control_config.gamecube[CONTROL_STEER_RIGHT] &
                 ~GC_INPUT_STICK_RIGHT)
            steer += STEER_RIGHT;
        in->accel |= (gc & control_config.gamecube[CONTROL_ACCEL]) != 0;
        in->brake |= (gc & control_config.gamecube[CONTROL_BRAKE]) != 0;
        {
            /* press-to-toggle: a fresh press flips the latch, holding
             * the button down does not re-trigger it every frame */
            int raw = (gc & control_config.gamecube[CONTROL_HANDBRAKE]) != 0;
            if (raw && !gc_hop_prev[p])
                gc_hop_latched[p] = !gc_hop_latched[p];
            gc_hop_prev[p] = raw;
            in->hop |= gc_hop_latched[p];
        }
        in->gear_up |= (gc & control_config.gamecube[CONTROL_GEAR_UP]) != 0;
        in->gear_down |=
            (gc & control_config.gamecube[CONTROL_GEAR_DOWN]) != 0;
        in->boost |= (gc & control_config.gamecube[CONTROL_BOOST]) != 0;
    }

    /* USB keyboard bindings come from the same file. */
    if (p < CONTROL_KEYBOARD_PLAYERS) {
        if (key_actions[p][CONTROL_STEER_LEFT])  steer += STEER_LEFT;
        if (key_actions[p][CONTROL_STEER_RIGHT]) steer += STEER_RIGHT;
        in->accel |= key_actions[p][CONTROL_ACCEL];
        in->brake |= key_actions[p][CONTROL_BRAKE];
        in->hop |= key_actions[p][CONTROL_HANDBRAKE];
        in->gear_up |= key_actions[p][CONTROL_GEAR_UP];
        in->gear_down |= key_actions[p][CONTROL_GEAR_DOWN];
        in->boost |= key_actions[p][CONTROL_BOOST];
    }

    /* Classic Controller keeps the familiar shoulder-button shifts. */
    in->gear_up   |= (wheld[p] & WPAD_CLASSIC_BUTTON_ZR) ? 1 : 0;
    in->gear_down |= (wheld[p] & WPAD_CLASSIC_BUTTON_ZL) ? 1 : 0;

    /* every device goes through the virtual stick, so a tapped key and
     * a flicked thumbstick both move the wheel at a believable rate */
    in->steer = steer_axis_update_with_settings(
        &steer_axis[p], game_clampf(steer, -1.0f, 1.0f),
        game.karts[p].speed, dt, &app_settings);
    shown_input[p] = *in;
}

/* ------------------------------------------------------------------ */
/* How many players can actually be fielded                            */
/* ------------------------------------------------------------------ */

/*
 * A player is only playable if that player's own device is present:
 * player 1 may use the USB keyboard, and any player may use the Wii
 * Remote or the GameCube pad on their own channel. Players are numbered
 * from one upward, so the fieldable count is the unbroken run starting
 * at player 1 — two remotes on channels 1 and 3 still only gives one
 * playable player, because player 2 would have nothing to hold.
 */
static int player_has_device(int p)
{
    u32 type;

    if (p < 2 && keyboard_ok && keyboard_here)
        return 1;      /* one keyboard seats two players (WASD + IJKL) */
    if (WPAD_Probe((s32)p, &type) == WPAD_ERR_NONE)
        return 1;
    if (gc_mask & (1u << p))
        return 1;
    return 0;
}

static int available_players(void)
{
    int n = 0;
    while (n < MAX_HUMANS && player_has_device(n))
        n++;
    return n;
}

/* ------------------------------------------------------------------ */
/* Menu buttons, all driven by player 1's devices                      */
/* ------------------------------------------------------------------ */

/*
 * "Activate the highlighted row" is deliberately separate from "change a
 * value" and from "move the cursor". Previously every button press moved
 * the whole menu forward, so pressing A to look at something jumped past
 * it; now A only ever acts on the row under the cursor, and rows that
 * merely hold a setting do nothing at all when activated.
 *
 * Start is not an activate: it is the in-race "back to menu" button and
 * would bounce straight back out.
 */
static int menu_activate(void)
{
    return (wdown[0] & (WPAD_BUTTON_2 | WPAD_BUTTON_A |
                        WPAD_CLASSIC_BUTTON_A)) ||
           (gamecube_down_state(0) &
            control_config.gamecube[CONTROL_MENU_CONFIRM]) ||
           key_confirm_edge;
}

static int menu_back(void)
{
    return (wdown[0] & (WPAD_BUTTON_1 | WPAD_BUTTON_B |
                        WPAD_CLASSIC_BUTTON_B)) ||
           (gamecube_down_state(0) &
            control_config.gamecube[CONTROL_MENU_BACK]) ||
           key_back_edge;
}

/*
 * Keyboard menu keys are taken as edges, plus a slow auto-repeat once a
 * key has been held for a moment. They used to be read straight out of
 * the held-state map on every tenth frame, which meant a normal tap that
 * began and ended between two of those frames did nothing at all — the
 * menus looked frozen even though the game was running fine.
 */
static int key_repeating(void)
{
    return key_repeat_t > 0.32f && (frame_no % 6) == 0;
}

/* change the highlighted row's value: left/right, or A/D on a keyboard */
static int menu_dvalue(void)
{
    int d = 0;
    if (wdown[0] & (WPAD_BUTTON_LEFT | WPAD_CLASSIC_BUTTON_LEFT))  d -= 1;
    if (wdown[0] & (WPAD_BUTTON_RIGHT | WPAD_CLASSIC_BUTTON_RIGHT)) d += 1;
    if (gdown[0] & PAD_BUTTON_LEFT)  d -= 1;
    if (gdown[0] & PAD_BUTTON_RIGHT) d += 1;
    if (key_left_edge ||
        (key_actions[0][CONTROL_STEER_LEFT] && key_repeating())) d -= 1;
    if (key_right_edge ||
        (key_actions[0][CONTROL_STEER_RIGHT] && key_repeating())) d += 1;
    return d;
}

/* move the cursor between rows: up/down, or W/S on a keyboard */
static int menu_dcursor(void)
{
    int d = 0;
    if (wdown[0] & (WPAD_BUTTON_UP | WPAD_CLASSIC_BUTTON_UP))     d -= 1;
    if (wdown[0] & (WPAD_BUTTON_DOWN | WPAD_CLASSIC_BUTTON_DOWN)) d += 1;
    if (gdown[0] & PAD_BUTTON_UP)   d -= 1;
    if (gdown[0] & PAD_BUTTON_DOWN) d += 1;
    if (key_up_edge ||
        (key_actions[0][CONTROL_ACCEL] && key_repeating())) d -= 1;
    if (key_down_edge ||
        (key_actions[0][CONTROL_BRAKE] && key_repeating())) d += 1;
    return d;
}

static int race_to_menu_pressed(void)
{
    int p;
    for (p = 0; p < game.cfg.n_humans; p++) {
        if (wdown[p] & (WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS))
            return 1;
        if (gamecube_down_state(p) &
            control_config.gamecube[CONTROL_RACE_MENU])
            return 1;
    }
    return key_menu_edge;
}

/* The setup menus belong to player one, but an in-race question belongs
 * to whoever opened it.  Let any active racer answer with the same
 * configurable confirm/back actions shown by the input translator. */
static int race_confirm_pressed(void)
{
    int p;
    if (key_confirm_edge)
        return 1;
    for (p = 0; p < game.cfg.n_humans; p++) {
        if (wdown[p] & (WPAD_BUTTON_2 | WPAD_BUTTON_A |
                        WPAD_CLASSIC_BUTTON_A))
            return 1;
        if (gamecube_down_state(p) &
            control_config.gamecube[CONTROL_MENU_CONFIRM])
            return 1;
    }
    return 0;
}

static int race_cancel_pressed(void)
{
    int p;
    if (key_back_edge)
        return 1;
    for (p = 0; p < game.cfg.n_humans; p++) {
        if (wdown[p] & (WPAD_BUTTON_1 | WPAD_BUTTON_B |
                        WPAD_CLASSIC_BUTTON_B))
            return 1;
        if (gamecube_down_state(p) &
            control_config.gamecube[CONTROL_MENU_BACK])
            return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Audio: tiny procedural synth over ASND                              */
/* ------------------------------------------------------------------ */

#define ENGINE_CYCLE 256
#define NOISE_LEN    4096
#define BEEP_LEN     8192

static s16 *engine_buf, *noise_buf, *beep_buf;
static int audio_ok = 0;

static void audio_init(void)
{
    int i;

    ASND_Init();
    ASND_Pause(0);

    engine_buf = (s16 *)memalign(32, ENGINE_CYCLE * sizeof(s16));
    noise_buf  = (s16 *)memalign(32, NOISE_LEN * sizeof(s16));
    beep_buf   = (s16 *)memalign(32, BEEP_LEN * sizeof(s16));
    if (!engine_buf || !noise_buf || !beep_buf)
        return;

    /* engine: one cycle of a buzzy square + sub harmonic */
    for (i = 0; i < ENGINE_CYCLE; i++) {
        float ph = (float)i / ENGINE_CYCLE;
        float v = (ph < 0.5f ? 1.0f : -1.0f) * 0.55f +
                  sinf(ph * 2.0f * (float)M_PI) * 0.30f +
                  sinf(ph * 4.0f * (float)M_PI) * 0.15f;
        engine_buf[i] = (s16)(v * 9000.0f);
    }
    /* white-ish noise for tire squeal / whoosh */
    {
        unsigned seed = 22222u;
        for (i = 0; i < NOISE_LEN; i++) {
            seed = seed * 1664525u + 1013904223u;
            noise_buf[i] = (s16)((int)(seed >> 16) - 32768) / 3;
        }
    }
    DCFlushRange(engine_buf, ENGINE_CYCLE * sizeof(s16));
    DCFlushRange(noise_buf, NOISE_LEN * sizeof(s16));

    ASND_SetInfiniteVoice(0, VOICE_MONO_16BIT, 12000, 0,
                          engine_buf, ENGINE_CYCLE * sizeof(s16), 0, 0);
    ASND_SetInfiniteVoice(1, VOICE_MONO_16BIT, 32000, 0,
                          noise_buf, NOISE_LEN * sizeof(s16), 0, 0);
    audio_ok = 1;
}

static void audio_beep(float freq, int ms, int vol)
{
    int n = 48 * ms;
    int i;
    if (!audio_ok) return;
    if (n > BEEP_LEN) n = BEEP_LEN;
    for (i = 0; i < n; i++) {
        float env = 1.0f - (float)i / (float)n;
        beep_buf[i] = (s16)(sinf(2.0f * (float)M_PI * freq * i / 48000.0f) *
                            10000.0f * env);
    }
    DCFlushRange(beep_buf, n * sizeof(s16));
    ASND_SetVoice(2, VOICE_MONO_16BIT, 48000, 0, beep_buf,
                  n * (s32)sizeof(s16), vol, vol, NULL);
}

static void audio_update(void)
{
    const Kart *k = &game.karts[0];
    float v;
    int pitch, vol;

    if (!audio_ok) return;

    if (app_state != APP_RACE || race_exit_confirm) {
        ASND_ChangeVolumeVoice(0, 0, 0);
        ASND_ChangeVolumeVoice(1, 0, 0);
        return;
    }

    /* engine follows P1's speed; revs rise and fall with velocity */
    v = fabsf(k->speed);
    pitch = (int)(ENGINE_CYCLE * (34.0f + v * 4.4f));      /* Hz * cycle */
    if (pitch > 140000) pitch = 140000;
    vol = 70 + (int)(v * 2.2f);
    if (vol > 170) vol = 170;
    ASND_ChangePitchVoice(0, pitch);
    ASND_ChangeVolumeVoice(0, vol, vol);

    /* tire squeal from slip */
    vol = (int)(k->slip * 170.0f);
    if (vol < 12) vol = 0;
    ASND_ChangeVolumeVoice(1, vol, vol);
}

/* ------------------------------------------------------------------ */
/* Scenery                                                             */
/* ------------------------------------------------------------------ */

#define MAX_TREES 28
static float tree_x[MAX_TREES], tree_z[MAX_TREES], tree_y[MAX_TREES];
static int n_trees = 0;

/*
 * Grandstands (Track.grandstands, currently BULLRING only): unlike trees,
 * these have to actually line up with the road and face it, so they are
 * placed deterministically along genuinely straight stretches (low
 * |curv|) rather than scattered by a PRNG — a grandstand on the outside
 * of a hairpin would read as a mistake, not scenery. Each entry is one
 * unit's centerline anchor plus the track heading there, so drawing can
 * lay the stand out parallel to the road and facing in.
 */
#define MAX_GRANDSTANDS 24
static float gs_x[MAX_GRANDSTANDS], gs_z[MAX_GRANDSTANDS], gs_y[MAX_GRANDSTANDS];
static float gs_yaw[MAX_GRANDSTANDS];
static int gs_side[MAX_GRANDSTANDS];   /* -1 / +1, which side of the road */
static int n_grandstands = 0;

static void place_scenery(const Track *t)
{
    int i;
    unsigned seed = 12345u + (unsigned)t->id * 777u;

    n_trees = 0;
    for (i = 0; i < 240 && n_trees < MAX_TREES; i++) {
        float fx, fz, lat, frac, y;
        int seg;
        seed = seed * 1664525u + 1013904223u;
        fx = t->min_x - 30.0f +
             (t->max_x - t->min_x + 60.0f) * ((seed >> 8) & 0xffff) / 65535.0f;
        seed = seed * 1664525u + 1013904223u;
        fz = t->min_z - 30.0f +
             (t->max_z - t->min_z + 60.0f) * ((seed >> 8) & 0xffff) / 65535.0f;
        track_locate(t, fx, fz, -1, &seg, &frac, &lat, &y);
        if (fabsf(lat) > t->wall_half + 4.0f &&
            fabsf(lat) < t->wall_half + 26.0f) {
            tree_x[n_trees] = fx;
            tree_z[n_trees] = fz;
            tree_y[n_trees] = y - 0.12f * (fabsf(lat) - t->wall_half);
            if (tree_y[n_trees] < t->min_y - 2.0f)
                tree_y[n_trees] = t->min_y - 2.0f;
            n_trees++;
        }
    }

    n_grandstands = 0;
    if (t->grandstands) {
        const float STRAIGHT_CURV = 0.004f;   /* practically zero bend   */
        const int STRIDE = 7;                 /* one unit every ~7 samples */
        int side;
        for (i = 0; i < t->n && n_grandstands + 1 < MAX_GRANDSTANDS; i += STRIDE) {
            float lx, lz, setback;
            if (t->curv[i] >= STRAIGHT_CURV)
                continue;
            lx = -t->dz[i];
            lz = t->dx[i];
            setback = track_wall_half(t, i) + 9.0f;
            for (side = -1; side <= 1 && n_grandstands < MAX_GRANDSTANDS;
                 side += 2) {
                float s = (float)side;
                gs_x[n_grandstands] = t->px[i] + lx * setback * s;
                gs_z[n_grandstands] = t->pz[i] + lz * setback * s;
                gs_y[n_grandstands] = t->py[i];
                gs_yaw[n_grandstands] = atan2f(t->dz[i], t->dx[i]);
                gs_side[n_grandstands] = side;
                n_grandstands++;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Low-level draw helpers                                              */
/* ------------------------------------------------------------------ */

static void quad(float ax, float ay, float az,
                 float bx, float by, float bz,
                 float cx, float cy, float cz,
                 float dx, float dy, float dz,
                 u8 r, u8 g, u8 b, u8 a)
{
    GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
    GX_Position3f32(ax, ay, az); GX_Color4u8(r, g, b, a);
    GX_Position3f32(bx, by, bz); GX_Color4u8(r, g, b, a);
    GX_Position3f32(cx, cy, cz); GX_Color4u8(r, g, b, a);
    GX_Position3f32(dx, dy, dz); GX_Color4u8(r, g, b, a);
    GX_End();
}

static u8 shade(u8 c, float f)
{
    float v = (float)c * f;
    if (v > 255.0f) v = 255.0f;
    if (v < 0.0f) v = 0.0f;
    return (u8)v;
}

/* How far a point at signed lateral offset `lat` sits above or below
 * the centerline once the road is canted by `bank` (Track.bank; same
 * sign convention as lat itself — see the field's comment in game.h).
 * A positive bank tilts the surface down on the positive-lat side (the
 * inside of the turn a positive curvature bends toward) and up on the
 * negative side, the way a real banked corner drops toward the apex. */
static float bank_dy(float lat, float bank)
{
    return -lat * sinf(bank);
}

/* box rotated by yaw (about Y), then pitched (nose up positive), then
 * rolled about its own forward axis (positive = same sign convention as
 * Track.bank: the positive-lateral side dips). roll is applied last so
 * it tilts the already-pitched lateral/up axes rather than the world
 * ones, the way a car's roll rides on top of its pitch. */
static void draw_box(float cx, float cy, float cz, float yaw, float pitch,
                     float roll, float hfw, float hh, float hlat,
                     u8 r, u8 g, u8 b)
{
    float cf = cosf(yaw), sf = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    float cr = cosf(roll), sr = sinf(roll);
    float fx = cf * cp, fy = sp, fz = sf * cp;      /* forward */
    float lx0 = -sf,     ly0 = 0.0f, lz0 = cf;        /* lateral (left) */
    float ux0 = -sp * cf, uy0 = cp, uz0 = -sp * sf;   /* up */
    float lx = lx0 * cr - ux0 * sr, ly = ly0 * cr - uy0 * sr,
          lz = lz0 * cr - uz0 * sr;
    float ux = lx0 * sr + ux0 * cr, uy = ly0 * sr + uy0 * cr,
          uz = lz0 * sr + uz0 * cr;
    float corner[8][3];
    int i;

    for (i = 0; i < 8; i++) {
        float s_f = (i & 1) ? 1.0f : -1.0f;
        float s_l = (i & 2) ? 1.0f : -1.0f;
        float s_u = (i & 4) ? 1.0f : -1.0f;
        corner[i][0] = cx + fx * hfw * s_f + lx * hlat * s_l + ux * hh * s_u;
        corner[i][1] = cy + fy * hfw * s_f + ly * hlat * s_l + uy * hh * s_u;
        corner[i][2] = cz + fz * hfw * s_f + lz * hlat * s_l + uz * hh * s_u;
    }

    {
        float s_top   = 0.55f + 0.45f * fmaxf(0.0f, ux * LX + uy * LY + uz * LZ);
        float s_front = 0.55f + 0.45f * fmaxf(0.0f,  fx * LX + fy * LY + fz * LZ);
        float s_back  = 0.55f + 0.45f * fmaxf(0.0f, -fx * LX - fy * LY - fz * LZ);
        float s_left  = 0.55f + 0.45f * fmaxf(0.0f,  lx * LX + lz * LZ);
        float s_right = 0.55f + 0.45f * fmaxf(0.0f, -lx * LX - lz * LZ);

        quad(corner[4][0], corner[4][1], corner[4][2],
             corner[5][0], corner[5][1], corner[5][2],
             corner[7][0], corner[7][1], corner[7][2],
             corner[6][0], corner[6][1], corner[6][2],
             shade(r, s_top), shade(g, s_top), shade(b, s_top), 255);
        quad(corner[1][0], corner[1][1], corner[1][2],
             corner[3][0], corner[3][1], corner[3][2],
             corner[7][0], corner[7][1], corner[7][2],
             corner[5][0], corner[5][1], corner[5][2],
             shade(r, s_front), shade(g, s_front), shade(b, s_front), 255);
        quad(corner[0][0], corner[0][1], corner[0][2],
             corner[4][0], corner[4][1], corner[4][2],
             corner[6][0], corner[6][1], corner[6][2],
             corner[2][0], corner[2][1], corner[2][2],
             shade(r, s_back), shade(g, s_back), shade(b, s_back), 255);
        quad(corner[2][0], corner[2][1], corner[2][2],
             corner[6][0], corner[6][1], corner[6][2],
             corner[7][0], corner[7][1], corner[7][2],
             corner[3][0], corner[3][1], corner[3][2],
             shade(r, s_left), shade(g, s_left), shade(b, s_left), 255);
        quad(corner[0][0], corner[0][1], corner[0][2],
             corner[1][0], corner[1][1], corner[1][2],
             corner[5][0], corner[5][1], corner[5][2],
             corner[4][0], corner[4][1], corner[4][2],
             shade(r, s_right), shade(g, s_right), shade(b, s_right), 255);
    }
}

static void draw_cone(float cx, float cy_base, float cz,
                      float radius, float height, u8 r, u8 g, u8 b)
{
    const int SEG = 8;
    int i;
    for (i = 0; i < SEG; i++) {
        float a0 = (float)i       * (2.0f * (float)M_PI / SEG);
        float a1 = (float)(i + 1) * (2.0f * (float)M_PI / SEG);
        float sh = 0.62f + 0.30f * fmaxf(0.0f, cosf(a0) * LX + sinf(a0) * LZ);
        GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
        GX_Position3f32(cx, cy_base + height, cz);
        GX_Color4u8(shade(r, sh), shade(g, sh), shade(b, sh), 255);
        GX_Position3f32(cx + cosf(a0) * radius, cy_base, cz + sinf(a0) * radius);
        GX_Color4u8(shade(r, sh), shade(g, sh), shade(b, sh), 255);
        GX_Position3f32(cx + cosf(a1) * radius, cy_base, cz + sinf(a1) * radius);
        GX_Color4u8(shade(r, sh), shade(g, sh), shade(b, sh), 255);
        GX_End();
    }
}

/*
 * Grandstands: three stepped, rising tiers of seating plus a roof over
 * the back tier, laid out with their long axis parallel to the road
 * (see place_scenery). Distance-culled the same way trees are, since a
 * full field of these is a lot more geometry per unit than a tree.
 */
static void draw_grandstands(float vx, float vz)
{
    const int TIERS = 3;
    int i, tier;

    for (i = 0; i < n_grandstands; i++) {
        float ddx = gs_x[i] - vx, ddz = gs_z[i] - vz;
        float lx, lz, s, roof_off, roof_h;

        if (ddx * ddx + ddz * ddz > 220.0f * 220.0f)
            continue;

        lx = -sinf(gs_yaw[i]);
        lz =  cosf(gs_yaw[i]);
        s = (float)gs_side[i];

        for (tier = 0; tier < TIERS; tier++) {
            float depth_off = 3.0f + (float)tier * 3.6f;
            float h = 1.1f + (float)tier * 0.35f;
            u8 rr = (u8)(150 - tier * 14), gg = (u8)(158 - tier * 14),
               bb = (u8)(168 - tier * 10);
            draw_box(gs_x[i] + lx * depth_off * s, gs_y[i] + h,
                     gs_z[i] + lz * depth_off * s, gs_yaw[i], 0.0f, 0.0f,
                     11.0f, h, 3.4f, rr, gg, bb);
        }
        roof_off = 3.0f + (float)(TIERS - 1) * 3.6f + 2.2f;
        roof_h = 1.1f + (float)(TIERS - 1) * 0.35f + 1.4f;
        draw_box(gs_x[i] + lx * roof_off * s, gs_y[i] + roof_h,
                 gs_z[i] + lz * roof_off * s, gs_yaw[i], 0.0f, 0.0f,
                 12.0f, 0.15f, 4.2f, 70, 76, 92);
    }
}

/* ------------------------------------------------------------------ */
/* 3D scene                                                            */
/* ------------------------------------------------------------------ */

/*
 * With a twelve-car field and up to four viewports, drawing every road
 * segment for every player is more immediate-mode geometry than the GX
 * FIFO wants to chew through. Cull to a window of segments around the
 * viewer (wider in one-player, tighter in split screen) and drop distant
 * cars and scenery.
 */
static void view_window(int *ahead, int *behind)
{
    int n = game.cfg.n_humans;
    *behind = 12;
    *ahead = (n <= 1) ? 96 : (n == 2 ? 64 : 44);
}

static int seg_in_window(const Track *t, int viewer_seg, int seg,
                         int ahead, int behind)
{
    int rel = seg - viewer_seg;
    if (rel < -t->n / 2) rel += t->n;
    if (rel >  t->n / 2) rel -= t->n;
    return (rel >= -behind && rel <= ahead);
}

static void draw_track(const Track *t, int viewer_seg, float race_t)
{
    int win_ahead, win_behind;
    /* flat background height the surrounding "ground" sits at; alpine
     * tracks drop it well clear of anything the mountainside geometry
     * does, flat ones keep it close to road height since it reads as
     * infield/apron grass right up against the curb. On a flat track
     * with real banking (BULLRING) the road surface itself dips below
     * this on the inside of a turn and rises above it on the outside,
     * which the per-segment fill quads below account for so neither
     * edge pokes through or leaves a gap against this plane. */
    float floor_y = t->min_y - (t->alpine ? 8.0f : 0.02f);
    int i;

    view_window(&win_ahead, &win_behind);

    /* valley floor */
    {
        u8 r = t->alpine ? 84 : 58, g = t->alpine ? 120 : 142,
           b = t->alpine ? 70 : 60;
        quad(t->min_x - 120.0f, floor_y, t->min_z - 120.0f,
             t->max_x + 120.0f, floor_y, t->min_z - 120.0f,
             t->max_x + 120.0f, floor_y, t->max_z + 120.0f,
             t->min_x - 120.0f, floor_y, t->max_z + 120.0f,
             r, g, b, 255);
    }

    for (i = 0; i < t->n; i++) {
        int in = (i + 1) % t->n;
        float l0x = -t->dz[i],  l0z = t->dx[i];
        float l1x = -t->dz[in], l1z = t->dx[in];
        float y0 = t->py[i] + 0.06f, y1 = t->py[in] + 0.06f;
        /* each strip is drawn between two samples, and the road may be a
         * different width at each of them */
        float rw0 = track_road_half(t, i),  rw1 = track_road_half(t, in);
        float ww0 = track_wall_half(t, i),  ww1 = track_wall_half(t, in);
        float bank0 = t->bank[i], bank1 = t->bank[in];
        u8 r, g, b;

        if (!seg_in_window(t, viewer_seg, i, win_ahead, win_behind))
            continue;

        if (i & 1) {
            r = 95; g = 95; b = 100;
        } else {
            r = 85; g = 85; b = 90;
        }
        /* weather (any circuit, if this race turned it on): a patch
         * reads as bright fresh snow, pale blue-grey ice once it has
         * melted, and a dark wet puddle once that has melted too —
         * see track_weather_at */
        switch (track_weather_at(t, i, race_t, &game.settings)) {
        case WEATHER_SNOW:   r = 235; g = 235; b = 240; break;
        case WEATHER_ICE:    r = 175; g = 205; b = 220; break;
        case WEATHER_PUDDLE: r = 40;  g = 55;  b = 70;  break;
        default: break;
        }

        /* road surface, canted by Track.bank — outer edge (negative lat
         * of a right-hand bend, positive curvature) rides higher, inner
         * edge lower, same as a real banked corner */
        quad(t->px[i]  + l0x * rw0, y0 + bank_dy(rw0, bank0),
             t->pz[i]  + l0z * rw0,
             t->px[in] + l1x * rw1, y1 + bank_dy(rw1, bank1),
             t->pz[in] + l1z * rw1,
             t->px[in] - l1x * rw1, y1 + bank_dy(-rw1, bank1),
             t->pz[in] - l1z * rw1,
             t->px[i]  - l0x * rw0, y0 + bank_dy(-rw0, bank0),
             t->pz[i]  - l0z * rw0,
             r, g, b, 255);

        /* curbs / shoulder stripe */
        if (i & 1) { r = 210; g = 40; b = 40; }
        else       { r = 235; g = 235; b = 235; }
        quad(t->px[i]  + l0x * (rw0 + 0.9f),
             y0 + bank_dy(rw0 + 0.9f, bank0), t->pz[i]  + l0z * (rw0 + 0.9f),
             t->px[in] + l1x * (rw1 + 0.9f),
             y1 + bank_dy(rw1 + 0.9f, bank1), t->pz[in] + l1z * (rw1 + 0.9f),
             t->px[in] + l1x * rw1,
             y1 + bank_dy(rw1, bank1),        t->pz[in] + l1z * rw1,
             t->px[i]  + l0x * rw0,
             y0 + bank_dy(rw0, bank0),        t->pz[i]  + l0z * rw0,
             r, g, b, 255);
        quad(t->px[i]  - l0x * rw0,
             y0 + bank_dy(-rw0, bank0),        t->pz[i]  - l0z * rw0,
             t->px[in] - l1x * rw1,
             y1 + bank_dy(-rw1, bank1),        t->pz[in] - l1z * rw1,
             t->px[in] - l1x * (rw1 + 0.9f),
             y1 + bank_dy(-(rw1 + 0.9f), bank1), t->pz[in] - l1z * (rw1 + 0.9f),
             t->px[i]  - l0x * (rw0 + 0.9f),
             y0 + bank_dy(-(rw0 + 0.9f), bank0), t->pz[i]  - l0z * (rw0 + 0.9f),
             r, g, b, 255);

        if (t->alpine) {
            /* mountainside skirts falling away from the shoulder — the
             * near edge (E0) is the curb's own outer edge, so it picks
             * up the same bank offset the curb quads above used, or
             * there would be a visible seam where they meet; the drop
             * itself (d1/d2 below the road) is left unbanked past that,
             * since a few degrees of cant is lost in a scree slope */
            const float E0 = 0.9f, E1 = 12.0f, E2 = 34.0f;
            float d1 = 7.0f, d2 = 22.0f;
            int side;
            for (side = -1; side <= 1; side += 2) {
                float s = (float)side;
                float e0y0 = y0 + bank_dy((rw0 + E0) * s, bank0) - 0.02f;
                float e0y1 = y1 + bank_dy((rw1 + E0) * s, bank1) - 0.02f;
                u8 rr = 122, gg = 108, bb = 92;   /* rock */
                quad(t->px[i]  + l0x * (rw0 + E0) * s, e0y0,
                     t->pz[i]  + l0z * (rw0 + E0) * s,
                     t->px[in] + l1x * (rw1 + E0) * s, e0y1,
                     t->pz[in] + l1z * (rw1 + E0) * s,
                     t->px[in] + l1x * (rw1 + E1) * s, y1 - d1,
                     t->pz[in] + l1z * (rw1 + E1) * s,
                     t->px[i]  + l0x * (rw0 + E1) * s, y0 - d1,
                     t->pz[i]  + l0z * (rw0 + E1) * s,
                     rr, gg, bb, 255);
                rr = 96; gg = 104; bb = 78;       /* scrub below */
                quad(t->px[i]  + l0x * (rw0 + E1) * s, y0 - d1,
                     t->pz[i]  + l0z * (rw0 + E1) * s,
                     t->px[in] + l1x * (rw1 + E1) * s, y1 - d1,
                     t->pz[in] + l1z * (rw1 + E1) * s,
                     t->px[in] + l1x * (rw1 + E2) * s, y1 - d2,
                     t->pz[in] + l1z * (rw1 + E2) * s,
                     t->px[i]  + l0x * (rw0 + E2) * s, y0 - d2,
                     t->pz[i]  + l0z * (rw0 + E2) * s,
                     rr, gg, bb, 255);
            }
        } else {
            /* Flat tracks have no mountainside, but a real banked corner
             * (BULLRING) tilts the road clear of the flat background
             * ground: the outside curb lifts clear above it, the inside
             * curb dips below it. With nothing between them the ground
             * either pokes through the low side or leaves a gap floating
             * above the high side. This closes both with a short fill
             * from the banked curb edge straight down/up to the flat
             * ground height, using the same green as the ground itself
             * so the join is seamless; on an unbanked stretch both ends
             * land at the same height and the fill is an invisible
             * sliver, so straights are unaffected. */
            int side;
            for (side = -1; side <= 1; side += 2) {
                float s = (float)side;
                float ex0 = (rw0 + 0.9f) * s, ex1 = (rw1 + 0.9f) * s;
                float ey0 = y0 + bank_dy(ex0, bank0);
                float ey1 = y1 + bank_dy(ex1, bank1);
                quad(t->px[i]  + l0x * ex0, ey0, t->pz[i]  + l0z * ex0,
                     t->px[in] + l1x * ex1, ey1, t->pz[in] + l1z * ex1,
                     t->px[in] + l1x * ex1, floor_y, t->pz[in] + l1z * ex1,
                     t->px[i]  + l0x * ex0, floor_y, t->pz[i]  + l0z * ex0,
                     58, 142, 60, 255);
            }
        }
        /* guardrails, where this road has them, independent of terrain
         * (BULLRING and CLASSIC are flat ovals/speedways but still
         * barriered — has_walls, not alpine, is what decides this) */
        if (t->has_walls) {
            float wall0 = ww0 - 0.2f, wall1 = ww1 - 0.2f;
            float rby0 = y0 + bank_dy(wall0, bank0);
            float rby1 = y1 + bank_dy(wall1, bank1);
            float lby0 = y0 + bank_dy(-wall0, bank0);
            float lby1 = y1 + bank_dy(-wall1, bank1);
            u8 rr = 225, gg = 228, bb = 232;
            if (i & 1) { rr = 180; gg = 184; bb = 190; }
            quad(t->px[i]  + l0x * wall0, rby0 + 0.15f,
                 t->pz[i]  + l0z * wall0,
                 t->px[in] + l1x * wall1, rby1 + 0.15f,
                 t->pz[in] + l1z * wall1,
                 t->px[in] + l1x * wall1, rby1 + 0.75f,
                 t->pz[in] + l1z * wall1,
                 t->px[i]  + l0x * wall0, rby0 + 0.75f,
                 t->pz[i]  + l0z * wall0,
                 rr, gg, bb, 255);
            quad(t->px[i]  - l0x * wall0, lby0 + 0.15f,
                 t->pz[i]  - l0z * wall0,
                 t->px[in] - l1x * wall1, lby1 + 0.15f,
                 t->pz[in] - l1z * wall1,
                 t->px[in] - l1x * wall1, lby1 + 0.75f,
                 t->pz[in] - l1z * wall1,
                 t->px[i]  - l0x * wall0, lby0 + 0.75f,
                 t->pz[i]  - l0z * wall0,
                 rr, gg, bb, 255);
        } else if (t->alpine) {
            /* No barrier on a mountainside: mark the edge with a stripe
             * and drop the ground away sharply, so the cliff reads as a
             * cliff. Banked purely at the road edge, same as the skirts
             * — the drop itself does not need to follow the tilt. A
             * flat, unguarded track would have no cliff to speak of, so
             * this stays alpine-only (no such track exists today). */
            float wall0 = ww0, wall1 = ww1;
            int side;
            for (side = -1; side <= 1; side += 2) {
                float sg = (float)side;
                float ey0 = y0 + bank_dy(wall0 * sg, bank0);
                float ey1 = y1 + bank_dy(wall1 * sg, bank1);
                u8 er = (i & 1) ? 235 : 90, eg = (i & 1) ? 180 : 90;
                quad(t->px[i]  + l0x * (wall0 - 0.5f) * sg, ey0 + 0.02f,
                     t->pz[i]  + l0z * (wall0 - 0.5f) * sg,
                     t->px[in] + l1x * (wall1 - 0.5f) * sg, ey1 + 0.02f,
                     t->pz[in] + l1z * (wall1 - 0.5f) * sg,
                     t->px[in] + l1x * wall1 * sg, ey1 + 0.02f,
                     t->pz[in] + l1z * wall1 * sg,
                     t->px[i]  + l0x * wall0 * sg, ey0 + 0.02f,
                     t->pz[i]  + l0z * wall0 * sg,
                     er, eg, 70, 255);
                quad(t->px[i]  + l0x * wall0 * sg, ey0,
                     t->pz[i]  + l0z * wall0 * sg,
                     t->px[in] + l1x * wall1 * sg, ey1,
                     t->pz[in] + l1z * wall1 * sg,
                     t->px[in] + l1x * (wall1 + 1.5f) * sg, y1 - 26.0f,
                     t->pz[in] + l1z * (wall1 + 1.5f) * sg,
                     t->px[i]  + l0x * (wall0 + 1.5f) * sg, y0 - 26.0f,
                     t->pz[i]  + l0z * (wall0 + 1.5f) * sg,
                     84, 74, 64, 255);
            }
        }
    }

    /* start/finish checkers */
    {
        int in = 1;
        float l0x = -t->dz[0], l0z = t->dx[0];
        float l1x = -t->dz[in], l1z = t->dx[in];
        int cx, cz;
        for (cz = 0; cz < 2; cz++) {
            for (cx = 0; cx < 6; cx++) {
                float lw = track_road_half(t, 0);
                float w0 = -lw + 2.0f * lw * (float)cx / 6.0f;
                float w1 = -lw + 2.0f * lw * (float)(cx + 1) / 6.0f;
                float f0 = (float)cz / 2.0f, f1 = (float)(cz + 1) / 2.0f;
                float ax = t->px[0] + (t->px[in] - t->px[0]) * f0;
                float az = t->pz[0] + (t->pz[in] - t->pz[0]) * f0;
                float ay = t->py[0] + (t->py[in] - t->py[0]) * f0 + 0.09f;
                float bx = t->px[0] + (t->px[in] - t->px[0]) * f1;
                float bz = t->pz[0] + (t->pz[in] - t->pz[0]) * f1;
                float by = t->py[0] + (t->py[in] - t->py[0]) * f1 + 0.09f;
                float lax = l0x + (l1x - l0x) * f0, laz = l0z + (l1z - l0z) * f0;
                float lbx = l0x + (l1x - l0x) * f1, lbz = l0z + (l1z - l0z) * f1;
                u8 c = ((cx + cz) & 1) ? 235 : 25;
                quad(ax + lax * w0, ay, az + laz * w0,
                     bx + lbx * w0, by, bz + lbz * w0,
                     bx + lbx * w1, by, bz + lbz * w1,
                     ax + lax * w1, ay, az + laz * w1,
                     c, c, c, 255);
            }
        }
    }

    /* start arch */
    {
        float yaw = atan2f(t->dz[0], t->dx[0]);
        float lx = -t->dz[0], lz = t->dx[0];
        float by = t->py[0];
        float RW = track_road_half(t, 0);
        draw_box(t->px[0] + lx * (RW + 1.8f), by + 2.75f,
                 t->pz[0] + lz * (RW + 1.8f), yaw, 0.0f, 0.0f,
                 0.4f, 2.75f, 0.4f, 225, 225, 230);
        draw_box(t->px[0] - lx * (RW + 1.8f), by + 2.75f,
                 t->pz[0] - lz * (RW + 1.8f), yaw, 0.0f, 0.0f,
                 0.4f, 2.75f, 0.4f, 225, 225, 230);
        draw_box(t->px[0], by + 5.9f, t->pz[0], yaw, 0.0f, 0.0f,
                 0.3f, 0.55f, RW + 2.2f, 200, 30, 30);
    }

    /* trees (near ones only) and the far peaks, which are always drawn
     * because they are the horizon */
    for (i = 0; i < n_trees; i++) {
        float ddx = tree_x[i] - t->px[viewer_seg];
        float ddz = tree_z[i] - t->pz[viewer_seg];
        if (ddx * ddx + ddz * ddz > 150.0f * 150.0f)
            continue;
        draw_box(tree_x[i], tree_y[i] + 0.6f, tree_z[i], 0.0f, 0.0f, 0.0f,
                 0.25f, 0.6f, 0.25f, 110, 75, 40);
        draw_cone(tree_x[i], tree_y[i] + 1.2f, tree_z[i], 1.7f, 3.4f,
                  t->alpine ? 24 : 30, t->alpine ? 100 : 130, 45);
    }
    if (t->grandstands)
        draw_grandstands(t->px[viewer_seg], t->pz[viewer_seg]);
    {
        float base = t->min_y - (t->alpine ? 8.0f : 0.0f);
        float hs = t->alpine ? 2.2f : 1.0f;
        draw_cone(t->min_x - 70.0f, base, t->min_z - 65.0f, 40.0f,
                  30.0f * hs, 130, 130, 145);
        draw_cone(t->max_x + 70.0f, base, t->min_z - 55.0f, 46.0f,
                  38.0f * hs, 120, 120, 135);
        draw_cone(t->max_x + 65.0f, base, t->max_z + 65.0f, 36.0f,
                  26.0f * hs, 135, 135, 150);
        draw_cone(t->min_x - 65.0f, base, t->max_z + 60.0f, 44.0f,
                  34.0f * hs, 125, 125, 140);
    }
}

/* the car itself: body, cabin and four wheels, shared by the race view
 * and the garage turntable */
/*
 * Proportions follow the spec, not just the paint, so two cars with
 * very different numbers read as different cars at a glance and not
 * just a different colour on the same shape: a long wheelbase reads as
 * a long car, a heavy one as broad, a high-drag one as tall and boxy
 * (a low-drag one low and lean), and the driven axle carries visibly
 * bigger tires, the way a real rear- or all-wheel-drive car often
 * does. spec may be NULL (a few call sites have no KartSpec handy),
 * in which case every scale is 1.0 — the original fixed proportions.
 */
/* roll: same sign convention as Track.bank (see game.h) — positive
 * dips the car's positive-lateral (right) side, matching how the road
 * itself cants beneath it on a banked section. 0 on flat ground. */
static void draw_car_model(float cx, float cy, float cz, float yaw,
                           float pitch, float roll, float steer_vis,
                           const u8 col[3], const KartSpec *spec,
                           int braking)
{
    float fx = cosf(yaw), fz = sinf(yaw);
    float lx = -fz, lz = fx;
    float len = spec ? game_clampf(spec->wheelbase / 2.6f, 0.72f, 1.35f)
                      : 1.0f;
    float wid = spec ? game_clampf(powf(spec->mass_kg / 1000.0f, 0.30f),
                                   0.78f, 1.28f)
                      : 1.0f;
    float hgt = spec ? game_clampf(0.82f + spec->cd_a * 0.40f, 0.82f, 1.22f)
                      : 1.0f;
    float track = 0.72f * wid;
    float wheel_fw = 0.85f * len;
    int front_driven = !spec || spec->drivetrain != DRIVETRAIN_RWD;
    int rear_driven = !spec || spec->drivetrain != DRIVETRAIN_FWD;
    int w;

    draw_box(cx, cy + 0.42f * hgt, cz, yaw, pitch, roll,
             1.10f * len, 0.28f * hgt, 0.65f * wid, col[0], col[1], col[2]);
    draw_box(cx - fx * 0.25f * len, cy + 0.92f * hgt, cz - fz * 0.25f * len,
             yaw, pitch, roll, 0.30f * len, 0.26f * hgt, 0.30f * wid,
             40, 40, 45);

    for (w = 0; w < 4; w++) {
        float s_f = (w < 2) ? 1.0f : -1.0f;
        float s_l = (w & 1) ? 1.0f : -1.0f;
        float wyaw = yaw + ((w < 2) ? steer_vis * 0.45f : 0.0f);
        float driven = (s_f > 0.0f) ? (float)front_driven
                                     : (float)rear_driven;
        float wheel_r = 0.28f + 0.05f * driven;
        /* the axle itself rides up or down with roll, same as a point
         * on the road surface offset by the same lateral distance */
        float wy = cy + 0.30f + bank_dy(track * s_l, roll);
        draw_box(cx + fx * wheel_fw * s_f + lx * track * s_l, wy,
                 cz + fz * wheel_fw * s_f + lz * track * s_l,
                 wyaw, 0.0f, roll, wheel_r, wheel_r, 0.14f, 25, 25, 28);
    }

    /* brake lights: a pair of small rear-facing quads, lit bright red on
     * the brake pedal (k->braking — off-throttle engine braking and a
     * FLOOR IT override that cancelled a held brake don't count, same
     * as a real car) and a dim, unlit red the rest of the time. */
    {
        float rx = cx - fx * 1.06f * len, rz = cz - fz * 1.06f * len;
        float ry = cy + 0.42f * hgt;
        u8 lr = braking ? 255 : 60, lg = braking ? 35 : 12, lb = lg;
        int side;
        for (side = 0; side < 2; side++) {
            float s_l = side ? 1.0f : -1.0f;
            float ox = lx * 0.42f * wid * s_l, oz = lz * 0.42f * wid * s_l;
            float wx = lx * 0.09f * wid, wz = lz * 0.09f * wid;
            quad(rx + ox - wx, ry - 0.07f * hgt, rz + oz - wz,
                 rx + ox + wx, ry - 0.07f * hgt, rz + oz + wz,
                 rx + ox + wx, ry + 0.07f * hgt, rz + oz + wz,
                 rx + ox - wx, ry + 0.07f * hgt, rz + oz - wz,
                 lr, lg, lb, 235);
        }
    }
}

static void draw_kart(const Track *t, const Kart *k)
{
    const u8 *col = kart_color(k);
    /* the real body slip angle (v1.28.0's dynamic bicycle model tracks
     * lateral velocity directly now) already looks like a drift crab
     * angle under a handbrake and a gentle cornering lean everywhere
     * else, so one physically real number replaces the old fixed
     * drifting-flag kick plus its separate ad hoc steer/slip fudges */
    float body_slip = game_clampf(atan2f(k->vy, fmaxf(k->speed, 0.5f)),
                                  -1.2f, 1.2f);
    float yaw = k->heading + body_slip;
    float dirdot = cosf(k->heading) * t->dx[k->seg] +
                   sinf(k->heading) * t->dz[k->seg];
    float pitch = atanf(t->slope[k->seg] * dirdot);
    float roll = t->bank[k->seg];
    float fx = cosf(yaw), fz = sinf(yaw);
    float lx = -fz, lz = fx;
    float by = k->y;
    int w;

    if (k->invincible_t > 0.0f) {
        int flash_phase = (int)(game.race_t *
                                game.settings.invincible_flash_hz * 2.0f);
        if ((flash_phase & 1) != 0)
            return;
    }

    /* shadow */
    quad(k->x + fx * 1.3f + lx * 0.85f, by + 0.10f,
         k->z + fz * 1.3f + lz * 0.85f,
         k->x + fx * 1.3f - lx * 0.85f, by + 0.10f,
         k->z + fz * 1.3f - lz * 0.85f,
         k->x - fx * 1.3f - lx * 0.85f, by + 0.10f,
         k->z - fz * 1.3f - lz * 0.85f,
         k->x - fx * 1.3f + lx * 0.85f, by + 0.10f,
         k->z - fz * 1.3f + lz * 0.85f,
         0, 0, 0, 90);

    draw_car_model(k->x, by, k->z, yaw, pitch, roll, k->steer_vis, col,
                   &kart_specs[k->spec], k->braking);

    /* tire smoke when sliding hard */
    if (k->slip > 0.35f && fabsf(k->speed) > 5.0f) {
        u8 sr = 200, sg = 200, sb = 205;
        float jx = 0.15f * sinf((float)frame_no * 1.7f);
        for (w = 0; w < 2; w++) {
            float s_l = w ? 1.0f : -1.0f;
            float sx = k->x - fx * 1.0f + lx * (0.75f * s_l) + jx;
            float sz = k->z - fz * 1.0f + lz * (0.75f * s_l) - jx;
            quad(sx - 0.16f, by + 0.16f, sz - 0.16f,
                 sx + 0.16f, by + 0.16f, sz - 0.16f,
                 sx + 0.16f, by + 0.16f, sz + 0.16f,
                 sx - 0.16f, by + 0.16f, sz + 0.16f,
                 sr, sg, sb, 200);
        }
    }
}

/*
 * Display aspect of one player's viewport. The pixels are square, so the
 * viewport's own shape is the answer at 4:3; on a 16:9 set every pixel is
 * displayed 4/3 wider, and the projection has to say so or the picture is
 * simply stretched.
 */
static int video_widescreen;

static float view_aspect(float vw, float vh)
{
    float a = vw / vh;
    if (video_widescreen)
        a *= (16.0f / 9.0f) / (4.0f / 3.0f);
    return a;
}

static void viewport_rect(int p, int n, float *vx, float *vy,
                          float *vw, float *vh)
{
    float W = (float)rmode->fbWidth, H = (float)rmode->efbHeight;
    if (n <= 1) {
        *vx = 0; *vy = 0; *vw = W; *vh = H;
    } else if (n == 2) {
        *vx = 0; *vw = W; *vh = H * 0.5f;
        *vy = (p == 0) ? 0 : H * 0.5f;
    } else {
        *vw = W * 0.5f; *vh = H * 0.5f;
        *vx = (p & 1) ? W * 0.5f : 0;
        *vy = (p >= 2) ? H * 0.5f : 0;
    }
}

static void draw_scene_for_player(int p, float dt)
{
    Mtx view;
    Mtx44 persp;
    guVector eye, up, look;
    const Track *t = &game.track;
    const Kart *k = &game.karts[p];
    float vx, vy, vw, vh;
    int i;

    /* the camera itself lives in camera.c, where it can be tested */
    camera_update(&cam[p], &app_settings, k, t, dt);

    eye.x = cam[p].x;   eye.y = cam[p].y;   eye.z = cam[p].z;
    up.x  = 0.0f;       up.y  = 1.0f;       up.z  = 0.0f;
    look.x = cam[p].look_x;
    look.y = cam[p].look_y;
    look.z = cam[p].look_z;

    viewport_rect(p, game.cfg.n_humans, &vx, &vy, &vw, &vh);
    GX_SetViewport(vx, vy, vw, vh, 0.0f, 1.0f);
    GX_SetScissor((u32)vx, (u32)vy, (u32)vw, (u32)vh);

    guPerspective(persp, 58.0f, view_aspect(vw, vh), 0.5f, 900.0f);
    GX_LoadProjectionMtx(persp, GX_PERSPECTIVE);
    guLookAt(view, &eye, &up, &look);
    GX_LoadPosMtxImm(view, GX_PNMTX0);

    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);

    draw_track(t, k->seg, game.race_t);
    for (i = 0; i < NUM_KARTS; i++) {
        const Kart *o = &game.karts[i];
        float ddx = o->x - cam[p].x;
        float ddz = o->z - cam[p].z;
        if (o != k && ddx * ddx + ddz * ddz > 220.0f * 220.0f)
            continue;              /* a speck at this range */
        draw_kart(t, o);
    }
}

/* ------------------------------------------------------------------ */
/* HUD: 7-segment glyphs                                               */
/* ------------------------------------------------------------------ */

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

/*
 * World +X maps to screen +X (right) and world +Z maps to screen +Y
 * (down, since this is drawn in the top-left-origin HUD ortho set up by
 * hud_ortho_fullscreen). That is the non-mirrored mapping: the same
 * cross(forward, up) = right convention the v1.0.1 steering fix verified
 * against guLookAt says a rightward offset at heading (dx,dz) = (1,0) is
 * +Z, and +Z has to land below the line on screen for "right of an
 * eastward road" to actually read as south/down rather than mirrored
 * north/up. Mapping +Z to screen "up" instead (i.e. flipping this sign)
 * mirrors the whole minimap left-for-right relative to the real track.
 *
 * `highlight` picks who gets the pulsing ring: a kart index rings just
 * that one car (the 1P corner map's own kart), -1 rings nobody (the 3P
 * spare-quadrant map, shared by three different people with no single
 * "the player" among them), and -2 rings every human-controlled kart
 * at once (the 2P shared map: both players are looking at the same
 * screen, so both get picked out of the field together).
 */
static void draw_minimap(const Track *t, int with_karts, int highlight,
                         float ox, float oy, float size)
{
    float span_x = t->max_x - t->min_x;
    float span_z = t->max_z - t->min_z;
    float span = (span_x > span_z) ? span_x : span_z;
    float scale = size / span;
    int i;

    GX_SetLineWidth(14, GX_TO_ZERO);
    GX_Begin(GX_LINESTRIP, GX_VTXFMT0, (u16)(t->n + 1));
    for (i = 0; i <= t->n; i++) {
        int j = i % t->n;
        GX_Position3f32(ox + (t->px[j] - t->min_x) * scale,
                        oy + (t->pz[j] - t->min_z) * scale, -5.0f);
        GX_Color4u8(240, 240, 240, 200);
    }
    GX_End();

    /*
     * The finish line, drawn across the road at segment 0: a black and
     * white gate with a flag beside it, so the minimap says where the lap
     * ends rather than leaving you to guess which bend is the last one.
     */
    {
        float lx = -t->dz[0], lz = t->dx[0];
        float half = track_road_half(t, 0) * 1.9f;   /* readable at this
                                                      * scale rather than
                                                      * true to width */
        float ax = ox + (t->px[0] + lx * half - t->min_x) * scale;
        float ay = oy + ((t->pz[0] + lz * half) - t->min_z) * scale;
        float bx = ox + (t->px[0] - lx * half - t->min_x) * scale;
        float by = oy + ((t->pz[0] - lz * half) - t->min_z) * scale;
        int c;

        for (c = 0; c < 4; c++) {
            float f0 = (float)c / 4.0f, f1 = (float)(c + 1) / 4.0f;
            u8 v = (c & 1) ? 25 : 245;
            float x0 = ax + (bx - ax) * f0, y0 = ay + (by - ay) * f0;
            float x1 = ax + (bx - ax) * f1, y1 = ay + (by - ay) * f1;
            float w = fabsf(x1 - x0) + 2.5f, h = fabsf(y1 - y0) + 2.5f;
            hud_rect((x0 < x1 ? x0 : x1) - 1.0f, (y0 < y1 ? y0 : y1) - 1.0f,
                     w, h, v, v, v, 255);
        }
        /* a little flag on the pole at one end */
        hud_rect(ax - 1.0f, ay - 9.0f, 2.0f, 9.0f, 235, 235, 240, 235);
        hud_rect(ax + 1.0f, ay - 9.0f, 6.0f, 4.0f, 235, 60, 60, 235);
    }

    if (!with_karts)
        return;
    for (i = NUM_KARTS - 1; i >= 0; i--) {
        const Kart *k = &game.karts[i];
        const u8 *c = kart_color(k);
        float mx = ox + (k->x - t->min_x) * scale;
        float my = oy + (k->z - t->min_z) * scale;
        float s = (k->human >= 0) ? 5.0f : 4.0f;
        if (i == highlight || (highlight == -2 && k->human >= 0)) {
            /* the viewer's own car: a pulsing white ring behind its own
             * marker, so picking it out of eleven other dots does not
             * mean reading colours against the clock mid-corner */
            float pulse = 2.0f + 1.4f * sinf(game.race_t * 6.0f);
            float ring = s + 5.0f + pulse;
            hud_rect(mx - ring * 0.5f, my - ring * 0.5f, ring, ring,
                     255, 255, 255, 230);
            s += 2.0f;
        }
        hud_rect(mx - s * 0.5f, my - s * 0.5f, s, s, c[0], c[1], c[2], 255);
    }
}

static void hud_ortho_fullscreen(void)
{
    Mtx44 ortho;
    Mtx ident;
    float W = (float)rmode->fbWidth;
    float H = (float)rmode->efbHeight;

    GX_SetViewport(0.0f, 0.0f, W, H, 0.0f, 1.0f);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    guOrtho(ortho, 0.0f, H, 0.0f, W, 0.0f, 300.0f);
    GX_LoadProjectionMtx(ortho, GX_ORTHOGRAPHIC);
    guMtxIdentity(ident);
    GX_LoadPosMtxImm(ident, GX_PNMTX0);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
}

/* label for a car: players are P1..P4, AI are named by strategy so the
 * field reads as a grid of characters rather than "CPU 1..11" */
static const char *kart_label(const Kart *k)
{
    static char buf[8];
    if (k->human >= 0) {
        snprintf(buf, sizeof(buf), "P%d", k->human + 1);
        return buf;
    }
    return ai_driver_name(k->driver_no);
}

static const Kart *kart_at_rank(int rank)
{
    int i;
    for (i = 0; i < NUM_KARTS; i++)
        if (game.karts[i].rank == rank)
            return &game.karts[i];
    return NULL;
}

/* m:ss.hh, or "--" before there is a time to show */
/* how many laps the selected circuit would run on its own, for the
 * AUTO readout on the LAPS row */
static int menu_track_laps(void)
{
    if (menu_track_loaded != sel_track) {
        track_init_with_settings(&menu_track, sel_track, &app_settings);
        menu_track_loaded = sel_track;
    }
    return menu_track.laps;
}

static void format_lap_time(char *out, int cap, float seconds)
{
    int m, sec, hun;
    if (!(seconds > 0.0f)) {
        snprintf(out, cap, "--.--");
        return;
    }
    m = (int)(seconds / 60.0f);
    sec = (int)seconds - m * 60;
    hun = (int)((seconds - (float)((int)seconds)) * 100.0f);
    if (m > 0)
        snprintf(out, cap, "%d.%02d.%02d", m, sec, hun);
    else
        snprintf(out, cap, "%d.%02d", sec, hun);
}

/*
 * Crossing the line puts a short note in that player's own viewport —
 * their lap time, and whether it was their best — rather than a banner
 * across a split screen belonging to somebody else.
 */
#define LAP_POPUP_SECONDS 2.6f
static float lap_popup_t[MAX_HUMANS];
static char  lap_popup_time[MAX_HUMANS][16];
static int   lap_popup_lap[MAX_HUMANS];
static int   lap_popup_best[MAX_HUMANS];

static void lap_popups_update(float dt)
{
    int p;
    for (p = 0; p < MAX_HUMANS; p++) {
        if (lap_popup_t[p] > 0.0f)
            lap_popup_t[p] -= dt;
        if (p < game.cfg.n_humans && game.karts[p].lap_event) {
            const Kart *k = &game.karts[p];
            format_lap_time(lap_popup_time[p], sizeof(lap_popup_time[p]),
                            k->last_lap_time);
            lap_popup_lap[p] = k->laps_done;
            lap_popup_best[p] = k->lap_best_event;
            lap_popup_t[p] = LAP_POPUP_SECONDS;
            audio_beep(k->lap_best_event ? 990.0f : 660.0f, 110,
                       k->lap_best_event ? 170 : 150);
        }
    }
}

static void draw_lap_popup(int p, float vx, float vy, float vw, float vh)
{
    float a, y;
    char line[28];
    u8 tint_r, tint_g, tint_b;

    if (lap_popup_t[p] <= 0.0f)
        return;

    /* fades out over its last half second */
    a = lap_popup_t[p] > 0.5f ? 1.0f : lap_popup_t[p] * 2.0f;
    y = vy + vh * 0.30f;
    tint_r = lap_popup_best[p] ? 255 : 235;
    tint_g = lap_popup_best[p] ? 225 : 235;
    tint_b = lap_popup_best[p] ?  80 : 245;

    snprintf(line, sizeof(line), "LAP %d", lap_popup_lap[p]);
    hud_text(vx + vw * 0.5f - hud_text_width(11.0f, line) * 0.5f, y,
             11.0f, 19.0f, line, 200, 205, 220, (u8)(210.0f * a));
    hud_text(vx + vw * 0.5f -
             hud_text_width(16.0f, lap_popup_time[p]) * 0.5f, y + 22.0f,
             16.0f, 28.0f, lap_popup_time[p], tint_r, tint_g, tint_b,
             (u8)(245.0f * a));
    if (lap_popup_best[p])
        hud_text(vx + vw * 0.5f - hud_text_width(10.0f, "BEST LAP") * 0.5f,
                 y + 54.0f, 10.0f, 17.0f, "BEST LAP", 255, 225, 80,
                 (u8)(235.0f * a));
}

/*
 * Where the line is, from the car's point of view. The distance to it is
 * always worth knowing on the last lap, and worth knowing at all once you
 * are close, so the marker fades in over the final stretch: a checkered
 * flag, the distance, and FINAL LAP when this is the one that counts.
 */
static void draw_finish_marker(int p, float vx, float vy, float vw,
                               float vh)
{
    const Kart *k = &game.karts[p];
    const Track *t = &game.track;
    float m_per_seg = t->total_len / (float)t->n;
    float to_line = ((float)t->n - k->prog_raw) * m_per_seg;
    int final_lap = (k->lap + 1 >= t->laps);
    float show_from = final_lap ? 400.0f : 220.0f;
    float x, y, a;
    char buf[24];
    int c;

    if (game.state != STATE_RACING || k->finished)
        return;
    if (to_line < 0.0f)
        to_line += t->total_len;              /* just crossed it */
    if (to_line > show_from)
        return;

    a = 1.0f - (to_line / show_from) * 0.45f;   /* firms up as it nears */
    x = vx + vw * 0.5f - 46.0f;
    y = vy + vh * 0.18f;

    /* a small checkered flag, drawn as squares */
    for (c = 0; c < 8; c++) {
        int col = c & 3, row = (c >> 2) & 1;
        u8 v = ((col + row) & 1) ? 30 : 240;
        hud_rect(x + (float)col * 5.0f, y + (float)row * 5.0f, 5.0f, 5.0f,
                 v, v, v, (u8)(235.0f * a));
    }

    snprintf(buf, sizeof(buf), "%d FT", (int)(to_line * M_TO_FT));
    hud_text(x + 26.0f, y - 1.0f, 10.0f, 17.0f, buf,
             final_lap ? 255 : 225, final_lap ? 225 : 230,
             final_lap ? 90 : 240, (u8)(240.0f * a));
    if (final_lap)
        hud_text(vx + vw * 0.5f - hud_text_width(9.0f, "FINAL LAP") * 0.5f,
                 y + 16.0f, 9.0f, 15.0f, "FINAL LAP", 255, 210, 70,
                 (u8)(235.0f * a));
}

/*
 * The wrong-way marshal: a helicopter over the car's own viewport, saying
 * what it would actually shout. The real penalty is the engine cut in
 * kart_step (see game.c) — this is just telling the driver why the car
 * has gone soft.
 */
static void draw_wrong_way_marker(int p, float vx, float vy, float vw,
                                  float vh)
{
    const Kart *k = &game.karts[p];
    float cx, cy, spin, flash;
    int i;

    if (game.state != STATE_RACING || !k->wrong_way)
        return;

    cx = vx + vw * 0.5f;
    cy = vy + vh * 0.22f;
    spin = game.race_t * 14.0f;              /* main rotor, radians/s   */
    flash = 0.55f + 0.45f * sinf(game.race_t * 6.0f);

    /* fuselage, tail boom, skids — flat shapes, same style as everything
     * else on this HUD */
    hud_rect(cx - 16.0f, cy - 7.0f, 32.0f, 14.0f, 235, 60, 45, 235);
    hud_rect(cx + 14.0f, cy - 2.5f, 20.0f, 5.0f, 235, 60, 45, 235);
    hud_rect(cx - 14.0f, cy + 8.0f, 28.0f, 2.5f, 40, 20, 20, 220);

    /* main rotor, spinning; tail rotor, spinning faster and fading in
     * and out the way a fast-spinning blade actually reads at this
     * resolution */
    for (i = 0; i < 2; i++) {
        float a = spin + (float)i * 3.14159265f;
        hud_diag(cx - cosf(a) * 26.0f, cy - 12.0f - sinf(a) * 6.0f,
                 cx + cosf(a) * 26.0f, cy - 12.0f + sinf(a) * 6.0f,
                 2.0f, 235, 235, 235, 200);
    }
    hud_diag(cx + 33.0f, cy - 6.0f, cx + 33.0f, cy + 3.0f, 1.5f,
             225, 225, 225, (u8)(200.0f * (0.5f + 0.5f * cosf(spin * 3.0f))));

    hud_text(cx - hud_text_width(11.0f, "TURN AROUND") * 0.5f, cy + 18.0f,
             11.0f, 19.0f, "TURN AROUND", 255, 90, 70, (u8)(245.0f * flash));
}

/*
 * The order of the race, live. It sits in a narrow column down the right
 * edge so it never covers the road: rank, driver, and the gap to the car
 * in front expressed as time, which is what a driver actually wants.
 * Split screens only get the sharp end plus wherever this player is.
 */
static void draw_leaderboard(int p, float vx, float vy, float vw, float vh)
{
    (void)vh;
    const Kart *me = &game.karts[p];
    float m_per_seg = game.track.total_len / (float)game.track.n;
    float x = vx + vw - 116.0f;
    float y = vy + 34.0f;
    float size = (game.cfg.n_humans > 1) ? 8.0f : 9.0f;
    float step = size + 5.0f;
    int rows = (game.cfg.n_humans > 1) ? 4 : NUM_KARTS;
    int rank, shown = 0;
    const Kart *leader = kart_at_rank(1);
    char buf[24];

    if (game.state == STATE_FINISHED)
        return;                       /* the results table takes over */

    for (rank = 1; rank <= NUM_KARTS; rank++) {
        const Kart *k = kart_at_rank(rank);
        const u8 *c;
        int is_me;
        float gap;

        if (!k)
            continue;
        is_me = (k == me);
        /* on a split screen, the top few and this player's own row */
        if (shown >= rows - 1 && !is_me)
            continue;
        if (shown >= rows && is_me)
            break;

        c = kart_color(k);
        snprintf(buf, sizeof(buf), "%d", rank);
        hud_text(x, y, size, size * 1.7f, buf,
                 is_me ? 255 : 190, is_me ? 220 : 195,
                 is_me ? 60 : 205, is_me ? 245 : 200);
        hud_text(x + 18.0f, y, size, size * 1.7f, kart_label(k),
                 c[0], c[1], c[2], is_me ? 250 : 215);

        if (k->finished) {
            snprintf(buf, sizeof(buf), "IN");
        } else if (rank == 1 || !leader) {
            snprintf(buf, sizeof(buf), "-");
        } else {
            /* distance to the leader, turned into a time at the pace the
             * leader is actually doing */
            float ref = leader->speed > 8.0f ? leader->speed : 8.0f;
            gap = (leader->total_progress - k->total_progress) * m_per_seg
                  / ref;
            if (gap < 0.0f) gap = 0.0f;
            if (gap > 99.0f)
                snprintf(buf, sizeof(buf), "-1L");
            else
                snprintf(buf, sizeof(buf), "%d.%d", (int)gap,
                         (int)((gap - (float)(int)gap) * 10.0f));
        }
        hud_text(x + 74.0f, y, size, size * 1.7f, buf, 180, 185, 200, 190);

        y += step;
        shown++;
    }
}

static void draw_player_hud(int p)
{
    const Kart *k = &game.karts[p];
    char buf[24];
    float vx, vy, vw, vh;

    viewport_rect(p, game.cfg.n_humans, &vx, &vy, &vw, &vh);

    /* lap, and the stopwatch: this lap so far, the last one, the best */
    {
        int laps = game.track.laps;
        int lap_disp = k->lap + 1;
        float running = game.race_t - k->lap_start_t;
        char t[16];
        if (lap_disp < 1) lap_disp = 1;
        if (lap_disp > laps) lap_disp = laps;
        snprintf(buf, sizeof(buf), "L%d/%d", lap_disp, laps);
        hud_text(vx + 14.0f, vy + 12.0f, 11.0f, 19.0f, buf,
                 255, 255, 255, 220);

        if (game.state == STATE_COUNTDOWN)
            running = 0.0f;
        format_lap_time(t, sizeof(t), running);
        hud_text(vx + 14.0f, vy + 32.0f, 12.0f, 21.0f, t,
                 235, 235, 245, 225);

        if (game.cfg.n_humans <= 2) {      /* room for the detail */
            format_lap_time(t, sizeof(t), k->last_lap_time);
            hud_text(vx + 14.0f, vy + 54.0f, 8.0f, 14.0f, "LAST",
                     160, 165, 180, 190);
            hud_text(vx + 52.0f, vy + 54.0f, 8.0f, 14.0f, t,
                     215, 218, 228, 205);
            format_lap_time(t, sizeof(t), k->best_lap_time);
            hud_text(vx + 14.0f, vy + 70.0f, 8.0f, 14.0f, "BEST",
                     190, 175, 90, 195);
            hud_text(vx + 52.0f, vy + 70.0f, 8.0f, 14.0f, t,
                     245, 225, 120, 215);

            /* the best this circuit has seen all session, not just this
             * race — stays blank until either this race or an earlier
             * one this session actually beats it */
            format_lap_time(t, sizeof(t),
                            session_best_lap_get(game.cfg.track_id, p));
            hud_text(vx + 14.0f, vy + 86.0f, 8.0f, 14.0f, "SESS",
                     140, 175, 190, 195);
            hud_text(vx + 52.0f, vy + 86.0f, 8.0f, 14.0f, t,
                     170, 220, 235, 215);
        }
    }
    /* position */
    snprintf(buf, sizeof(buf), "P%d", k->rank);
    hud_text(vx + vw - 60.0f, vy + 12.0f, 13.0f, 22.0f, buf,
             255, 220, 60, 240);

    /* speed, mph — red while the marshal has cut the power */
    snprintf(buf, sizeof(buf), "%d", (int)(fabsf(k->speed) * MPS_TO_MPH));
    if (k->wrong_way)
        hud_text(vx + 14.0f, vy + vh - 40.0f, 13.0f, 24.0f, buf,
                 245, 75, 65, 235);
    else
        hud_text(vx + 14.0f, vy + vh - 40.0f, 13.0f, 24.0f, buf,
                 235, 235, 235, 235);

    /* who you are chasing — and how they drive */
    if (game.cfg.n_humans == 1 && k->rank > 1 &&
        game.state == STATE_RACING) {
        const Kart *ahead = kart_at_rank(k->rank - 1);
        if (ahead) {
            const u8 *c = kart_color(ahead);
            hud_text(vx + 100.0f, vy + 14.0f, 8.0f, 14.0f, "CHASING",
                     170, 175, 190, 210);
            hud_text(vx + 176.0f, vy + 14.0f, 8.0f, 14.0f,
                     kart_label(ahead), c[0], c[1], c[2], 235);
        }
    }

    /* what's coming up: only ever finds anything when this race turned
     * weather on (otherwise weather_zone is all -1 for the whole track,
     * see game_init), and only worth a line where there's room for the
     * rest of the lap detail too. Reads the same track_weather_at the
     * road-surface tint and the AI's own cornering speed already use —
     * no new mechanic. */
    if (game.cfg.n_humans <= 2 && game.state == STATE_RACING) {
        const Track *t = &game.track;
        int seg = k->seg, j, weather = WEATHER_CLEAR;
        float dist = 0.0f;
        u8 wr = 255, wg = 255, wb = 255;

        for (j = 0; j < 40 && dist < 150.0f; j++) {
            weather = track_weather_at(t, seg, game.race_t, &game.settings);
            if (weather != WEATHER_CLEAR)
                break;
            dist += t->seg_len[seg];
            seg = (seg + 1) % t->n;
        }
        if (weather != WEATHER_CLEAR) {
            switch (weather) {
            case WEATHER_SNOW:   wr = 235; wg = 235; wb = 240; break;
            case WEATHER_ICE:    wr = 175; wg = 205; wb = 220; break;
            case WEATHER_PUDDLE: wr = 130; wg = 165; wb = 200; break;
            }
            hud_text(vx + 100.0f, vy + 34.0f, 8.0f, 14.0f, "AHEAD",
                     170, 175, 190, 210);
            hud_text(vx + 152.0f, vy + 34.0f, 8.0f, 14.0f,
                     weather_name(weather), wr, wg, wb, 235);
        }
    }

    /* gear and revs: the gear number, and a bar for where in the band it
     * is, so a manual driver can see when to shift */
    {
        const KartSpec *sp = &kart_specs[k->spec];
        float rev = game_clampf(k->rev_frac, 0.0f, 1.0f);
        float nominal_frac = game_clampf(sp->nominal_rpm /
                                         game.settings.tacho_redline_rpm,
                                         0.05f, 0.95f);
        u8 rr = 90, rg = 210, rb = 120;
        int rpm;

        /*
         * The band is read at a glance, so its colours are the four
         * things that matter rather than a gradient: grey while the
         * engine is bogging below its nominal RPM, green through the
         * useful range, amber in the shift window, red at the limiter.
         */
        if (k->rev_frac > 0.94f)      { rr = 255; rg =  80; rb =  65; }
        else if (k->rev_frac > 0.86f) { rr = 245; rg = 200; rb =  70; }
        else if (k->rev_frac < nominal_frac * 0.6f)
            { rr = 140; rg = 150; rb = 165; }

        snprintf(buf, sizeof(buf), "%d", k->gear + 1);
        hud_text(vx + 118.0f, vy + vh - 44.0f, 15.0f, 28.0f, buf,
                 235, 235, 245, 245);

        /* the digital tachometer: revs as a number, mapped from where the
         * car is in the gear onto the car's own idle-to-limiter range */
        rpm = (int)(game.settings.tacho_idle_rpm +
                    game_clampf(k->rev_frac, 0.0f, 1.05f) *
                    (game.settings.tacho_redline_rpm -
                     game.settings.tacho_idle_rpm));
        rpm = (rpm / 10) * 10;
        snprintf(buf, sizeof(buf), "%d", rpm);
        hud_text(vx + 112.0f, vy + vh - 27.0f, 9.0f, 15.0f, buf,
                 rr, rg, rb, 230);

        hud_rect(vx + 112.0f, vy + vh - 12.0f, 62.0f, 7.0f, 15, 15, 20, 170);
        hud_rect(vx + 113.0f, vy + vh - 11.0f, 60.0f * rev, 5.0f,
                 rr, rg, rb, 240);
        /* where the shift window starts, so the bar has a reference */
        hud_rect(vx + 113.0f + 60.0f * 0.86f, vy + vh - 12.0f, 1.0f, 7.0f,
                 235, 210, 120, 200);
        if (k->shift_t > 0.0f)      /* drive is cut during the change */
            hud_text(vx + 140.0f, vy + vh - 44.0f, 11.0f, 19.0f, "-",
                     255, 200, 80, 240);
        if (k->gear + 1 >= sp->n_gears)
            hud_text(vx + 140.0f, vy + vh - 44.0f, 9.0f, 15.0f, "T",
                     150, 200, 240, 220);
    }

    /* Turbo: spools up on its own near the rev limiter and bleeds off
     * the instant you lift or brake — a full bar means the engine is
     * making everything it has right now, no button required. */
    {
        float meter = game_clampf(k->turbo_spool, 0.0f, 1.0f);
        u8 br = 90, bg = 170, bb = 255;
        if (meter > 0.95f) { br = 255; bg = 220; bb = 90; }
        hud_text(vx + 112.0f, vy + vh - 72.0f, 8.0f, 14.0f, "TURBO",
                 160, 165, 180, 190);
        hud_rect(vx + 112.0f, vy + vh - 60.0f, 62.0f, 7.0f, 15, 15, 20, 170);
        hud_rect(vx + 113.0f, vy + vh - 59.0f, 60.0f * meter, 5.0f,
                 br, bg, bb, 240);
    }

    /*
     * Tires: how hot and how worn, because both now decide what the car
     * will do. The bar is the life left in them; it goes amber and then
     * red as that runs out, and the temperature reads in its own colour
     * when the rubber is outside the window it wants.
     */
    {
        float life = 1.0f - game_clampf(k->tire_wear, 0.0f, 1.0f);
        float opt = game.settings.tire_temp_optimal[k->tire %
                                                    TIRE_COMPOUNDS];
        float win = game.settings.tire_temp_window[k->tire %
                                                   TIRE_COMPOUNDS];
        float off = fabsf(k->tire_temp - opt) / (win > 1.0f ? win : 1.0f);
        u8 lr = 110, lg = 205, lb = 130;
        u8 tr = 170, tg = 190, tb = 210;
        float bx = vx + vw - 106.0f, by = vy + vh - 26.0f;

        if (life < 0.25f)      { lr = 235; lg =  80; lb =  70; }
        else if (life < 0.5f)  { lr = 240; lg = 190; lb =  70; }
        if (off > 1.0f)        { tr = 235; tg = 120; tb =  80; }
        else if (off > 0.55f)  { tr = 240; tg = 205; tb = 110; }

        hud_text(bx, by - 15.0f, 8.0f, 14.0f, tire_name(k->tire),
                 190, 195, 210, 200);
        snprintf(buf, sizeof(buf), "%dC", (int)k->tire_temp);
        hud_text(bx + 44.0f, by - 15.0f, 8.0f, 14.0f, buf, tr, tg, tb, 215);
        hud_rect(bx, by, 84.0f, 6.0f, 15, 15, 20, 170);
        hud_rect(bx + 1.0f, by + 1.0f, 82.0f * life, 4.0f, lr, lg, lb, 235);
    }

    draw_leaderboard(p, vx, vy, vw, vh);
    draw_finish_marker(p, vx, vy, vw, vh);
    draw_wrong_way_marker(p, vx, vy, vw, vh);
    draw_lap_popup(p, vx, vy, vw, vh);

    /* just been fished out of the void */
    if (k->fall_t > 0.0f)
        hud_text(vx + vw * 0.5f - 60.0f, vy + vh * 0.5f, 14.0f, 24.0f,
                 "FALLING", 255, 120, 90, 240);
}

static int displayed_action_on(int p, int action)
{
    const Input *in = &shown_input[p];
    switch (action) {
    case CONTROL_ACCEL:     return in->accel;
    case CONTROL_BRAKE:     return in->brake;
    case CONTROL_HANDBRAKE: return in->hop;
    case CONTROL_GEAR_UP:   return in->gear_up;
    case CONTROL_GEAR_DOWN: return in->gear_down;
    case CONTROL_BOOST:     return in->boost;
    case CONTROL_RACE_MENU:
        return (shown_gc[p] &
                control_config.gamecube[CONTROL_RACE_MENU]) != 0 ||
               (p < CONTROL_KEYBOARD_PLAYERS &&
                key_actions[p][CONTROL_RACE_MENU]) ||
               (wheld[p] & (WPAD_BUTTON_PLUS |
                            WPAD_CLASSIC_BUTTON_PLUS)) != 0;
    default:                return 0;
    }
}

static int displayed_key_action_on(int p, int action)
{
    return p < CONTROL_KEYBOARD_PLAYERS && key_actions[p][action];
}

static int displayed_gamecube_action_on(int p, int action)
{
    return (shown_gc[p] & control_config.gamecube[action]) != 0;
}

static void draw_binding_line(int p, float x, float y, const char *label,
                              int action)
{
    char key[16], buf[80];
    int key_on = displayed_key_action_on(p, action);
    int gc_on = displayed_gamecube_action_on(p, action);
    int game_on = displayed_action_on(p, action);
    int code = p < CONTROL_KEYBOARD_PLAYERS
                   ? control_config.keyboard[p][action][0] : GAME_KEY_NONE;
    snprintf(key, sizeof(key), "%s", control_key_name(code));
    snprintf(buf, sizeof(buf), "%-6.6s %-5.5s:%-2s %-9.9s %-12.12s:%-2s %s",
             label, key, key_on ? "ON" : "-",
             control_config.xbox_label[action],
             control_gamecube_name(control_config.gamecube[action]),
             gc_on ? "ON" : "-", game_on ? "ON" : "-");
    hud_text(x, y, 5.2f, 9.0f, buf,
             game_on ? 255 : 188,
             game_on ? 225 : 194,
             game_on ? 90 : 208, 235);
}

static void draw_input_translator(int p)
{
    char left[12], right[12], buf[80];
    float x = 14.0f, y = 48.0f;
    u32 wtype;
    int has_wii = WPAD_Probe((s32)p, &wtype) == WPAD_ERR_NONE;
    int has_key = p < CONTROL_KEYBOARD_PLAYERS && keyboard_here;
    int has_gc = (gc_mask & (1u << p)) != 0;
    int steer_pct = (int)(shown_input[p].steer * 100.0f);

    if (!control_config.show_input_overlay || game.cfg.n_humans != 1)
        return;

    hud_rect(x - 6.0f, y - 8.0f, 414.0f, 156.0f,
             8, 12, 22, 185);
    hud_text(x, y, 5.2f, 9.0f, "ACTION KEY:RAW XBOX DOLPHIN:RAW GAME",
             130, 205, 255, 245);
    y += 14.0f;

    snprintf(left, sizeof(left), "%s",
             control_key_name(control_config.keyboard[0]
                                                    [CONTROL_STEER_LEFT][0]));
    snprintf(right, sizeof(right), "%s",
             control_key_name(control_config.keyboard[0]
                                                     [CONTROL_STEER_RIGHT][0]));
    snprintf(buf, sizeof(buf), "STEER %s/%s:%s %.7s/%.7s %-9.9s:%s %d",
             left, right,
             (displayed_key_action_on(p, CONTROL_STEER_LEFT) ||
              displayed_key_action_on(p, CONTROL_STEER_RIGHT)) ? "ON" : "-",
             control_config.xbox_label[CONTROL_STEER_LEFT],
             control_config.xbox_label[CONTROL_STEER_RIGHT],
             control_gamecube_name(
                 control_config.gamecube[CONTROL_STEER_LEFT] |
                 control_config.gamecube[CONTROL_STEER_RIGHT]),
             (displayed_gamecube_action_on(p, CONTROL_STEER_LEFT) ||
              displayed_gamecube_action_on(p, CONTROL_STEER_RIGHT))
                 ? "ON" : "-",
             steer_pct);
    hud_text(x, y, 5.2f, 9.0f, buf, 205, 210, 225, 238);
    y += 14.0f;

    draw_binding_line(p, x, y, "GAS", CONTROL_ACCEL); y += 14.0f;
    draw_binding_line(p, x, y, "BRAKE", CONTROL_BRAKE); y += 14.0f;
    draw_binding_line(p, x, y, "HAND", CONTROL_HANDBRAKE); y += 14.0f;
    draw_binding_line(p, x, y, "UP", CONTROL_GEAR_UP); y += 14.0f;
    draw_binding_line(p, x, y, "DOWN", CONTROL_GEAR_DOWN); y += 14.0f;
    draw_binding_line(p, x, y, "FLOOR IT", CONTROL_BOOST); y += 14.0f;
    draw_binding_line(p, x, y, "MENU", CONTROL_RACE_MENU); y += 14.0f;

    snprintf(buf, sizeof(buf), "SEEN KEY %s  GC %s  WII %s",
             has_key ? "YES" : "NO", has_gc ? "YES" : "NO",
             has_wii ? "YES" : "NO");
    hud_text(x, y, 5.2f, 9.0f, buf, 140, 175, 200, 225);
}

static void draw_respawn_blackout(int p)
{
    const Kart *k = &game.karts[p];
    float vx, vy, vw, vh, alpha;
    if (k->respawn_t <= 0.0f)
        return;
    viewport_rect(p, game.cfg.n_humans, &vx, &vy, &vw, &vh);
    if (k->respawn_t > game.settings.respawn_fade_seconds)
        alpha = 255.0f;
    else
        alpha = 255.0f *
                (k->respawn_t / game.settings.respawn_fade_seconds);
    hud_rect(vx, vy, vw, vh, 0, 0, 0,
             (u8)game_clampf(alpha, 0.0f, 255.0f));
}

static void draw_race_hud(void)
{
    float W = (float)rmode->fbWidth;
    float H = (float)rmode->efbHeight;
    char buf[24];
    int p;

    hud_ortho_fullscreen();

    for (p = 0; p < game.cfg.n_humans; p++)
        draw_player_hud(p);
    draw_input_translator(0);

    /* minimap: corner in 1P, one shared map straddling the seam in 2P,
     * spare quadrant in 3P. Highlighting a single car only makes sense
     * when there is one obvious "the player" looking at it — kart 0 in
     * the 1P case; both humans get the ring in 2P (highlight -2, see
     * draw_minimap) since both are looking at the same shared map on
     * the same screen; the 3P case is one shared map for three
     * different people, so no single ring would mean the same thing to
     * all of them.
     *
     * 2P's own HUD is a horizontal split (viewport_rect), so there is
     * no spare quadrant the way 3P gets one — every screen edge and
     * both top corners of each half are already spoken for (lap timer
     * top-left, position/leaderboard top-right, gear/turbo/tire
     * bottom). The one gap big enough for a real map and clear on both
     * halves at once is a vertical strip left of the leaderboard
     * column and right of the centered finish/wrong-way/lap-popup text
     * (which never reaches past roughly x=400) — this sits it there,
     * centered on the screen's own vertical middle so it's read the
     * same way by whichever half a given player is looking at. */
    if (game.cfg.n_humans == 1)
        draw_minimap(&game.track, 1, 0, W - 130.0f, H - 140.0f, 100.0f);
    else if (game.cfg.n_humans == 2)
        draw_minimap(&game.track, 1, -2, W - 230.0f, H * 0.5f - 52.5f,
                     105.0f);
    else if (game.cfg.n_humans == 3)
        draw_minimap(&game.track, 1, -1, W * 0.5f + 60.0f, H * 0.5f + 40.0f,
                     150.0f);

    /* countdown / go */
    if (game.state == STATE_COUNTDOWN) {
        float c = game.countdown - 0.2f;
        if (c > 0.0f) {
            int n = (int)ceilf(c);
            if (n > 3) n = 3;
            snprintf(buf, sizeof(buf), "%d", n);
            hud_text(W * 0.5f - 40.0f, H * 0.5f - 70.0f, 80.0f, 130.0f,
                     buf, 255, 230, 60, 240);
        }
    } else if (game.state == STATE_RACING && game.race_t < 0.8f) {
        hud_text(W * 0.5f - 110.0f, H * 0.5f - 70.0f, 80.0f, 130.0f,
                 "GO", 120, 255, 120, 240);
    }

    if (game.state == STATE_FINISHED) {
        int rank, rows = NUM_KARTS;   /* classify the whole field */
        float y0;

        hud_rect(0.0f, 0.0f, W, H, 0, 0, 20, 150);
        hud_text(W * 0.5f - hud_text_width(30.0f, "FINISH") * 0.5f,
                 26.0f, 30.0f, 50.0f, "FINISH", 255, 255, 255, 240);

        /* full classification, so you can see which strategy won */
        y0 = 92.0f;
        for (rank = 1; rank <= rows; rank++) {
            const Kart *k = kart_at_rank(rank);
            const u8 *c;
            float y = y0 + (float)(rank - 1) * 22.0f;
            if (!k)
                continue;
            c = kart_color(k);
            snprintf(buf, sizeof(buf), "%d", rank);
            hud_text(W * 0.5f - 170.0f, y, 11.0f, 18.0f, buf,
                     230, 230, 235, 230);
            hud_text(W * 0.5f - 132.0f, y, 11.0f, 18.0f, kart_label(k),
                     c[0], c[1], c[2], 240);
            /* who they are, as well as where they finished */
            if (k->human < 0)
                hud_text(W * 0.5f - 44.0f, y, 8.0f, 13.0f,
                         ai_driver(k->driver_no)->trait, 150, 158, 175, 190);
            if (k->finished)
                snprintf(buf, sizeof(buf), "%.1fS", k->finish_time);
            else
                snprintf(buf, sizeof(buf), "LAP %d", k->lap + 1);
            hud_text(W * 0.5f + 74.0f, y, 11.0f, 18.0f, buf,
                     200, 205, 215, 225);
        }
        hud_text(W * 0.5f - hud_text_width(11.0f, "PRESS  ") * 0.5f,
                 H - 34.0f, 11.0f, 18.0f, "PRESS  ",
                 200, 200, 210, 200);
        /* '+' drawn as two bars */
        hud_rect(W * 0.5f + 52.0f, H - 27.0f, 14.0f, 4.0f,
                 200, 200, 210, 200);
        hud_rect(W * 0.5f + 57.0f, H - 32.0f, 4.0f, 14.0f,
                 200, 200, 210, 200);
    }

    /* Last overlay drawn: the blackout covers both the world and HUD,
     * then recedes during the configured fade-in. */
    for (p = 0; p < game.cfg.n_humans; p++)
        draw_respawn_blackout(p);
}

static void draw_race_exit_confirmation(void)
{
    float W = (float)rmode->fbWidth;
    float H = (float)rmode->efbHeight;
    float pw = W - 72.0f;
    float ph = 218.0f;
    float px, py;
    char yes[80], no[80];

    if (pw > 530.0f)
        pw = 530.0f;
    px = (W - pw) * 0.5f;
    py = (H - ph) * 0.5f;

    snprintf(yes, sizeof(yes), "YES  %s / %s / WII A OR 2",
             control_key_name(control_config.keyboard[0]
                                                     [CONTROL_MENU_CONFIRM][0]),
             control_config.xbox_label[CONTROL_MENU_CONFIRM]);
    snprintf(no, sizeof(no), "NO   %s / %s / WII B OR 1",
             control_key_name(control_config.keyboard[0]
                                                        [CONTROL_MENU_BACK][0]),
             control_config.xbox_label[CONTROL_MENU_BACK]);

    hud_ortho_fullscreen();
    hud_rect(0.0f, 0.0f, W, H, 0, 0, 8, 170);
    hud_rect(px - 4.0f, py - 4.0f, pw + 8.0f, ph + 8.0f,
             205, 72, 52, 245);
    hud_rect(px, py, pw, ph, 10, 17, 31, 250);

    hud_text(W * 0.5f - hud_text_width(24.0f, "LEAVE RACE") * 0.5f,
             py + 24.0f, 24.0f, 40.0f, "LEAVE RACE",
             255, 225, 100, 250);
    hud_text(W * 0.5f - hud_text_width(13.0f, "ARE YOU SURE") * 0.5f,
             py + 76.0f, 13.0f, 22.0f, "ARE YOU SURE",
             235, 238, 245, 240);
    hud_text(W * 0.5f - hud_text_width(7.5f, yes) * 0.5f,
             py + 117.0f, 7.5f, 13.0f, yes,
             255, 150, 120, 245);
    hud_text(W * 0.5f - hud_text_width(7.5f, no) * 0.5f,
             py + 145.0f, 7.5f, 13.0f, no,
             135, 245, 165, 245);
    hud_text(W * 0.5f -
                 hud_text_width(7.5f, "MENU AGAIN ALSO SAYS NO") * 0.5f,
             py + 178.0f, 7.5f, 13.0f, "MENU AGAIN ALSO SAYS NO",
             175, 190, 215, 225);
}

/* ------------------------------------------------------------------ */
/* Menus                                                               */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Garage: a real 3D workshop with the chosen car on a turntable       */
/* ------------------------------------------------------------------ */

static void draw_garage_scene(int paint_idx, const KartSpec *spec)
{
    Mtx view;
    Mtx44 persp;
    guVector cam, up, look;
    float W = (float)rmode->fbWidth, H = (float)rmode->efbHeight;
    float turn = (float)frame_no * 0.010f;
    int i;

    GX_SetViewport(0.0f, 0.0f, W, H, 0.0f, 1.0f);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    guPerspective(persp, 40.0f, view_aspect(W, H), 0.3f, 200.0f);
    GX_LoadProjectionMtx(persp, GX_PERSPECTIVE);

    cam.x = 4.4f;  cam.y = 2.35f; cam.z = 5.2f;
    up.x  = 0.0f;  up.y  = 1.0f;  up.z  = 0.0f;
    look.x = 0.30f; look.y = 0.60f; look.z = 0.0f;
    guLookAt(view, &cam, &up, &look);
    GX_LoadPosMtxImm(view, GX_PNMTX0);
    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);

    /* floor slab with a checker inlay */
    quad(-16.0f, 0.0f, -16.0f,  16.0f, 0.0f, -16.0f,
          16.0f, 0.0f,  16.0f, -16.0f, 0.0f,  16.0f, 38, 41, 50, 255);
    for (i = 0; i < 64; i++) {
        int cxi = i & 7, czi = i >> 3;
        float x0, z0;
        if (((cxi + czi) & 1) == 0)
            continue;
        x0 = -8.0f + 2.0f * (float)cxi;
        z0 = -8.0f + 2.0f * (float)czi;
        quad(x0,         0.01f, z0,
             x0 + 2.0f,  0.01f, z0,
             x0 + 2.0f,  0.01f, z0 + 2.0f,
             x0,         0.01f, z0 + 2.0f, 52, 56, 67, 255);
    }

    /* workshop walls */
    quad(-16.0f, 0.0f, -10.0f,  16.0f, 0.0f, -10.0f,
          16.0f, 11.0f, -10.0f, -16.0f, 11.0f, -10.0f, 47, 51, 63, 255);
    quad(-10.0f, 0.0f, -16.0f, -10.0f, 0.0f,  16.0f,
         -10.0f, 11.0f,  16.0f, -10.0f, 11.0f, -16.0f, 41, 45, 56, 255);

    /* overhead light bars (and their glow on the wall) */
    for (i = -1; i <= 1; i += 2) {
        float zc = (float)i * 3.2f;
        quad(-5.5f, 4.9f, zc - 0.35f,  5.5f, 4.9f, zc - 0.35f,
              5.5f, 4.9f, zc + 0.35f, -5.5f, 4.9f, zc + 0.35f,
             245, 246, 235, 255);
    }
    quad(-9.9f, 3.4f, -9.0f, -9.9f, 3.4f, 9.0f,
         -9.9f, 3.9f,  9.0f, -9.9f, 3.9f, -9.0f, 96, 104, 122, 255);

    /* turntable, then the car itself */
    draw_cone(0.0f, 0.02f, 0.0f, 3.1f, 0.10f, 74, 79, 92);
    draw_cone(0.0f, 0.12f, 0.0f, 2.7f, 0.06f, 92, 98, 112);
    quad(-1.6f, 0.19f, -1.1f,  1.6f, 0.19f, -1.1f,
          1.6f, 0.19f,  1.1f, -1.6f, 0.19f,  1.1f, 0, 0, 0, 90);
    draw_car_model(0.0f, 0.18f, 0.0f, turn, 0.0f, 0.0f, 0.0f,
                   paint_palette[paint_idx % PAINT_COUNT], spec, 0);
}

static void menu_update_track_preview(void)
{
    if (menu_track_loaded != sel_track) {
        track_init_with_settings(&menu_track, sel_track, &app_settings);
        menu_track_loaded = sel_track;
    }
}

/* ------------------------------------------------------------------ */
/* Menus: a cursor over rows, never "press anything to go forward"      */
/* ------------------------------------------------------------------ */

/*
 * Every screen is a list of rows with a cursor on one of them. Up/down
 * moves the cursor, left/right changes the value of the row it is on, and
 * only the action rows (GO, GARAGE, DONE, EXIT) do anything when
 * activated. Previously any button press advanced the whole screen, so
 * pressing A to look at something jumped straight past it.
 *
 * Every screen also has a way back: an explicit row, and the B/1/Esc
 * button. Nothing is a one-way door.
 */
enum {
    RK_PLAYERS = 0, RK_TRACK, RK_LAPS, RK_WEATHER, RK_GARAGE, RK_DESIGN,
    RK_CONFIG, RK_START, RK_EXIT,                              /* setup   */
    RK_CAR, RK_PAINT, RK_GEARBOX, RK_TIRES, RK_EDIT_CAR, RK_DONE,
                                                               /* garage  */
    RK_DES_NAME, RK_DES_POWER, RK_DES_BRAKE, RK_DES_GRIP,
    RK_DES_DRAG, RK_DES_OFFROAD, RK_DES_ASPIRATION, RK_DES_DRIVETRAIN,
    RK_DES_AWD_BIAS,
    RK_DES_GEARCOUNT, RK_DES_GEAR_SEL, RK_DES_GEAR_TOP, RK_DES_NOMINAL_RPM,
    RK_DES_SAVE, RK_DES_CANCEL                                /* designer */
};

typedef struct {
    int kind;
    int player;
} MenuRow;

/* sized for the designer screen's worst case: 8 stat rows (one
 * conditional on AWD) plus SAVE and CANCEL */
static MenuRow menu_rows[16];
static int n_menu_rows;
static int menu_row;
static int garage_player;
static char menu_msg[44];
static float menu_msg_t;

enum { SCREEN_SETUP = 0, SCREEN_GARAGE = 1, SCREEN_CONFIG = 2,
       SCREEN_DESIGNER = 3 };

static void menu_notice(const char *text)
{
    snprintf(menu_msg, sizeof(menu_msg), "%s", text);
    menu_msg_t = 4.0f;
}

/*
 * The in-game car designer. Rather than a full on-screen keyboard, a
 * name is picked from a fixed list the same way a paint colour or a
 * track is — consistent with every other choice in this menu, and one
 * fewer input widget to build and test blind.
 *
 * Mass is not one of the exposed stats: it is derived from power
 * (DESIGNER_MASS_BASE + power_hp * DESIGNER_MASS_PER_HP, see
 * designer_finalize), on purpose. A slider that let power and mass move
 * independently would let a design get both a superkart's power-to-
 * weight and a truck's raw horsepower with no cost anywhere — every
 * real car pays for more engine with more weight around it, and this
 * makes the power row the one place that trade-off actually lives:
 * push it up and the car gets both stronger and heavier, same as a
 * bigger engine really would.
 *
 * Grip is not exposed either, for the same reason: a lighter, less
 * draggy car corners differently than a heavy, draggy one whether or
 * not a player asked it to. designer_grip() derives lat_g from the
 * mass the power trade-off already settled on (lighter is grippier)
 * and from drag (a car built with more aero surface — more cd_a — is
 * assumed to be running some of that as downforce, not pure drag, so
 * it gets some grip back for the privilege of a lower top speed). The
 * GRIP row still shows the number; it just cannot be dragged around on
 * its own any more.
 *
 * Wheelbase is still derived from mass, the same way it always was.
 * Gearing only exposes what a driver can actually feel: gear count and
 * each gear's own top speed (ratio) are directly editable — GEAR_SEL
 * pages through whichever gear GEAR_TOP is currently pointed at, so the
 * row count stays fixed regardless of how many gears the car has — plus
 * one nominal RPM shared by every gear (real engines have one power
 * curve, not one per gear; see the KartSpec comment in game.h). There
 * is deliberately no separate upshift/downshift dial any more: where
 * the automatic box actually changes gear now follows straight from
 * nominal RPM (kart_step), the same way a real driver picks a shift
 * point off where the engine actually makes power, not off a number
 * with no engine behind it.
 */
static const char *designer_names[] = {
    "PROTOTYPE", "HOMEBREW", "BLUEPRINT", "MAVERICK", "RENEGADE",
    "PIONEER", "VELOCITY", "CATALYST", "PHANTOM", "NOVA", "ROOKIE",
    "APEX", "VECTOR", "INDIE", "GARAGE", "CUSTOM"
};
#define DESIGNER_NAME_COUNT \
    (int)(sizeof(designer_names) / sizeof(designer_names[0]))

/* the power/mass trade-off: every extra hp costs real weight, so a
 * light car and a powerful car are no longer independent choices */
#define DESIGNER_MASS_BASE    500.0f
#define DESIGNER_MASS_PER_HP    1.8f
/* Forced induction costs curb weight too, on top of power_hp's own —
 * the hardware itself (intercooler and plumbing for a turbo; the blower
 * and its drive for a supercharger; two of everything for twin-turbo)
 * is real mass that isn't there on a naturally aspirated engine. This
 * is what makes "build a bigger NA engine instead" a genuine choice
 * rather than strictly worse than bolting on a turbo: an NA car gets
 * to spend its whole power_hp-to-mass budget on displacement, nothing
 * held back for hardware it doesn't have. */
#define DESIGNER_MASS_TURBO_KG      35.0f
#define DESIGNER_MASS_SUPERCHARGED_KG 28.0f
#define DESIGNER_MASS_TWIN_TURBO_KG  65.0f

/*
 * The grip trade-off. Lighter cars corner better: real tires get
 * measurably less coefficient of friction out of a heavier normal load
 * on the contact patch (tire load sensitivity — a well documented
 * property of rubber, not a game-only rule), so two cars on
 * similarly-sized tires see the lighter one able to pull more lateral
 * g even before anything else about them differs. That's the mass
 * term below, unchanged from when grip first stopped being a free
 * dial.
 *
 * Drag is the other input, and it now goes the other way: LESS drag
 * area buys MORE grip, not less. Earlier this treated extra cd_a as
 * assumed downforce (an open-wheel racer's big wing costs drag and
 * buys grip at the same time) — defensible for a car built purely to
 * corner, but backwards for the kind of road cars and karts this
 * roster actually is. Without a separate downforce stat, cd_a here is
 * just "how much blunt, turbulent frontal area this car presents," and
 * that same bluntness disturbs the airflow around the contact patches
 * as much as it costs top speed — a low, clean, low-drag shape (RACER,
 * FORMULA) is also the more aerodynamically planted one, while a tall,
 * high-drag shape (TRUCK, WAGON) is dirty air both ways. So: less drag
 * area, more grip.
 */
#define DESIGNER_GRIP_BASE       1.15f
#define DESIGNER_GRIP_MASS_REF 1000.0f
#define DESIGNER_GRIP_PER_1000KG 0.30f
#define DESIGNER_GRIP_CDA_REF     0.62f
#define DESIGNER_GRIP_PER_CDA     0.35f
#define DESIGNER_GRIP_MIN         0.85f
#define DESIGNER_GRIP_MAX         1.85f

/* the braking trade-off: stopping distance is no longer a free dial
 * either (see designer_brake_dist below). kart_step no longer even
 * reads KartSpec.brake_dist_100 for the actual physics — braking is
 * traction-limited now (v^2 <= 2 * mu_trac * d), the same grip budget
 * every other straight-line force reads, see mu_trac in kart_step —
 * so this recomputes the exact same 100-0 distance that grip budget
 * actually produces, rather than a separately-tunable estimate that
 * could drift from what the car really does on track. 100 km/h is
 * V100 in game.c's own units, duplicated here rather than exposed
 * across the module boundary for one constant. */
#define DESIGNER_BRAKE_V100_MPS   27.78f

static KartSpec designer_car;
static int designer_name_idx;
static int designer_gear_sel;   /* 0-indexed gear GEAR_TOP edit         */
/* >= 0: SAVE patches kart_specs[designer_editing_idx] in place instead
 * of adding a new car (see designer_apply_edit) — session only, never
 * written to disk. -1: the ordinary new-car path (designer_save). */
static int designer_editing_idx = -1;
static int designer_return_screen;   /* where CANCEL/SAVE goes back to */

/* mass, then grip and an estimated braking distance from that mass and
 * the current drag figure — the same derived numbers designer_finalize
 * commits to a real spec, kept in one place so the live preview
 * (row_value) can never show a different number than SAVE actually uses */
static float designer_mass(void)
{
    float m = DESIGNER_MASS_BASE + designer_car.power_hp * DESIGNER_MASS_PER_HP;
    switch (designer_car.aspiration) {
    case ASPIRATION_TURBO:        m += DESIGNER_MASS_TURBO_KG;        break;
    case ASPIRATION_SUPERCHARGED: m += DESIGNER_MASS_SUPERCHARGED_KG; break;
    case ASPIRATION_TWIN_TURBO:   m += DESIGNER_MASS_TWIN_TURBO_KG;   break;
    default: break;
    }
    return m;
}

static float designer_grip(void)
{
    float g = DESIGNER_GRIP_BASE -
              (designer_mass() - DESIGNER_GRIP_MASS_REF) / 1000.0f *
                  DESIGNER_GRIP_PER_1000KG -
              (designer_car.cd_a - DESIGNER_GRIP_CDA_REF) *
                  DESIGNER_GRIP_PER_CDA;
    return game_clampf(g, DESIGNER_GRIP_MIN, DESIGNER_GRIP_MAX);
}

/* see the comment above designer_brake_dist's constant: this is exactly
 * kart_step's own traction-limited braking, using the same grip figure
 * designer_grip() already derived (mass and drag, not a free number). */
static float designer_brake_dist(void)
{
    return (DESIGNER_BRAKE_V100_MPS * DESIGNER_BRAKE_V100_MPS) /
           (2.0f * designer_grip() * GRAVITY);
}

static void designer_reset(void)
{
    static const float base_gears[5] =
        { 16.111f, 27.222f, 40.278f, 54.167f, 69.444f };
    int g;

    memset(&designer_car, 0, sizeof(designer_car));
    designer_name_idx = 0;
    designer_gear_sel = 0;
    designer_editing_idx = -1;
    snprintf(designer_car.name, sizeof(designer_car.name), "%s",
             designer_names[designer_name_idx]);
    designer_car.power_hp = 220.0f;
    designer_car.cd_a = 0.62f;
    designer_car.offroad_grip = 0.40f;
    designer_car.drivetrain = DRIVETRAIN_RWD;
    designer_car.awd_front_bias = 0.5f;
    designer_car.aspiration = ASPIRATION_NA;
    designer_car.n_gears = 5;
    for (g = 0; g < 5; g++)
        designer_car.gear_top[g] = base_gears[g];
    kart_spec_default_gearing(&designer_car);
}

/* Load an existing car for a session-only edit (the garage's EDIT CAR
 * row) rather than building a new one. Its name is kept as-is — see
 * designer_apply_edit — everything else becomes editable exactly like
 * a car under construction, including mass and grip re-deriving from
 * whatever power/drag it is edited to (which may not match the number
 * that shipped with it, if it predates those trade-offs — see
 * docs/HANDOFF.md). */
static void designer_load_existing(int idx)
{
    designer_car = kart_specs[idx];
    designer_editing_idx = idx;
    designer_gear_sel = 0;
}

/* Fill in the fields the designer still derives outright rather than
 * exposing: mass, grip and braking distance (see the comments above),
 * and wheelbase from whatever mass that settled on. Producing the
 * complete KartSpec that both the live preview and SAVE itself work
 * from. */
static void designer_finalize(KartSpec *out)
{
    *out = designer_car;
    out->mass_kg = designer_mass();
    out->lat_g = designer_grip();
    out->brake_dist_100 = designer_brake_dist();
    out->wheelbase = 2.0f + out->mass_kg / 2500.0f;
}

static void build_rows(void)
{
    int p;

    n_menu_rows = 0;
    if (menu_screen == SCREEN_SETUP) {
        menu_rows[n_menu_rows].kind = RK_PLAYERS;
        menu_rows[n_menu_rows++].player = 0;
        menu_rows[n_menu_rows].kind = RK_TRACK;
        menu_rows[n_menu_rows++].player = 0;
        menu_rows[n_menu_rows].kind = RK_LAPS;
        menu_rows[n_menu_rows++].player = 0;
        menu_rows[n_menu_rows].kind = RK_WEATHER;
        menu_rows[n_menu_rows++].player = 0;
        for (p = 0; p < sel_players; p++) {
            menu_rows[n_menu_rows].kind = RK_GARAGE;
            menu_rows[n_menu_rows++].player = p;
        }
        menu_rows[n_menu_rows].kind = RK_DESIGN;
        menu_rows[n_menu_rows++].player = 0;
        menu_rows[n_menu_rows].kind = RK_CONFIG;
        menu_rows[n_menu_rows++].player = 0;
        menu_rows[n_menu_rows].kind = RK_START;
        menu_rows[n_menu_rows++].player = 0;
        menu_rows[n_menu_rows].kind = RK_EXIT;
        menu_rows[n_menu_rows++].player = 0;
    } else if (menu_screen == SCREEN_CONFIG) {
        menu_rows[n_menu_rows].kind = RK_DONE;
        menu_rows[n_menu_rows++].player = 0;
    } else if (menu_screen == SCREEN_DESIGNER) {
        static const int rows[] = {
            RK_DES_NAME, RK_DES_POWER, RK_DES_BRAKE,
            RK_DES_GRIP, RK_DES_DRAG, RK_DES_OFFROAD, RK_DES_ASPIRATION,
            RK_DES_DRIVETRAIN
        };
        int i;
        for (i = 0; i < (int)(sizeof(rows) / sizeof(rows[0])); i++) {
            menu_rows[n_menu_rows].kind = rows[i];
            menu_rows[n_menu_rows++].player = 0;
        }
        if (designer_car.drivetrain == DRIVETRAIN_AWD) {
            menu_rows[n_menu_rows].kind = RK_DES_AWD_BIAS;
            menu_rows[n_menu_rows++].player = 0;
        }
        {
            static const int gear_rows[] = {
                RK_DES_GEARCOUNT, RK_DES_GEAR_SEL, RK_DES_GEAR_TOP,
                RK_DES_NOMINAL_RPM
            };
            for (i = 0; i < (int)(sizeof(gear_rows) / sizeof(gear_rows[0]));
                 i++) {
                menu_rows[n_menu_rows].kind = gear_rows[i];
                menu_rows[n_menu_rows++].player = 0;
            }
        }
        menu_rows[n_menu_rows].kind = RK_DES_SAVE;
        menu_rows[n_menu_rows++].player = 0;
        menu_rows[n_menu_rows].kind = RK_DES_CANCEL;
        menu_rows[n_menu_rows++].player = 0;
    } else {
        menu_rows[n_menu_rows].kind = RK_CAR;
        menu_rows[n_menu_rows++].player = garage_player;
        menu_rows[n_menu_rows].kind = RK_PAINT;
        menu_rows[n_menu_rows++].player = garage_player;
        menu_rows[n_menu_rows].kind = RK_GEARBOX;
        menu_rows[n_menu_rows++].player = garage_player;
        menu_rows[n_menu_rows].kind = RK_TIRES;
        menu_rows[n_menu_rows++].player = garage_player;
        menu_rows[n_menu_rows].kind = RK_EDIT_CAR;
        menu_rows[n_menu_rows++].player = garage_player;
        menu_rows[n_menu_rows].kind = RK_DONE;
        menu_rows[n_menu_rows++].player = garage_player;
    }
    if (menu_row >= n_menu_rows)
        menu_row = n_menu_rows - 1;
    if (menu_row < 0)
        menu_row = 0;
}

static void row_label(const MenuRow *r, char *out, int cap)
{
    switch (r->kind) {
    case RK_PLAYERS: snprintf(out, cap, "PLAYERS");            break;
    case RK_TRACK:   snprintf(out, cap, "TRACH");              break;
    case RK_LAPS:    snprintf(out, cap, "LAPS");               break;
    case RK_WEATHER: snprintf(out, cap, "WEATHER");            break;
    case RK_CONFIG:  snprintf(out, cap, "JSON CONFIG");        break;
    case RK_GARAGE:  snprintf(out, cap, "P%d GARAGE", r->player + 1); break;
    case RK_START:   snprintf(out, cap, "GO");                 break;
    case RK_EXIT:    snprintf(out, cap, "EXIT");               break;
    case RK_CAR:     snprintf(out, cap, "CAR");                break;
    case RK_PAINT:   snprintf(out, cap, "PAINT");              break;
    case RK_GEARBOX: snprintf(out, cap, "GEARS");              break;
    case RK_TIRES:   snprintf(out, cap, "TIRES");              break;
    case RK_EDIT_CAR: snprintf(out, cap, "EDIT CAR");          break;
    case RK_DESIGN:  snprintf(out, cap, "DESIGN A CAR");       break;
    case RK_DES_NAME:       snprintf(out, cap, "NAME");        break;
    case RK_DES_POWER:      snprintf(out, cap, "POWER");       break;
    case RK_DES_BRAKE:      snprintf(out, cap, "BRAKES");      break;
    case RK_DES_GRIP:       snprintf(out, cap, "GRIP");        break;
    case RK_DES_DRAG:       snprintf(out, cap, "AERO");        break;
    case RK_DES_OFFROAD:    snprintf(out, cap, "DIRT GRIP");   break;
    case RK_DES_ASPIRATION: snprintf(out, cap, "ASPIRATION");  break;
    case RK_DES_DRIVETRAIN: snprintf(out, cap, "DRIVE");       break;
    case RK_DES_AWD_BIAS:   snprintf(out, cap, "AWD BIAS");    break;
    case RK_DES_GEARCOUNT:  snprintf(out, cap, "GEARS");       break;
    case RK_DES_GEAR_SEL:   snprintf(out, cap, "EDIT GEAR");   break;
    case RK_DES_GEAR_TOP:   snprintf(out, cap, "GEAR TOP SPEED"); break;
    case RK_DES_NOMINAL_RPM: snprintf(out, cap, "NOMINAL RPM"); break;
    case RK_DES_SAVE:
        snprintf(out, cap, "%s",
                designer_editing_idx >= 0 ? "APPLY (SESSION ONLY)" : "SAVE");
        break;
    case RK_DES_CANCEL:     snprintf(out, cap, "CANCEL");      break;
    default:         snprintf(out, cap, "DONE");               break;
    }
}

static void row_value(const MenuRow *r, char *out, int cap)
{
    int p = r->player;
    switch (r->kind) {
    case RK_PLAYERS: snprintf(out, cap, "%d", sel_players);                 break;
    case RK_TRACK:   snprintf(out, cap, "%s", track_name(sel_track));       break;
    case RK_LAPS:
        if (sel_laps <= 0)
            snprintf(out, cap, "AUTO %d", menu_track_laps());
        else
            snprintf(out, cap, "%d", sel_laps);
        break;
    case RK_WEATHER:
        snprintf(out, cap, "%s",
                sel_weather == WEATHER_TOGGLE_ON ? "ON" : "OFF");
        break;
    case RK_CONFIG:  snprintf(out, cap, "%s", config_banner[0]
                                             ? config_banner
                                             : "BUILT IN");            break;
    case RK_GARAGE:  snprintf(out, cap, "%s",
                              kart_specs[sel_spec[p] % kart_spec_count].name); break;
    case RK_CAR:     snprintf(out, cap, "%s",
                              kart_specs[sel_spec[p] % kart_spec_count].name); break;
    case RK_PAINT:   snprintf(out, cap, "%s",
                              paint_names[sel_paint[p] % PAINT_COUNT]);     break;
    case RK_GEARBOX: snprintf(out, cap, "%s", gearbox_name(sel_gearbox[p])); break;
    case RK_TIRES:   snprintf(out, cap, "%s", tire_name(sel_tire[p]));      break;
    case RK_EDIT_CAR: snprintf(out, cap, "%s",
                              kart_specs[sel_spec[p] % kart_spec_count].name); break;
    case RK_DES_NAME:
        snprintf(out, cap, "%s", designer_car.name); break;
    case RK_DES_POWER:
        snprintf(out, cap, "%d HP", (int)designer_car.power_hp);     break;
    case RK_DES_BRAKE:
        snprintf(out, cap, "%d FT",
                (int)(designer_brake_dist() * M_TO_FT));
        break;
    case RK_DES_GRIP:
        snprintf(out, cap, "%.2f G", (double)designer_grip());       break;
    case RK_DES_DRAG:
        snprintf(out, cap, "%.2f", (double)designer_car.cd_a);       break;
    case RK_DES_OFFROAD:
        snprintf(out, cap, "%d", (int)(designer_car.offroad_grip * 100.0f));
        break;
    case RK_DES_ASPIRATION:
        snprintf(out, cap, "%s", aspiration_name(designer_car.aspiration));
        break;
    case RK_DES_DRIVETRAIN:
        snprintf(out, cap, "%s", drivetrain_name(designer_car.drivetrain));
        break;
    case RK_DES_AWD_BIAS:
        snprintf(out, cap, "%d F",
                (int)(designer_car.awd_front_bias * 100.0f));
        break;
    case RK_DES_GEARCOUNT:
        snprintf(out, cap, "%d", designer_car.n_gears); break;
    case RK_DES_GEAR_SEL:
        snprintf(out, cap, "%d/%d", designer_gear_sel + 1,
                designer_car.n_gears);
        break;
    case RK_DES_GEAR_TOP:
        snprintf(out, cap, "%d MPH",
                (int)(designer_car.gear_top[designer_gear_sel] * MPS_TO_MPH));
        break;
    case RK_DES_NOMINAL_RPM:
        snprintf(out, cap, "%d RPM", (int)designer_car.nominal_rpm);
        break;
    default:         out[0] = 0;                                            break;
    }
}

static int row_has_value(const MenuRow *r)
{
    return (r->kind == RK_PLAYERS || r->kind == RK_TRACK ||
            r->kind == RK_LAPS || r->kind == RK_WEATHER ||
            r->kind == RK_CAR || r->kind == RK_PAINT ||
            r->kind == RK_GEARBOX || r->kind == RK_TIRES ||
            r->kind == RK_EDIT_CAR ||
            r->kind == RK_DES_NAME ||
            r->kind == RK_DES_POWER || r->kind == RK_DES_BRAKE ||
            r->kind == RK_DES_GRIP || r->kind == RK_DES_DRAG ||
            r->kind == RK_DES_OFFROAD || r->kind == RK_DES_ASPIRATION ||
            r->kind == RK_DES_DRIVETRAIN ||
            r->kind == RK_DES_AWD_BIAS || r->kind == RK_DES_GEARCOUNT ||
            r->kind == RK_DES_GEAR_SEL || r->kind == RK_DES_GEAR_TOP ||
            r->kind == RK_DES_NOMINAL_RPM);
}

static void row_change(const MenuRow *r, int d)
{
    int p = r->player;
    switch (r->kind) {
    case RK_PLAYERS:
        sel_players += d;
        if (sel_players < 1) sel_players = MAX_SELECTABLE_PLAYERS;
        if (sel_players > MAX_SELECTABLE_PLAYERS) sel_players = 1;
        break;
    case RK_TRACK:
        sel_track = ((sel_track + d) % TRACK_COUNT + TRACK_COUNT) % TRACK_COUNT;
        break;
    case RK_LAPS:
        /* 0 is AUTO — the circuit's own count — then 1 to 9 by hand */
        sel_laps += d;
        if (sel_laps < 0) sel_laps = 9;
        if (sel_laps > 9) sel_laps = 0;
        break;
    case RK_WEATHER:
        sel_weather = ((sel_weather + d) % 2 + 2) % 2;
        break;
    case RK_GARAGE:
    case RK_CAR:
        sel_spec[p] = ((sel_spec[p] + d) % kart_spec_count +
                       kart_spec_count) % kart_spec_count;
        break;
    case RK_PAINT:
        sel_paint[p] = ((sel_paint[p] + d) % PAINT_COUNT + PAINT_COUNT)
                       % PAINT_COUNT;
        break;
    case RK_GEARBOX:
        sel_gearbox[p] = ((sel_gearbox[p] + d) % GEARBOX_MODES + GEARBOX_MODES)
                         % GEARBOX_MODES;
        break;
    case RK_TIRES:
        sel_tire[p] = ((sel_tire[p] + d) % TIRE_COMPOUNDS + TIRE_COMPOUNDS)
                      % TIRE_COMPOUNDS;
        break;
    case RK_DES_NAME:
        /* locked while editing an existing car — its identity is not
         * one of the things being edited, only its numbers are (see
         * designer_load_existing/designer_apply_edit) */
        if (designer_editing_idx >= 0)
            break;
        designer_name_idx = ((designer_name_idx + d) % DESIGNER_NAME_COUNT +
                             DESIGNER_NAME_COUNT) % DESIGNER_NAME_COUNT;
        snprintf(designer_car.name, sizeof(designer_car.name), "%s",
                designer_names[designer_name_idx]);
        break;
    case RK_DES_POWER:
        designer_car.power_hp = game_clampf(designer_car.power_hp +
                                            (float)d * 20.0f, 60.0f, 650.0f);
        break;
    /* BRAKES has no case here on purpose any more, same reason as GRIP
     * just below — see designer_brake_dist(). It is derived from grip
     * (itself derived from mass and drag), not an independent dial;
     * the row shows the number without changing it. */
    /* GRIP has no case here on purpose — see designer_grip(). It is
     * derived from mass (itself derived from power) and drag, not an
     * independent dial; the row shows the number without changing it. */
    case RK_DES_DRAG:
        /* same less-is-better convention as brakes, for drag */
        designer_car.cd_a = game_clampf(designer_car.cd_a -
                                        (float)d * 0.02f, 0.42f, 0.85f);
        break;
    case RK_DES_OFFROAD:
        designer_car.offroad_grip = game_clampf(designer_car.offroad_grip +
                                                (float)d * 0.05f, 0.10f,
                                                0.90f);
        break;
    case RK_DES_ASPIRATION:
        designer_car.aspiration = ((designer_car.aspiration + d) %
                                   ASPIRATION_COUNT + ASPIRATION_COUNT) %
                                  ASPIRATION_COUNT;
        break;
    case RK_DES_DRIVETRAIN:
        designer_car.drivetrain = ((designer_car.drivetrain + d) % 3 + 3)
                                  % 3;
        break;
    case RK_DES_AWD_BIAS:
        designer_car.awd_front_bias = game_clampf(
            designer_car.awd_front_bias + (float)d * 0.05f, 0.0f, 1.0f);
        break;
    case RK_DES_GEARCOUNT: {
        int new_n = designer_car.n_gears + d;
        if (new_n < 1) new_n = 1;
        if (new_n > MAX_GEARS) new_n = MAX_GEARS;
        if (new_n > designer_car.n_gears) {
            int g;
            /* a gear grown back after being shrunk keeps whatever it
             * had before; a genuinely new one gets a sane starting
             * point instead of the zero it reads as right now */
            for (g = designer_car.n_gears; g < new_n; g++)
                if (!(designer_car.gear_top[g] > 0.0f))
                    designer_car.gear_top[g] = fminf(
                        designer_car.gear_top[g - 1] + 8.0f, 140.0f);
            designer_car.n_gears = new_n;
            kart_spec_default_gearing(&designer_car);
        } else {
            designer_car.n_gears = new_n;
        }
        if (designer_gear_sel >= designer_car.n_gears)
            designer_gear_sel = designer_car.n_gears - 1;
        break;
    }
    case RK_DES_GEAR_SEL:
        designer_gear_sel = ((designer_gear_sel + d) % designer_car.n_gears +
                             designer_car.n_gears) % designer_car.n_gears;
        break;
    case RK_DES_GEAR_TOP: {
        int sel = designer_gear_sel;
        /* stays strictly between its neighbors — kart_spec_validate
         * would reject a ladder that did not, and there is no reason
         * to make a player discover that at SAVE instead of here.
         * Step is exactly 1 mph, now that the row displays in mph. */
        float lo = (sel > 0) ? designer_car.gear_top[sel - 1] + 0.5f
                              : 2.0f;
        float hi = (sel < designer_car.n_gears - 1)
                       ? designer_car.gear_top[sel + 1] - 0.5f : 140.0f;
        designer_car.gear_top[sel] = game_clampf(
            designer_car.gear_top[sel] + (float)d / MPS_TO_MPH, lo, hi);
        break;
    }
    case RK_DES_NOMINAL_RPM:
        /* the one gearing dial left: where the engine itself peaks,
         * shared by every gear (see the KartSpec comment in game.h) —
         * shift points follow automatically from this in kart_step,
         * they are not a separate thing to tune here */
        designer_car.nominal_rpm = game_clampf(
            designer_car.nominal_rpm + (float)d * 100.0f, 1500.0f, 9500.0f);
        break;
    default:
        break;
    }
}

/*
 * Silence every channel, not just the ones this race used: rumble is
 * started inside the per-player loop but a race can be left from outside
 * it (quit, or a controller going away), and a 4-player race followed by
 * a 1-player race would otherwise leave channels 2-4 buzzing for the rest
 * of the session.
 */
static void stop_all_rumble(void)
{
    int p;
    for (p = 0; p < MAX_HUMANS; p++) {
        rumble_t[p] = 0.0f;
        WPAD_Rumble(p, 0);
        PAD_ControlMotor(p, PAD_MOTOR_STOP);
    }
}

static void leave_race_for_menu(void)
{
    stop_all_rumble();
    race_exit_confirm = 0;
    app_state = APP_MENU;
    menu_screen = SCREEN_SETUP;
    menu_row = 0;
}

static void start_race(void)
{
    GameConfig cfg;
    int p;

    memset(&cfg, 0, sizeof(cfg));
    cfg.track_id = sel_track;
    cfg.n_humans = sel_players;
    cfg.settings = &app_settings;
    cfg.laps_override = sel_laps;
    cfg.weather = sel_weather;
    for (p = 0; p < MAX_HUMANS; p++) {
        cfg.spec[p] = sel_spec[p] % kart_spec_count;
        cfg.paint[p] = sel_paint[p] % PAINT_COUNT;
        cfg.gearbox[p] = sel_gearbox[p] % GEARBOX_MODES;
        cfg.tire[p] = sel_tire[p] % TIRE_COMPOUNDS;
    }

    game_init(&game, &cfg);
    place_scenery(&game.track);
    stop_all_rumble();
    race_exit_confirm = 0;
    controller_lost_t = 0.0f;    /* a dropout banked in a previous race
                                  * must not shorten this one's grace */
    prev_countdown_n = -1;
    for (p = 0; p < MAX_HUMANS; p++) {
        Kart *k = &game.karts[p < cfg.n_humans ? p : 0];
        camera_reset(&cam[p], &app_settings, k, &game.track);
        lap_popup_t[p] = 0.0f;
        rumble_t[p] = 0.0f;
        steer_axis_reset(&steer_axis[p]);
        gc_hop_prev[p] = 0;
        gc_hop_latched[p] = 0;
    }
    app_state = APP_RACE;
    audio_beep(660.0f, 90, 160);
}

/*
 * Starting a race needs a controller per player. If there are not enough,
 * say so and stay put — the old behaviour would have started a race with
 * a player who could not steer.
 */
static void try_start_race(void)
{
    int avail = available_players();

    if (avail < sel_players) {
        char buf[44];
        snprintf(buf, sizeof(buf), "NEED %d PADS  FOUND %d",
                 sel_players, avail);
        menu_notice(buf);
        audio_beep(150.0f, 240, 175);
        return;
    }
    start_race();
}

/*
 * SAVE: add the finalized car to the live roster, then try to persist
 * the whole roster to disk at the exact path cars.json was found under
 * at boot. A rejected car (duplicate name, full garage) keeps the
 * player on the designer screen to fix it; an accepted one always
 * takes effect for the rest of this session even if the disk write
 * fails or there is nowhere to write it — a car "vanishing" after
 * being accepted would be a worse surprise than it just not
 * outlasting the session.
 */
static void designer_save(void)
{
    KartSpec finalized;
    char error[128];
    char notice[44];
    int idx;

    designer_finalize(&finalized);
    idx = kart_specs_add_custom(&finalized, error, (int)sizeof(error));
    if (idx < 0) {
        menu_notice(error);
        audio_beep(150.0f, 240, 175);
        return;
    }

    if (!cars_json_path[0]) {
        snprintf(notice, sizeof(notice), "%s ADDED (NOT SAVED)",
                kart_specs[idx].name);
        menu_notice(notice);
        audio_beep(500.0f, 90, 150);
    } else if (!config_save_cars_file(cars_json_path, kart_specs,
                                      kart_spec_count, error,
                                      (int)sizeof(error))) {
        snprintf(notice, sizeof(notice), "%s ADDED (SAVE FAILED)",
                kart_specs[idx].name);
        menu_notice(notice);
        audio_beep(500.0f, 90, 150);
    } else {
        snprintf(notice, sizeof(notice), "%s SAVED TO THE GARAGE",
                kart_specs[idx].name);
        menu_notice(notice);
        audio_beep(920.0f, 70, 150);
    }
    menu_screen = designer_return_screen;
    menu_row = 0;
}

/*
 * APPLY: patch kart_specs[designer_editing_idx] in place with whatever
 * the designer currently holds — session only, never written to disk,
 * so a bad edit or a experiment costs nothing beyond this race meeting.
 * The name is locked to whatever it already was (row_change refuses to
 * touch it in this mode), so there is no duplicate-name case to reject
 * here the way a brand new car has to check for.
 */
static void designer_apply_edit(void)
{
    KartSpec finalized;
    char error[128];
    char notice[44];

    designer_finalize(&finalized);
    snprintf(finalized.name, sizeof(finalized.name), "%s",
            kart_specs[designer_editing_idx].name);
    if (!kart_spec_validate(&finalized, error, (int)sizeof(error))) {
        menu_notice(error);
        audio_beep(150.0f, 240, 175);
        return;
    }
    kart_specs[designer_editing_idx] = finalized;
    snprintf(notice, sizeof(notice), "%s UPDATED FOR THIS SESSION",
            finalized.name);
    menu_notice(notice);
    audio_beep(920.0f, 70, 150);
    menu_screen = designer_return_screen;
    menu_row = 0;
}

static void activate_row(const MenuRow *r)
{
    switch (r->kind) {
    case RK_GARAGE:
        garage_player = r->player;
        menu_screen = SCREEN_GARAGE;
        menu_row = 0;
        audio_beep(880.0f, 60, 140);
        break;
    case RK_EDIT_CAR:
        designer_load_existing(sel_spec[r->player] % kart_spec_count);
        designer_return_screen = SCREEN_GARAGE;
        menu_screen = SCREEN_DESIGNER;
        menu_row = 0;
        audio_beep(880.0f, 60, 140);
        break;
    case RK_CONFIG:
        menu_screen = SCREEN_CONFIG;
        menu_row = 0;
        audio_beep(760.0f, 60, 140);
        break;
    case RK_DESIGN:
        designer_reset();
        designer_return_screen = SCREEN_SETUP;
        menu_screen = SCREEN_DESIGNER;
        menu_row = 0;
        audio_beep(880.0f, 60, 140);
        break;
    case RK_DES_SAVE:
        if (designer_editing_idx >= 0)
            designer_apply_edit();
        else
            designer_save();
        break;
    case RK_DES_CANCEL:
        menu_screen = designer_return_screen;
        menu_row = 0;
        audio_beep(660.0f, 60, 130);
        break;
    case RK_START:
        try_start_race();
        break;
    case RK_EXIT:
        exit(0);
        break;
    case RK_DONE:
        menu_screen = SCREEN_SETUP;
        menu_row = 0;
        audio_beep(660.0f, 60, 130);
        break;
    default:
        /* a setting row: left/right changes it, activating does nothing.
         * This is the whole point — a stray press must not advance. */
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Menu drawing                                                        */
/* ------------------------------------------------------------------ */

static void draw_row_list(float x, float y0, float rowh, float value_x)
{
    char label[24], value[24];
    int i;

    for (i = 0; i < n_menu_rows; i++) {
        float y = y0 + rowh * (float)i;
        int sel = (i == menu_row);
        u8 br = sel ? 255 : 210, bg = sel ? 235 : 215, bb = sel ? 245 : 225;

        if (sel)
            hud_rect(x - 12.0f, y - 4.0f, value_x + 150.0f, rowh - 6.0f,
                     60, 80, 130, 220);
        row_label(&menu_rows[i], label, sizeof(label));
        hud_text(x, y, 11.0f, 19.0f, label, br, bg, bb, 250);

        row_value(&menu_rows[i], value, sizeof(value));
        if (value[0]) {
            hud_text(x + value_x, y, 11.0f, 19.0f, value,
                     255, 220, 60, 250);
            if (sel) {
                /* little arrows, so it is obvious this row is adjustable */
                hud_text(x + value_x - 22.0f, y, 9.0f, 16.0f, "-",
                         200, 205, 220, 220);
                hud_text(x + value_x + hud_text_width(11.0f, value) + 10.0f,
                         y, 9.0f, 16.0f, "-", 200, 205, 220, 220);
            }
        }
    }
}

/*
 * The JSON screen: where the game looked, what it found, whether each
 * file parsed, and a number you can check against the file you edited.
 * If cars.json says SPORT has 210 hp and this screen says 210 HP, it
 * worked; if it says 150, the game never read your file, and the lines
 * above say why.
 */
static void draw_config_screen(void)
{
    float W = (float)rmode->fbWidth;
    float H = (float)rmode->efbHeight;
    static const char *names[3] = { "SETTINGS.JSON", "CARS.JSON",
                                    "CONTROLS.JSON" };
    float y;
    int i;

    hud_ortho_fullscreen();
    hud_rect(0.0f, 0.0f, W, H, 18, 24, 40, 255);
    hud_text(W * 0.5f - hud_text_width(20.0f, "JSON CONFIG") * 0.5f, 22.0f,
             20.0f, 34.0f, "JSON CONFIG", 230, 210, 90, 255);

    y = 84.0f;
    hud_text(48.0f, y, 9.0f, 15.0f, "READ FROM", 150, 158, 175, 220);
    hud_text(170.0f, y, 9.0f, 15.0f,
             config_where[0] ? config_where : "NOWHERE", 225, 228, 238, 235);
    y += 26.0f;

    for (i = 0; i < 3; i++) {
        int ok = (strcmp(config_file_status[i], "LOADED") == 0);
        int missing = (strcmp(config_file_status[i], "NOT FOUND") == 0);
        hud_text(48.0f, y, 9.0f, 15.0f, names[i], 190, 195, 210, 225);
        hud_text(230.0f, y, 9.0f, 15.0f, config_file_status[i],
                 ok ? 120 : (missing ? 170 : 255),
                 ok ? 215 : (missing ? 175 : 150),
                 ok ? 140 : (missing ? 190 : 90), 235);
        y += 20.0f;
    }

    y += 10.0f;
    hud_text(48.0f, y, 9.0f, 15.0f, "IN THE GARAGE NOW", 150, 158, 175, 220);
    y += 18.0f;
    hud_text(48.0f, y, 11.0f, 18.0f, config_proof, 235, 235, 245, 240);
    y += 32.0f;

    /* how to make it work, for the case where it did not */
    hud_text(48.0f, y, 8.0f, 13.0f, "PUT THE CONFIG FOLDER NEXT TO BOOT.DOL",
             150, 158, 175, 210);
    y += 16.0f;
    hud_text(48.0f, y, 8.0f, 13.0f, "SD:/APPS/WIIKART/CONFIG/CARS.JSON",
             150, 158, 175, 210);
    y += 16.0f;
    hud_text(48.0f, y, 8.0f, 13.0f,
             "IN DOLPHIN TURN ON CONFIG - WII - INSERT SD CARD",
             150, 158, 175, 210);

    draw_row_list(48.0f, H - 74.0f, 26.0f, 190.0f);
}

/*
 * The live preview reuses the exact spec-sheet layout the garage
 * overlay already shows for a stock car (see draw_garage_overlay) —
 * same rows, same units — so a player moving between "here's a car"
 * and "here's the one I'm building" is reading the same sheet both
 * times, not learning a second display convention.
 */
static void draw_designer_screen(void)
{
    KartSpec sp;
    float W = (float)rmode->fbWidth;
    float H = (float)rmode->efbHeight;
    float panel_x = W - 260.0f;
    char buf[48];
    float y;

    designer_finalize(&sp);

    hud_ortho_fullscreen();
    hud_rect(0.0f, 0.0f, W, H, 18, 24, 40, 255);
    hud_text(W * 0.5f - hud_text_width(20.0f, "CAR DESIGNER") * 0.5f, 22.0f,
             20.0f, 34.0f, "CAR DESIGNER", 230, 210, 90, 255);

    /* the row list, left column, same as every other screen */
    draw_row_list(48.0f, 90.0f, 28.0f, 190.0f);

    /* a live preview of what SAVE would actually add, right column —
     * same spec-sheet layout draw_garage_overlay shows for a stock car */
    hud_rect(panel_x - 12.0f, 80.0f, 244.0f, 260.0f, 12, 15, 24, 170);
    y = 104.0f;
    snprintf(buf, sizeof(buf), "HP    %d", (int)sp.power_hp);
    hud_text(panel_x, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "CURB  %dLB", (int)(sp.mass_kg * KG_TO_LB));
    hud_text(panel_x, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "0-62  %.1fS",
             (double)spec_accel_time_with_settings(&sp, &app_settings));
    hud_text(panel_x, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "TOP   %d",
             (int)(spec_top_speed_with_settings(&sp, &app_settings) *
                   KPH_TO_MPH));
    hud_text(panel_x, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "62-0  %dFT",
             (int)(spec_brake_dist_100_with_settings(&sp, &app_settings) *
                   M_TO_FT));
    hud_text(panel_x, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "GRIP  %.2fG", (double)sp.lat_g);
    hud_text(panel_x, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "GEARS %d", sp.n_gears);
    hud_text(panel_x, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "DIRT  %d", (int)(sp.offroad_grip * 100.0f));
    hud_text(panel_x, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    if (sp.drivetrain == DRIVETRAIN_AWD)
        snprintf(buf, sizeof(buf), "DRIVE AWD %dF",
                (int)(sp.awd_front_bias * 100.0f));
    else
        snprintf(buf, sizeof(buf), "DRIVE %s", drivetrain_name(sp.drivetrain));
    hud_text(panel_x, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 28.0f;
    hud_text(panel_x, y, 7.0f, 12.0f, "CURB WEIGHT FOLLOWS POWER",
             150, 158, 175, 210); y += 14.0f;
    hud_text(panel_x, y, 7.0f, 12.0f, "WHEELBASE FOLLOWS CURB WEIGHT",
             150, 158, 175, 210);
    if (!cars_json_path[0]) {
        y += 22.0f;
        hud_text(panel_x, y, 7.0f, 12.0f, "NO SD CARD - WON'T PERSIST",
                 255, 175, 90, 220);
    }

    if (menu_msg_t > 0.0f)
        hud_text(W * 0.5f - hud_text_width(11.0f, menu_msg) * 0.5f,
                 H - 40.0f, 11.0f, 19.0f, menu_msg, 255, 120, 90, 250);
}

static void draw_setup_screen(void)
{
    float W = (float)rmode->fbWidth;
    float H = (float)rmode->efbHeight;
    char buf[48], hint[96];
    char up[12], down[12], left[12], right[12], confirm[12], back[12];
    int avail;

    menu_update_track_preview();
    hud_ortho_fullscreen();
    hud_rect(0.0f, 0.0f, W, H, 18, 24, 40, 255);
    hud_text(W * 0.5f - hud_text_width(26.0f, "WIIKART") * 0.5f, 22.0f,
             26.0f, 44.0f, "WIIKART", 230, 40, 40, 255);

    draw_row_list(48.0f, 96.0f, 32.0f, 190.0f);

    /* what hardware we can actually see */
    avail = available_players();
    snprintf(buf, sizeof(buf), "PADS FOUND %d", avail);
    hud_text(48.0f, H - 96.0f, 9.0f, 15.0f, buf,
             (avail < sel_players) ? 255 : 150,
             (avail < sel_players) ? 140 : 200,
             (avail < sel_players) ? 60 : 170, 235);

    if (config_banner[0])
        hud_text(48.0f, H - 122.0f, 7.5f, 13.0f, config_banner,
                 config_detail[0] ? 255 : 135,
                 config_detail[0] ? 135 : 190,
                 config_detail[0] ? 80 : 165, 225);
    if (config_detail[0])
        hud_text(48.0f, H - 106.0f, 6.5f, 11.0f, config_detail,
                 245, 160, 105, 220);

    /* track card */
    snprintf(buf, sizeof(buf), "%d LAPS", menu_track.laps);
    hud_text(W - 236.0f, 100.0f, 10.0f, 17.0f, buf, 210, 215, 225, 240);
    snprintf(buf, sizeof(buf), "%d FT", (int)(menu_track.total_len * M_TO_FT));
    hud_text(W - 236.0f, 124.0f, 10.0f, 17.0f, buf, 200, 205, 215, 235);
    snprintf(buf, sizeof(buf), "RISE %d",
             (int)((menu_track.max_y - menu_track.min_y) * M_TO_FT));
    hud_text(W - 236.0f, 148.0f, 10.0f, 17.0f, buf, 200, 205, 215, 235);
    hud_text(W - 236.0f, 172.0f, 10.0f, 17.0f,
             menu_track.has_walls ? "RAILS" : "NO RAILS",
             menu_track.has_walls ? 150 : 255,
             menu_track.has_walls ? 200 : 150, 170, 235);
    draw_minimap(&menu_track, 0, -1, W - 236.0f, 208.0f, 160.0f);

    if (menu_msg_t > 0.0f)
        hud_text(W * 0.5f - hud_text_width(11.0f, menu_msg) * 0.5f,
                 H - 66.0f, 11.0f, 19.0f, menu_msg, 255, 120, 90, 250);

    snprintf(up, sizeof(up), "%s",
             control_key_name(control_config.keyboard[0][CONTROL_ACCEL][0]));
    snprintf(down, sizeof(down), "%s",
             control_key_name(control_config.keyboard[0][CONTROL_BRAKE][0]));
    snprintf(left, sizeof(left), "%s",
             control_key_name(control_config.keyboard[0][CONTROL_STEER_LEFT][0]));
    snprintf(right, sizeof(right), "%s",
             control_key_name(control_config.keyboard[0][CONTROL_STEER_RIGHT][0]));
    snprintf(confirm, sizeof(confirm), "%s",
             control_key_name(control_config.keyboard[0][CONTROL_MENU_CONFIRM][0]));
    snprintf(back, sizeof(back), "%s",
             control_key_name(control_config.keyboard[0][CONTROL_MENU_BACK][0]));
    /* one line, not two — the setup screen used to spend a whole row each
     * on "how to move" and "how to confirm" */
    snprintf(hint, sizeof(hint), "%s%s LINE  %s%s CHANGE  %s SELECT  %s OUT",
             up, down, left, right, confirm, back);
    hud_text(W * 0.5f - hud_text_width(9.0f, hint) * 0.5f,
             H - 26.0f, 9.0f, 15.0f, hint,
             160, 165, 180, 220);
}

static void draw_garage_overlay(int p)
{
    const KartSpec *sp = &kart_specs[sel_spec[p] % kart_spec_count];
    const u8 *paint = paint_palette[sel_paint[p] % PAINT_COUNT];
    float W = (float)rmode->fbWidth;
    float H = (float)rmode->efbHeight;
    char buf[40];
    float y;
    int i;

    hud_ortho_fullscreen();
    hud_rect(14.0f, 18.0f, 268.0f, H - 74.0f, 12, 15, 24, 170);

    snprintf(buf, sizeof(buf), "P%d GARAGE", p + 1);
    hud_text(30.0f, 28.0f, 12.0f, 21.0f, buf, paint[0], paint[1], paint[2],
             255);

    draw_row_list(30.0f, 62.0f, 30.0f, 132.0f);

    /* spec sheet for the chosen car, including its gearbox */
    y = 224.0f;
    snprintf(buf, sizeof(buf), "HP    %d", (int)sp->power_hp);
    hud_text(30.0f, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "CURB  %dLB", (int)(sp->mass_kg * KG_TO_LB));
    hud_text(30.0f, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "0-62  %.1fS",
             spec_accel_time_with_settings(sp, &app_settings));
    hud_text(30.0f, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "TOP   %d",
             (int)(spec_top_speed_with_settings(sp, &app_settings) *
                   KPH_TO_MPH));
    hud_text(30.0f, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "62-0  %dFT",
             (int)(spec_brake_dist_100_with_settings(sp, &app_settings) *
                   M_TO_FT));
    hud_text(30.0f, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "GRIP  %.2fG",
             sp->lat_g *
                 tire_grip_mult_with_settings(&app_settings, sel_tire[p]));
    hud_text(30.0f, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "GEARS %d", sp->n_gears);
    hud_text(30.0f, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "DIRT  %d",
             (int)(sp->offroad_grip * 100.0f));
    hud_text(30.0f, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    if (sp->drivetrain == DRIVETRAIN_AWD)
        snprintf(buf, sizeof(buf), "DRIVE AWD %dF",
                 (int)(sp->awd_front_bias * 100.0f));
    else
        snprintf(buf, sizeof(buf), "DRIVE %s",
                 sp->drivetrain == DRIVETRAIN_FWD ? "FWD" : "RWD");
    hud_text(30.0f, y, 9.0f, 15.0f, buf, 205, 210, 220, 250);

    /* the gearing itself, as a row of bars: how tall each gear is */
    {
        float bx = W - 250.0f, by = H - 132.0f;
        float top = sp->gear_top[sp->n_gears - 1];
        hud_text(bx, by - 22.0f, 9.0f, 15.0f, "GEARING",
                 150, 158, 175, 235);
        for (i = 0; i < sp->n_gears; i++) {
            float wid = 190.0f * (sp->gear_top[i] / top);
            hud_rect(bx, by + (float)i * 15.0f, wid, 11.0f,
                     90, 160 + (u8)(i * 12), 235, 235);
        }
    }

    /* paint swatches */
    {
        float sx = W - 250.0f, sy = H - 190.0f;
        for (i = 0; i < PAINT_COUNT; i++) {
            const u8 *c = paint_palette[i];
            if (i == sel_paint[p] % PAINT_COUNT)
                hud_rect(sx + (float)i * 24.0f - 3.0f, sy - 3.0f, 26.0f,
                         26.0f, 255, 255, 255, 235);
            hud_rect(sx + (float)i * 24.0f, sy, 20.0f, 20.0f,
                     c[0], c[1], c[2], 255);
        }
    }

    if (menu_msg_t > 0.0f)
        hud_text(W * 0.5f - hud_text_width(11.0f, menu_msg) * 0.5f,
                 H - 62.0f, 11.0f, 19.0f, menu_msg, 255, 120, 90, 250);
    hud_text(W * 0.5f - hud_text_width(9.0f, "ESC  OUT") * 0.5f,
             H - 26.0f, 9.0f, 15.0f, "ESC  OUT", 160, 165, 180, 220);
}

static void menu_frame(float dt)
{
    int dcur, dval;

    if (menu_msg_t > 0.0f)
        menu_msg_t -= dt;

    /* how long a menu key has been held, for the auto-repeat */
    if (key_actions[0][CONTROL_STEER_LEFT] ||
        key_actions[0][CONTROL_STEER_RIGHT] ||
        key_actions[0][CONTROL_ACCEL] || key_actions[0][CONTROL_BRAKE])
        key_repeat_t += dt;
    else
        key_repeat_t = 0.0f;

    build_rows();
    dcur = menu_dcursor();
    dval = menu_dvalue();

    if (dcur) {
        menu_row = (menu_row + dcur + n_menu_rows) % n_menu_rows;
        audio_beep(420.0f, 30, 80);
    }
    if (dval) {
        row_change(&menu_rows[menu_row], dval);
        build_rows();      /* PLAYERS changes how many rows there are */
        audio_beep(500.0f, 30, 90);
    }
    if (menu_activate())
        activate_row(&menu_rows[menu_row]);
    else if (menu_back()) {
        if (menu_screen != SCREEN_SETUP) {
            menu_screen = SCREEN_SETUP;
            menu_row = 0;
            audio_beep(560.0f, 50, 120);
        } else {
            /* at the root: put the cursor on GO rather than trapping you */
            menu_row = n_menu_rows - 2;
        }
    }

    if (app_state == APP_RACE)
        return;                    /* start_race() already switched away */

    build_rows();
    if (menu_screen == SCREEN_GARAGE) {
        draw_garage_scene(sel_paint[garage_player],
                          &kart_specs[sel_spec[garage_player] %
                                     kart_spec_count]);
        draw_garage_overlay(garage_player);
    } else if (menu_screen == SCREEN_CONFIG) {
        draw_config_screen();
    } else if (menu_screen == SCREEN_DESIGNER) {
        draw_designer_screen();
    } else {
        draw_setup_screen();
    }
}

static void draw_race_views(float dt)
{
    int p;
    for (p = 0; p < game.cfg.n_humans; p++)
        draw_scene_for_player(p, dt);
    draw_race_hud();
}

static void race_frame(float dt)
{
    Input in[MAX_HUMANS];
    int p;

    /* Opening and answering live in separate frames.  Besides feeling
     * deliberate, this prevents a custom mapping that puts MENU and YES
     * on the same physical button from confirming its own question. The
     * driving-input filter stays frozen along with the simulation. */
    if (race_exit_confirm) {
        if (race_confirm_pressed()) {
            audio_beep(180.0f, 90, 140);
            leave_race_for_menu();
            return;
        }
        if (race_cancel_pressed() || race_to_menu_pressed()) {
            race_exit_confirm = 0;
            audio_beep(720.0f, 70, 135);
        }

        draw_race_views(0.0f);       /* paused: the camera holds too */
        if (race_exit_confirm)
            draw_race_exit_confirmation();
        return;
    }

    if (race_to_menu_pressed()) {
        race_exit_confirm = 1;
        stop_all_rumble();
        audio_beep(260.0f, 90, 140);
        draw_race_views(0.0f);
        draw_race_exit_confirmation();
        return;
    }

    for (p = 0; p < MAX_HUMANS; p++)
        read_player_input(p, &in[p], dt);

    /*
     * A controller going away mid-race must not leave a player as a
     * statue: after a moment's grace (so a brief radio dropout does not
     * count) the race is abandoned and we go back to the menu with an
     * explanation, where you can try again.
     */
    if (available_players() < game.cfg.n_humans) {
        controller_lost_t += dt;
        if (controller_lost_t > 0.75f) {
            controller_lost_t = 0.0f;
            menu_notice("CONTROLLER LOST");
            audio_beep(140.0f, 300, 180);
            leave_race_for_menu();
            return;
        }
    } else {
        controller_lost_t = 0.0f;
    }

    game_update(&game, in, dt);

    /* countdown beeps */
    if (game.state == STATE_COUNTDOWN) {
        int n = (int)ceilf(game.countdown - 0.2f);
        if (n != prev_countdown_n && n >= 1 && n <= 3)
            audio_beep(440.0f, 120, 170);
        prev_countdown_n = n;
    } else if (prev_countdown_n != -1) {
        audio_beep(880.0f, 260, 190);
        prev_countdown_n = -1;
    }

    /* per-player rumble + effect sounds (P1 sounds only) */
    for (p = 0; p < game.cfg.n_humans; p++) {
        const Kart *k = &game.karts[p];
        if (k->hit_wall || k->respawned)
            rumble_t[p] = k->respawned ? 0.30f : 0.18f;
        if (p == 0) {
            if (k->hit_wall)     audio_beep(110.0f, 120, 190);
            if (k->respawned)    audio_beep(520.0f, 260, 170);
        }
        if (rumble_t[p] > 0.0f) {
            rumble_t[p] -= dt;
            WPAD_Rumble(p, 1);
            PAD_ControlMotor(p, PAD_MOTOR_RUMBLE);
        } else {
            WPAD_Rumble(p, 0);
            PAD_ControlMotor(p, PAD_MOTOR_STOP);
        }
    }

    lap_popups_update(dt);

    /* blank the unused quadrant in 3P before HUD overlays it */
    draw_race_views(dt);
}

/* ------------------------------------------------------------------ */
/* Setup and main loop                                                 */
/* ------------------------------------------------------------------ */

/*
 * Pick the sharpest picture the console can actually send.
 *
 * The Wii's framebuffer is 640 wide whatever happens — that is the
 * hardware, and no software can raise it. What software can do is stop
 * throwing half of it away: with a component cable and progressive scan
 * enabled in the Wii's own settings, 480p draws every line every frame
 * instead of alternating fields, which removes the interlace shimmer and
 * lets the deflicker filter be turned off, so edges stay crisp instead of
 * being blurred vertically on purpose.
 *
 * (In Dolphin the same choice matters, and on top of it the emulator's
 * own Internal Resolution setting can render this scene at 1080p or 4K.
 * See the README.)
 */
static void video_setup(void)
{
    GXRModeObj *pref = VIDEO_GetPreferredMode(NULL);
    int progressive = VIDEO_HaveComponentCable() && CONF_GetProgressiveScan() > 0;

    rmode = pref;
    if (progressive) {
        switch (pref->viTVMode >> 2) {
        case VI_NTSC:     rmode = &TVNtsc480Prog;      break;
        case VI_EURGB60:  rmode = &TVEurgb60Hz480Prog; break;
        default:          rmode = pref;                break;  /* PAL 576i
                                                                * and MPAL
                                                                * stay put */
        }
    }

    /*
     * 16:9 is not a wider framebuffer, it is the same one stretched by the
     * TV — so the fix is to render a 16:9 field of view instead of letting
     * a 4:3 image be pulled sideways. Everything round stays round.
     */
    video_widescreen = (CONF_GetAspectRatio() == CONF_ASPECT_16_9);

    VIDEO_Configure(rmode);
}

int main(int argc, char **argv)
{
    void *gp_fifo;
    GXColor sky = { 120, 175, 235, 255 };
    f32 yscale;
    u32 xfbHeight;
    float dt;

    VIDEO_Init();
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC);
    WPAD_SetDataFormat(WPAD_CHAN_1, WPAD_FMT_BTNS_ACC);
    WPAD_SetDataFormat(WPAD_CHAN_2, WPAD_FMT_BTNS_ACC);
    WPAD_SetDataFormat(WPAD_CHAN_3, WPAD_FMT_BTNS_ACC);
    PAD_Init();
    if (KEYBOARD_Init(NULL) >= 0)
        keyboard_ok = 1;
    load_editable_config(argc, argv);

    video_setup();
    frameBuffer[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    frameBuffer[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

    VIDEO_SetNextFramebuffer(frameBuffer[fb]);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (rmode->viTVMode & VI_NON_INTERLACE)
        VIDEO_WaitVSync();

    gp_fifo = memalign(32, DEFAULT_FIFO_SIZE);
    memset(gp_fifo, 0, DEFAULT_FIFO_SIZE);
    GX_Init(gp_fifo, DEFAULT_FIFO_SIZE);

    GX_SetCopyClear(sky, 0x00ffffff);

    GX_SetViewport(0.0f, 0.0f, (f32)rmode->fbWidth, (f32)rmode->efbHeight,
                   0.0f, 1.0f);
    yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
    xfbHeight = GX_SetDispCopyYScale(yscale);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
    GX_SetDispCopyDst(rmode->fbWidth, (u16)xfbHeight);
    GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE,
                     rmode->vfilter);
    GX_SetFieldMode(rmode->field_rendering,
                    ((rmode->viHeight == 2 * rmode->xfbHeight)
                         ? GX_ENABLE : GX_DISABLE));

    if (rmode->aa)
        GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
    else
        GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);

    GX_SetCullMode(GX_CULL_NONE);
    GX_SetDispCopyGamma(GX_GM_1_0);

    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
    GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

    GX_SetNumChans(1);
    GX_SetNumTexGens(0);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL,
                   GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);

    GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA,
                    GX_LO_CLEAR);
    GX_SetAlphaUpdate(GX_FALSE);
    GX_SetColorUpdate(GX_TRUE);

    audio_init();

    dt = 1.0f / 60.0f;
    if ((rmode->viTVMode >> 2) == VI_PAL)
        dt = 1.0f / 50.0f;

    while (1) {
        poll_all_inputs();

        if (app_state == APP_MENU)
            menu_frame(dt);
        /* not an else: starting a race mid-frame draws its first frame
         * here rather than leaving the screen blank for one flip */
        if (app_state == APP_RACE)
            race_frame(dt);

        audio_update();

        GX_DrawDone();

        GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
        GX_SetColorUpdate(GX_TRUE);
        GX_CopyDisp(frameBuffer[fb], GX_TRUE);

        VIDEO_SetNextFramebuffer(frameBuffer[fb]);
        VIDEO_Flush();
        VIDEO_WaitVSync();
        fb ^= 1;
        frame_no++;
    }

    return 0;
}
