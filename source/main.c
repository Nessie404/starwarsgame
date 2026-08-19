/*
 * WiiKart — Wii platform layer.
 *
 * Everything libogc-specific lives here: video / GX setup, flat-shaded
 * 3D rendering (elevation, mountainside skirts, guardrails, item
 * boxes), menus, up-to-4-player split screen, procedural ASND audio
 * (engine, tire squeal, beeps) and input from Wiimote (tilt / D-pad /
 * Nunchuk / Classic Controller), GameCube pads and USB keyboards.
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

/* app flow */
enum { APP_MENU = 0, APP_RACE = 1 };
static int app_state = APP_MENU;
static int race_exit_confirm;       /* leave-race guard; pauses simulation */
static int menu_screen;              /* SCREEN_SETUP / SCREEN_GARAGE     */
static int sel_players = 1;
static int sel_track = 0;
static int sel_laps = 0;             /* 0 = the circuit's own lap count */
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

static void load_editable_config(int argc, char **argv)
{
    char roots[4][256];
    char path[320];
    char error[48];
    const char *root = NULL;
    int n_roots = 0, i, loaded = 0, failed = 0;

    game_settings_defaults(&app_settings);
    control_config_defaults(&control_config);
    kart_specs_reset_defaults();
    config_banner[0] = config_detail[0] = '\0';

    /* libfat is optional at runtime. Directly opening the DOL in Dolphin
     * still works with compiled defaults; a virtual SD card or real Wii
     * makes the adjacent JSON files editable without rebuilding. */
    (void)fatInitDefault();

    if (argc > 0 && argv && argv[0] && strchr(argv[0], '/')) {
        char *slash;
        snprintf(roots[n_roots], sizeof(roots[n_roots]), "%s", argv[0]);
        slash = strrchr(roots[n_roots], '/');
        if (slash) {
            *slash = '\0';
            n_roots++;
        }
    }
    snprintf(roots[n_roots++], sizeof(roots[0]), "sd:/apps/wiikart");
    snprintf(roots[n_roots++], sizeof(roots[0]), "usb:/apps/wiikart");
    snprintf(roots[n_roots++], sizeof(roots[0]), ".");

    for (i = 0; i < n_roots; i++) {
        if ((make_config_path(path, (int)sizeof(path), roots[i],
                              "settings.json") && readable_file(path)) ||
            (make_config_path(path, (int)sizeof(path), roots[i],
                              "cars.json") && readable_file(path)) ||
            (make_config_path(path, (int)sizeof(path), roots[i],
                              "controls.json") && readable_file(path))) {
            root = roots[i];
            break;
        }
    }

    if (!root) {
        snprintf(config_banner, sizeof(config_banner), "BUILT IN CONFIG");
        return;
    }

#define LOAD_ONE(filename, call)                                           \
    do {                                                                   \
        if (make_config_path(path, (int)sizeof(path), root, filename) &&   \
            readable_file(path)) {                                         \
            if (call) loaded++;                                            \
            else {                                                         \
                failed++;                                                  \
                if (!config_detail[0])                                     \
                    snprintf(config_detail, sizeof(config_detail), "%s", \
                             error);                                       \
            }                                                              \
        }                                                                  \
    } while (0)

    LOAD_ONE("settings.json",
             config_load_settings_file(&app_settings, path, error,
                                       (int)sizeof(error)));
    LOAD_ONE("cars.json",
             config_load_cars_file(path, error, (int)sizeof(error)));
    LOAD_ONE("controls.json",
             config_load_controls_file(&control_config, path, error,
                                       (int)sizeof(error)));
#undef LOAD_ONE

    if (failed)
        snprintf(config_banner, sizeof(config_banner), "CONFIG ERROR");
    else
        snprintf(config_banner, sizeof(config_banner), "CONFIG %d/3", loaded);
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
 *   Player 1: W/A/S/D drive, E up a gear, Q down a gear, X power-up,
 *             Space handbrake.
 *   Player 2: I/J/K/L drive, O up a gear, U down a gear, M power-up,
 *             P handbrake.
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
    in->item  = (wheld[p] & (WPAD_BUTTON_MINUS |
                             WPAD_CLASSIC_BUTTON_MINUS)) != 0;
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
        in->hop   |= (gc & control_config.gamecube[CONTROL_HANDBRAKE]) != 0;
        in->item  |= (gc & control_config.gamecube[CONTROL_ITEM]) != 0;
        in->gear_up |= (gc & control_config.gamecube[CONTROL_GEAR_UP]) != 0;
        in->gear_down |=
            (gc & control_config.gamecube[CONTROL_GEAR_DOWN]) != 0;
    }

    /* USB keyboard bindings come from the same file. */
    if (p < CONTROL_KEYBOARD_PLAYERS) {
        if (key_actions[p][CONTROL_STEER_LEFT])  steer += STEER_LEFT;
        if (key_actions[p][CONTROL_STEER_RIGHT]) steer += STEER_RIGHT;
        in->accel |= key_actions[p][CONTROL_ACCEL];
        in->brake |= key_actions[p][CONTROL_BRAKE];
        in->hop |= key_actions[p][CONTROL_HANDBRAKE];
        in->item |= key_actions[p][CONTROL_ITEM];
        in->gear_up |= key_actions[p][CONTROL_GEAR_UP];
        in->gear_down |= key_actions[p][CONTROL_GEAR_DOWN];
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
    if (k->push_t > 0.0f) pitch = (int)(pitch * 1.10f);
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

/* box rotated by yaw (about Y) then pitched (nose up positive) */
static void draw_box(float cx, float cy, float cz, float yaw, float pitch,
                     float hfw, float hh, float hlat,
                     u8 r, u8 g, u8 b)
{
    float cf = cosf(yaw), sf = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    float fx = cf * cp, fy = sp, fz = sf * cp;      /* forward */
    float lx = -sf,     ly = 0.0f, lz = cf;          /* lateral (left) */
    float ux = -sp * cf, uy = cp, uz = -sp * sf;     /* up */
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

static void draw_track(const Track *t, int viewer_seg)
{
    const float RW = t->road_half;
    int win_ahead, win_behind;
    int i;

    view_window(&win_ahead, &win_behind);

    /* valley floor */
    {
        u8 r = t->alpine ? 84 : 58, g = t->alpine ? 120 : 142,
           b = t->alpine ? 70 : 60;
        float gy = t->min_y - (t->alpine ? 8.0f : 0.02f);
        quad(t->min_x - 120.0f, gy, t->min_z - 120.0f,
             t->max_x + 120.0f, gy, t->min_z - 120.0f,
             t->max_x + 120.0f, gy, t->max_z + 120.0f,
             t->min_x - 120.0f, gy, t->max_z + 120.0f,
             r, g, b, 255);
    }

    for (i = 0; i < t->n; i++) {
        int in = (i + 1) % t->n;
        float l0x = -t->dz[i],  l0z = t->dx[i];
        float l1x = -t->dz[in], l1z = t->dx[in];
        float y0 = t->py[i] + 0.06f, y1 = t->py[in] + 0.06f;
        u8 r, g, b;

        if (!seg_in_window(t, viewer_seg, i, win_ahead, win_behind))
            continue;

        if (i & 1) {
            r = 95; g = 95; b = 100;
        } else {
            r = 85; g = 85; b = 90;
        }

        /* road surface */
        quad(t->px[i]  + l0x * RW, y0, t->pz[i]  + l0z * RW,
             t->px[in] + l1x * RW, y1, t->pz[in] + l1z * RW,
             t->px[in] - l1x * RW, y1, t->pz[in] - l1z * RW,
             t->px[i]  - l0x * RW, y0, t->pz[i]  - l0z * RW,
             r, g, b, 255);

        /* curbs / shoulder stripe */
        if (i & 1) { r = 210; g = 40; b = 40; }
        else       { r = 235; g = 235; b = 235; }
        quad(t->px[i]  + l0x * (RW + 0.9f), y0, t->pz[i]  + l0z * (RW + 0.9f),
             t->px[in] + l1x * (RW + 0.9f), y1, t->pz[in] + l1z * (RW + 0.9f),
             t->px[in] + l1x * RW,          y1, t->pz[in] + l1z * RW,
             t->px[i]  + l0x * RW,          y0, t->pz[i]  + l0z * RW,
             r, g, b, 255);
        quad(t->px[i]  - l0x * RW,          y0, t->pz[i]  - l0z * RW,
             t->px[in] - l1x * RW,          y1, t->pz[in] - l1z * RW,
             t->px[in] - l1x * (RW + 0.9f), y1, t->pz[in] - l1z * (RW + 0.9f),
             t->px[i]  - l0x * (RW + 0.9f), y0, t->pz[i]  - l0z * (RW + 0.9f),
             r, g, b, 255);

        if (t->alpine) {
            /* mountainside skirts falling away from the shoulder */
            float e0 = RW + 0.9f, e1 = RW + 12.0f, e2 = RW + 34.0f;
            float d1 = 7.0f, d2 = 22.0f;
            int side;
            for (side = -1; side <= 1; side += 2) {
                float s = (float)side;
                u8 rr = 122, gg = 108, bb = 92;   /* rock */
                quad(t->px[i]  + l0x * e0 * s, y0 - 0.02f,
                     t->pz[i]  + l0z * e0 * s,
                     t->px[in] + l1x * e0 * s, y1 - 0.02f,
                     t->pz[in] + l1z * e0 * s,
                     t->px[in] + l1x * e1 * s, y1 - d1,
                     t->pz[in] + l1z * e1 * s,
                     t->px[i]  + l0x * e1 * s, y0 - d1,
                     t->pz[i]  + l0z * e1 * s,
                     rr, gg, bb, 255);
                rr = 96; gg = 104; bb = 78;       /* scrub below */
                quad(t->px[i]  + l0x * e1 * s, y0 - d1,
                     t->pz[i]  + l0z * e1 * s,
                     t->px[in] + l1x * e1 * s, y1 - d1,
                     t->pz[in] + l1z * e1 * s,
                     t->px[in] + l1x * e2 * s, y1 - d2,
                     t->pz[in] + l1z * e2 * s,
                     t->px[i]  + l0x * e2 * s, y0 - d2,
                     t->pz[i]  + l0z * e2 * s,
                     rr, gg, bb, 255);
            }
            /* guardrails, where this road has them */
            if (t->has_walls) {
                float w = t->wall_half - 0.2f;
                u8 rr = 225, gg = 228, bb = 232;
                if (i & 1) { rr = 180; gg = 184; bb = 190; }
                quad(t->px[i]  + l0x * w, y0 + 0.15f, t->pz[i]  + l0z * w,
                     t->px[in] + l1x * w, y1 + 0.15f, t->pz[in] + l1z * w,
                     t->px[in] + l1x * w, y1 + 0.75f, t->pz[in] + l1z * w,
                     t->px[i]  + l0x * w, y0 + 0.75f, t->pz[i]  + l0z * w,
                     rr, gg, bb, 255);
                quad(t->px[i]  - l0x * w, y0 + 0.15f, t->pz[i]  - l0z * w,
                     t->px[in] - l1x * w, y1 + 0.15f, t->pz[in] - l1z * w,
                     t->px[in] - l1x * w, y1 + 0.75f, t->pz[in] - l1z * w,
                     t->px[i]  - l0x * w, y0 + 0.75f, t->pz[i]  - l0z * w,
                     rr, gg, bb, 255);
            } else {
                /* No barrier: mark the edge with a stripe and drop the
                 * ground away sharply, so the cliff reads as a cliff */
                float w = t->wall_half;
                int side;
                for (side = -1; side <= 1; side += 2) {
                    float sg = (float)side;
                    u8 er = (i & 1) ? 235 : 90, eg = (i & 1) ? 180 : 90;
                    quad(t->px[i]  + l0x * (w - 0.5f) * sg, y0 + 0.02f,
                         t->pz[i]  + l0z * (w - 0.5f) * sg,
                         t->px[in] + l1x * (w - 0.5f) * sg, y1 + 0.02f,
                         t->pz[in] + l1z * (w - 0.5f) * sg,
                         t->px[in] + l1x * w * sg, y1 + 0.02f,
                         t->pz[in] + l1z * w * sg,
                         t->px[i]  + l0x * w * sg, y0 + 0.02f,
                         t->pz[i]  + l0z * w * sg,
                         er, eg, 70, 255);
                    quad(t->px[i]  + l0x * w * sg, y0,
                         t->pz[i]  + l0z * w * sg,
                         t->px[in] + l1x * w * sg, y1,
                         t->pz[in] + l1z * w * sg,
                         t->px[in] + l1x * (w + 1.5f) * sg, y1 - 26.0f,
                         t->pz[in] + l1z * (w + 1.5f) * sg,
                         t->px[i]  + l0x * (w + 1.5f) * sg, y0 - 26.0f,
                         t->pz[i]  + l0z * (w + 1.5f) * sg,
                         84, 74, 64, 255);
                }
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
                float w0 = -RW + 2.0f * RW * (float)cx / 6.0f;
                float w1 = -RW + 2.0f * RW * (float)(cx + 1) / 6.0f;
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
        draw_box(t->px[0] + lx * (RW + 1.8f), by + 2.75f,
                 t->pz[0] + lz * (RW + 1.8f), yaw, 0.0f,
                 0.4f, 2.75f, 0.4f, 225, 225, 230);
        draw_box(t->px[0] - lx * (RW + 1.8f), by + 2.75f,
                 t->pz[0] - lz * (RW + 1.8f), yaw, 0.0f,
                 0.4f, 2.75f, 0.4f, 225, 225, 230);
        draw_box(t->px[0], by + 5.9f, t->pz[0], yaw, 0.0f,
                 0.3f, 0.55f, RW + 2.2f, 200, 30, 30);
    }

    /* item boxes: floating spinning cubes, hidden while respawning */
    {
        int rrow, b;
        for (rrow = 0; rrow < t->n_items; rrow++) {
            int seg = t->item_seg[rrow];
            float lx = -t->dz[seg], lz = t->dx[seg];
            if (!seg_in_window(t, viewer_seg, seg, win_ahead, win_behind))
                continue;
            for (b = 0; b < 3; b++) {
                float blat = ((float)b - 1.0f) * 0.55f * RW;
                float pulse;
                if (app_state == APP_RACE &&
                    game.item_respawn[rrow][b] > 0.0f)
                    continue;
                pulse = 0.8f + 0.2f * sinf((float)frame_no * 0.11f + b);
                /* amber = push-to-pass, green = fresh rubber; the panel
                 * always holds the same thing so drivers can aim */
                if (((rrow + b) & 1) == 0)
                    draw_box(t->px[seg] + lx * blat, t->py[seg] + 1.0f,
                             t->pz[seg] + lz * blat,
                             (float)frame_no * 0.05f + (float)b, 0.0f,
                             0.45f, 0.45f, 0.45f,
                             shade(240, pulse), shade(170, pulse),
                             shade(50, pulse));
                else
                    draw_box(t->px[seg] + lx * blat, t->py[seg] + 1.0f,
                             t->pz[seg] + lz * blat,
                             (float)frame_no * 0.05f + (float)b, 0.0f,
                             0.45f, 0.45f, 0.45f,
                             shade(80, pulse), shade(210, pulse),
                             shade(110, pulse));
            }
        }
    }

    /* trees (near ones only) and the far peaks, which are always drawn
     * because they are the horizon */
    for (i = 0; i < n_trees; i++) {
        float ddx = tree_x[i] - t->px[viewer_seg];
        float ddz = tree_z[i] - t->pz[viewer_seg];
        if (ddx * ddx + ddz * ddz > 150.0f * 150.0f)
            continue;
        draw_box(tree_x[i], tree_y[i] + 0.6f, tree_z[i], 0.0f, 0.0f,
                 0.25f, 0.6f, 0.25f, 110, 75, 40);
        draw_cone(tree_x[i], tree_y[i] + 1.2f, tree_z[i], 1.7f, 3.4f,
                  t->alpine ? 24 : 30, t->alpine ? 100 : 130, 45);
    }
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
static void draw_car_model(float cx, float cy, float cz, float yaw,
                           float pitch, float steer_vis, const u8 col[3])
{
    float fx = cosf(yaw), fz = sinf(yaw);
    float lx = -fz, lz = fx;
    int w;

    draw_box(cx, cy + 0.42f, cz, yaw, pitch,
             1.10f, 0.28f, 0.65f, col[0], col[1], col[2]);
    draw_box(cx - fx * 0.25f, cy + 0.92f, cz - fz * 0.25f, yaw, pitch,
             0.30f, 0.26f, 0.30f, 40, 40, 45);

    for (w = 0; w < 4; w++) {
        float s_f = (w < 2) ? 1.0f : -1.0f;
        float s_l = (w & 1) ? 1.0f : -1.0f;
        float wyaw = yaw + ((w < 2) ? steer_vis * 0.45f : 0.0f);
        draw_box(cx + fx * 0.85f * s_f + lx * 0.72f * s_l, cy + 0.30f,
                 cz + fz * 0.85f * s_f + lz * 0.72f * s_l,
                 wyaw, 0.0f, 0.30f, 0.30f, 0.14f, 25, 25, 28);
    }
}

static void draw_kart(const Track *t, const Kart *k)
{
    const u8 *col = kart_color(k);
    float yaw = k->heading + (k->drifting ? (float)k->drifting * 0.30f : 0.0f)
                + k->steer_vis * 0.05f + k->slip * 0.10f *
                  (k->steer_vis > 0.0f ? -1.0f : 1.0f);
    float dirdot = cosf(k->heading) * t->dx[k->seg] +
                   sinf(k->heading) * t->dz[k->seg];
    float pitch = atanf(t->slope[k->seg] * dirdot);
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

    draw_car_model(k->x, by, k->z, yaw, pitch, k->steer_vis, col);

    if (k->push_t > 0.0f) {
        float len = 1.1f * (0.7f + 0.3f * ((frame_no & 2) ? 1.0f : 0.4f));
        u8 fr = 255, fg = (frame_no & 2) ? 170 : 110;
        GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
        GX_Position3f32(k->x - fx * 1.15f + lx * 0.35f, by + 0.45f,
                        k->z - fz * 1.15f + lz * 0.35f);
        GX_Color4u8(fr, fg, 20, 230);
        GX_Position3f32(k->x - fx * 1.15f - lx * 0.35f, by + 0.45f,
                        k->z - fz * 1.15f - lz * 0.35f);
        GX_Color4u8(fr, fg, 20, 230);
        GX_Position3f32(k->x - fx * (1.15f + len), by + 0.42f,
                        k->z - fz * (1.15f + len));
        GX_Color4u8(255, 240, 90, 200);
        GX_End();
    }

    /* tire smoke when sliding hard; tinted while on fresh rubber */
    if (k->slip > 0.35f && fabsf(k->speed) > 5.0f) {
        u8 sr, sg, sb;
        float jx = 0.15f * sinf((float)frame_no * 1.7f);
        if (k->grip_t > 0.0f) {
            sr = 150; sg = 220; sb = 165;
        } else {
            sr = 200; sg = 200; sb = 205;
        }
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

    draw_track(t, k->seg);
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

static void draw_minimap(const Track *t, int with_karts,
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
                        oy + (t->max_z - t->pz[j]) * scale, -5.0f);
        GX_Color4u8(240, 240, 240, 200);
    }
    GX_End();

    if (!with_karts)
        return;
    for (i = NUM_KARTS - 1; i >= 0; i--) {
        const Kart *k = &game.karts[i];
        const u8 *c = kart_color(k);
        float mx = ox + (k->x - t->min_x) * scale;
        float my = oy + (t->max_z - k->z) * scale;
        float s = (k->human >= 0) ? 5.0f : 4.0f;
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
        }
    }
    /* position */
    snprintf(buf, sizeof(buf), "P%d", k->rank);
    hud_text(vx + vw - 60.0f, vy + 12.0f, 13.0f, 22.0f, buf,
             255, 220, 60, 240);

    /* speed, km/h — turns amber while push-to-pass is deployed */
    snprintf(buf, sizeof(buf), "%d", (int)(fabsf(k->speed) * 3.6f));
    hud_text(vx + 14.0f, vy + vh - 40.0f, 13.0f, 24.0f, buf,
             k->push_t > 0.0f ? 255 : 235,
             k->push_t > 0.0f ? 170 : 235,
             k->push_t > 0.0f ? 40 : 235, 235);

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

    /* gear and revs: the gear number, and a bar for where in the band it
     * is, so a manual driver can see when to shift */
    {
        const KartSpec *sp = &kart_specs[k->spec];
        float rev = game_clampf(k->rev_frac, 0.0f, 1.0f);
        float bog = game.settings.bog_fraction;
        u8 rr = 90, rg = 210, rb = 120;
        int rpm;

        /*
         * The band is read at a glance, so its colours are the four
         * things that matter rather than a gradient: grey while the
         * engine is bogging below its torque band, green through the
         * useful range, amber in the shift window, red at the limiter.
         */
        if (k->rev_frac > 0.94f)      { rr = 255; rg =  80; rb =  65; }
        else if (k->rev_frac > 0.86f) { rr = 245; rg = 200; rb =  70; }
        else if (k->rev_frac < bog)   { rr = 140; rg = 150; rb = 165; }

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

    draw_leaderboard(p, vx, vy, vw, vh);
    draw_lap_popup(p, vx, vy, vw, vh);

    /* just been fished out of the void */
    if (k->fall_t > 0.0f)
        hud_text(vx + vw * 0.5f - 60.0f, vy + vh * 0.5f, 14.0f, 24.0f,
                 "FALLING", 255, 120, 90, 240);

    /* power-up in reserve, and the timer while one is deployed */
    if (k->power_held) {
        const char *nm = power_name(k->power_held);
        if (k->power_held == POWER_PUSH)
            hud_text(vx + vw - 96.0f, vy + vh - 36.0f, 10.0f, 17.0f, nm,
                     240, 180, 60, 235);
        else
            hud_text(vx + vw - 96.0f, vy + vh - 36.0f, 10.0f, 17.0f, nm,
                     110, 225, 140, 235);
    }
    if (k->push_t > 0.0f || k->grip_t > 0.0f) {
        float frac = (k->push_t > 0.0f)
                         ? k->push_t / game.settings.push_seconds
                         : k->grip_t / game.settings.fresh_tire_seconds;
        u8 cr = (k->push_t > 0.0f) ? 240 : 110;
        u8 cg = (k->push_t > 0.0f) ? 175 : 225;
        u8 cb = (k->push_t > 0.0f) ?  55 : 140;
        hud_rect(vx + 14.0f, vy + vh - 12.0f, 90.0f, 6.0f, 15, 15, 20, 160);
        hud_rect(vx + 15.0f, vy + vh - 11.0f,
                 88.0f * game_clampf(frac, 0.0f, 1.0f), 4.0f, cr, cg, cb, 230);
    }
}

static int displayed_action_on(int p, int action)
{
    const Input *in = &shown_input[p];
    switch (action) {
    case CONTROL_ACCEL:     return in->accel;
    case CONTROL_BRAKE:     return in->brake;
    case CONTROL_HANDBRAKE: return in->hop;
    case CONTROL_ITEM:      return in->item;
    case CONTROL_GEAR_UP:   return in->gear_up;
    case CONTROL_GEAR_DOWN: return in->gear_down;
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

    hud_rect(x - 6.0f, y - 8.0f, 414.0f, 142.0f,
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
    draw_binding_line(p, x, y, "ITEM", CONTROL_ITEM); y += 14.0f;
    draw_binding_line(p, x, y, "UP", CONTROL_GEAR_UP); y += 14.0f;
    draw_binding_line(p, x, y, "DOWN", CONTROL_GEAR_DOWN); y += 14.0f;
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

    /* minimap: corner in 1P, spare quadrant in 3P */
    if (game.cfg.n_humans == 1)
        draw_minimap(&game.track, 1, W - 130.0f, H - 140.0f, 100.0f);
    else if (game.cfg.n_humans == 3)
        draw_minimap(&game.track, 1, W * 0.5f + 60.0f, H * 0.5f + 40.0f,
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

static void draw_garage_scene(int paint_idx)
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
    draw_car_model(0.0f, 0.18f, 0.0f, turn, 0.0f, 0.0f,
                   paint_palette[paint_idx % PAINT_COUNT]);
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
    RK_PLAYERS = 0, RK_TRACK, RK_LAPS, RK_GARAGE, RK_START, RK_EXIT, /* setup */
    RK_CAR, RK_PAINT, RK_GEARBOX, RK_TIRES, RK_DONE           /* garage  */
};

typedef struct {
    int kind;
    int player;
} MenuRow;

static MenuRow menu_rows[6 + MAX_HUMANS];
static int n_menu_rows;
static int menu_row;
static int garage_player;
static char menu_msg[44];
static float menu_msg_t;

enum { SCREEN_SETUP = 0, SCREEN_GARAGE = 1 };

static void menu_notice(const char *text)
{
    snprintf(menu_msg, sizeof(menu_msg), "%s", text);
    menu_msg_t = 4.0f;
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
        for (p = 0; p < sel_players; p++) {
            menu_rows[n_menu_rows].kind = RK_GARAGE;
            menu_rows[n_menu_rows++].player = p;
        }
        menu_rows[n_menu_rows].kind = RK_START;
        menu_rows[n_menu_rows++].player = 0;
        menu_rows[n_menu_rows].kind = RK_EXIT;
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
    case RK_GARAGE:  snprintf(out, cap, "P%d GARAGE", r->player + 1); break;
    case RK_START:   snprintf(out, cap, "GO");                 break;
    case RK_EXIT:    snprintf(out, cap, "EXIT");               break;
    case RK_CAR:     snprintf(out, cap, "CAR");                break;
    case RK_PAINT:   snprintf(out, cap, "PAINT");              break;
    case RK_GEARBOX: snprintf(out, cap, "GEARS");              break;
    case RK_TIRES:   snprintf(out, cap, "TIRES");              break;
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
    case RK_GARAGE:  snprintf(out, cap, "%s",
                              kart_specs[sel_spec[p] % kart_spec_count].name); break;
    case RK_CAR:     snprintf(out, cap, "%s",
                              kart_specs[sel_spec[p] % kart_spec_count].name); break;
    case RK_PAINT:   snprintf(out, cap, "%s",
                              paint_names[sel_paint[p] % PAINT_COUNT]);     break;
    case RK_GEARBOX: snprintf(out, cap, "%s", gearbox_name(sel_gearbox[p])); break;
    case RK_TIRES:   snprintf(out, cap, "%s", tire_name(sel_tire[p]));      break;
    default:         out[0] = 0;                                            break;
    }
}

static int row_has_value(const MenuRow *r)
{
    return (r->kind == RK_PLAYERS || r->kind == RK_TRACK ||
            r->kind == RK_LAPS ||
            r->kind == RK_CAR || r->kind == RK_PAINT ||
            r->kind == RK_GEARBOX || r->kind == RK_TIRES);
}

static void row_change(const MenuRow *r, int d)
{
    int p = r->player;
    switch (r->kind) {
    case RK_PLAYERS:
        sel_players += d;
        if (sel_players < 1) sel_players = MAX_HUMANS;
        if (sel_players > MAX_HUMANS) sel_players = 1;
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

static void activate_row(const MenuRow *r)
{
    switch (r->kind) {
    case RK_GARAGE:
        garage_player = r->player;
        menu_screen = SCREEN_GARAGE;
        menu_row = 0;
        audio_beep(880.0f, 60, 140);
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

static void draw_setup_screen(void)
{
    float W = (float)rmode->fbWidth;
    float H = (float)rmode->efbHeight;
    char buf[48], nav[64], action[64];
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
    snprintf(buf, sizeof(buf), "%d N", (int)menu_track.total_len);
    hud_text(W - 236.0f, 124.0f, 10.0f, 17.0f, buf, 200, 205, 215, 235);
    snprintf(buf, sizeof(buf), "RISE %d", (int)(menu_track.max_y -
                                                menu_track.min_y));
    hud_text(W - 236.0f, 148.0f, 10.0f, 17.0f, buf, 200, 205, 215, 235);
    hud_text(W - 236.0f, 172.0f, 10.0f, 17.0f,
             menu_track.has_walls ? "RAILS" : "NO RAILS",
             menu_track.has_walls ? 150 : 255,
             menu_track.has_walls ? 200 : 150, 170, 235);
    draw_minimap(&menu_track, 0, W - 236.0f, 208.0f, 160.0f);

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
    snprintf(nav, sizeof(nav), "%s %s LINE   %s %s CHANGE",
             up, down, left, right);
    snprintf(action, sizeof(action), "%s SELECT   %s OUT", confirm, back);
    hud_text(W * 0.5f - hud_text_width(9.0f, nav) * 0.5f,
             H - 40.0f, 9.0f, 15.0f, nav,
             160, 165, 180, 220);
    hud_text(W * 0.5f - hud_text_width(9.0f, action) * 0.5f,
             H - 22.0f, 9.0f, 15.0f, action,
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
    snprintf(buf, sizeof(buf), "CURB  %d", (int)sp->mass_kg);
    hud_text(30.0f, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "0-100 %.1fS",
             spec_accel_time_with_settings(sp, &app_settings));
    hud_text(30.0f, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "TOP   %d",
             (int)spec_top_speed_with_settings(sp, &app_settings));
    hud_text(30.0f, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "100-0 %d", (int)sp->brake_dist_100);
    hud_text(30.0f, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "GRIP  %.2fG",
             sp->lat_g *
                 tire_grip_mult_with_settings(&app_settings, sel_tire[p]));
    hud_text(30.0f, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "GEARS %d", sp->n_gears);
    hud_text(30.0f, y, 9.0f, 15.0f, buf, 205, 210, 220, 250); y += 20.0f;
    snprintf(buf, sizeof(buf), "DIRT  %d",
             (int)(sp->offroad_grip * 100.0f));
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
        if (menu_screen == SCREEN_GARAGE) {
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
        draw_garage_scene(sel_paint[garage_player]);
        draw_garage_overlay(garage_player);
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
        if (k->power_fired || k->hit_wall || k->got_item || k->respawned)
            rumble_t[p] = k->respawned ? 0.30f : 0.18f;
        if (p == 0) {
            if (k->got_item)     audio_beep(1320.0f, 90, 150);
            if (k->power_fired)  audio_beep(260.0f, 220, 175);
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
