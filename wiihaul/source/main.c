/*
 * WiiHaul — Wii platform layer.
 *
 * Everything libogc-specific lives here: video / GX setup, flat-shaded
 * low-poly 3D rendering (the rig, the yard's cones/boundaries/target/
 * obstacles), the menu, procedural ASND audio (engine drone, event
 * beeps) and input from Wiimote (D-pad steering, sideways grip — same
 * convention WiiKart uses), Nunchuk, Classic Controller, GameCube pads
 * and USB keyboards.
 *
 * Single player throughout, unlike WiiKart's up-to-four split screen —
 * this is a solo skills test, closer to a CDL yard exam than a race,
 * so there is no split-screen viewport code here at all.
 *
 * Runs on real hardware via the Homebrew Channel and in the Dolphin
 * emulator (File > Open > wiihaul.dol).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <wiikeyboard/keyboard.h>
#include <asndlib.h>
#include <ogc/conf.h>
#include <fat.h>

#include "game.h"
#include "config.h"
#include "camera.h"

#define DEFAULT_FIFO_SIZE (256 * 1024)

/* every on-screen speed/distance reads in mph/feet — the sim itself
 * stays in m/s and meters throughout */
#define MPS_TO_MPH 2.23694f
#define M_TO_FT    3.28084f
#define KG_TO_LB   2.20462f

static void *frameBuffer[2] = { NULL, NULL };
static GXRModeObj *rmode = NULL;
static u32 fb = 0;

static Game game;
static u32 frame_no = 0;
static HaulSettings app_settings;
static ControlConfig control_config;
static CareerState career;

static char config_banner[48];
static char config_detail[48];
static char config_where[72];
static char config_file_status[3][40];
static char config_proof[48];

static const float LX = 0.45f, LY = 0.85f, LZ = 0.28f;
static int video_widescreen;   /* set in video_setup(); read by menu.inc's
                                * view_aspect(), included well before that
                                * function is defined below              */

/* ------------------------------------------------------------------ */
/* App / menu state                                                    */
/* ------------------------------------------------------------------ */

enum { APP_MENU = 0, APP_ATTEMPT = 1 };
static int app_state = APP_MENU;

enum { SCREEN_TITLE = 0, SCREEN_SCENARIO = 1, SCREEN_RIG = 2 };
static int menu_screen = SCREEN_TITLE;
static int sel_scenario = 0;
static int sel_rig = 1;         /* DRY VAN HAUL — a reasonable first pick */
static int pause_open = 0;
static int pause_choice = 0;    /* 0 resume, 1 restart, 2 quit           */
static char menu_msg[64];

/* ------------------------------------------------------------------ */
/* Editable configuration                                              */
/* ------------------------------------------------------------------ */

static int readable_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/*
 * Look for one config file. Every plausible place a person might have
 * put it is tried, in both the layout a release ships (a config folder
 * beside boot.dol) and the flatter one people tend to improvise —
 * mirrors WiiKart's find_config_file exactly, just under wiihaul/.
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
    snprintf(roots[n++], sizeof(roots[0]), "sd:/apps/wiihaul");
    snprintf(roots[n++], sizeof(roots[0]), "usb:/apps/wiihaul");
    snprintf(roots[n++], sizeof(roots[0]), "sd:/wiihaul");
    snprintf(roots[n++], sizeof(roots[0]), "usb:/wiihaul");
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

    haul_settings_defaults(&app_settings);
    control_config_defaults(&control_config);
    truck_specs_reset_defaults();
    career_reset(&career);
    config_banner[0] = config_detail[0] = '\0';
    config_where[0] = config_proof[0] = '\0';
    for (i = 0; i < 3; i++)
        snprintf(config_file_status[i], sizeof(config_file_status[0]),
                 "NOT FOUND");

    /* libfat is optional at runtime: opening the DOL straight in Dolphin
     * with no SD card still works, on compiled-in defaults */
    if (!fatInitDefault())
        snprintf(config_where, sizeof(config_where), "NO SD CARD FOUND");

#define WHERE_SLOT (found ? NULL : config_where)
#define LOAD_ONE(slot, filename, call)                                     \
    do {                                                                   \
        if (find_config_file(filename, path, (int)sizeof(path),           \
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
    LOAD_ONE(1, "rigs.json",
             config_load_rigs_file(path, error, (int)sizeof(error)));
    LOAD_ONE(2, "controls.json",
             config_load_controls_file(&control_config, path, error,
                                       (int)sizeof(error)));
#undef LOAD_ONE
#undef WHERE_SLOT

    snprintf(config_proof, sizeof(config_proof), "%d RIGS  %s",
             rig_spec_count, rig_specs[0].name);

    if (!found) {
        snprintf(config_banner, sizeof(config_banner), "BUILT IN CONFIG");
        if (!config_where[0])
            snprintf(config_where, sizeof(config_where), "NO JSON FOUND");
    } else if (failed) {
        snprintf(config_banner, sizeof(config_banner), "CONFIG ERROR");
    } else {
        snprintf(config_banner, sizeof(config_banner), "CONFIG %d/3", loaded);
    }

    if (sel_rig >= rig_spec_count) sel_rig = 0;
}

/* ------------------------------------------------------------------ */
/* Input state (polled once per frame)                                 */
/* ------------------------------------------------------------------ */

static u32 wheld, wdown;
static u32 gheld, gdown;
static u32 gc_mask;

static u8 key_actions[CONTROL_ACTION_COUNT];
static u8 key_held[GAME_KEY_DOWN + 1];
static int keyboard_ok = 0;
static int keyboard_here = 0;
static u8 key_confirm_edge, key_back_edge, key_menu_edge;
static u8 key_up_edge, key_down_edge, key_left_edge, key_right_edge;

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

static void remember_key_edge(int action, int held)
{
    if (!held) return;
    if (action == CONTROL_STEER_LEFT)       key_left_edge = 1;
    else if (action == CONTROL_STEER_RIGHT) key_right_edge = 1;
    else if (action == CONTROL_ACCEL)       key_up_edge = 1;
    else if (action == CONTROL_REVERSE)     key_down_edge = 1;
    else if (action == CONTROL_CONFIRM)     key_confirm_edge = 1;
    else if (action == CONTROL_BACK)        key_back_edge = 1;
    else if (action == CONTROL_MENU)        key_menu_edge = 1;
}

static void rebuild_key_actions(void)
{
    int action, slot;
    memset(key_actions, 0, sizeof(key_actions));
    for (action = 0; action < CONTROL_ACTION_COUNT; action++) {
        for (slot = 0; slot < CONTROL_MAX_BINDS; slot++) {
            int code = control_config.keyboard[action][slot];
            if (code > GAME_KEY_NONE && code <= GAME_KEY_DOWN &&
               key_held[code]) {
                key_actions[action] = 1;
                break;
            }
        }
    }
}

static void poll_keyboard(void)
{
    keyboard_event ev;

    if (!keyboard_ok) return;
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
        keyboard_here = 1;
        if (ev.type != KEYBOARD_PRESSED && ev.type != KEYBOARD_RELEASED)
            continue;
        held = (ev.type == KEYBOARD_PRESSED);
        {
            int code = normalized_key(ev.symbol);
            int action, slot, was_held;
            if (!code) continue;
            was_held = key_held[code];
            key_held[code] = (u8)held;
            for (action = 0; action < CONTROL_ACTION_COUNT; action++)
                for (slot = 0; slot < CONTROL_MAX_BINDS; slot++)
                    if (control_config.keyboard[action][slot] == code) {
                        remember_key_edge(action, held && !was_held);
                        break;
                    }
            rebuild_key_actions();
        }
    }
}

static void poll_all_inputs(void)
{
    WPAD_ScanPads();
    gc_mask = PAD_ScanPads();
    key_confirm_edge = key_back_edge = key_menu_edge = 0;
    key_up_edge = key_down_edge = key_left_edge = key_right_edge = 0;
    poll_keyboard();

    wheld = WPAD_ButtonsHeld(0);
    wdown = WPAD_ButtonsDown(0);
    gheld = PAD_ButtonsHeld(0);
    gdown = PAD_ButtonsDown(0);

    if ((wdown & (WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME)) ||
       ((gheld & PAD_TRIGGER_Z) && (gdown & PAD_BUTTON_START)))
        exit(0);
}

static float stick_x(const joystick_t *js)
{
    if (js->mag < 0.2f) return 0.0f;
    return haul_clampf(js->mag, 0.0f, 1.0f) *
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

/*
 * The Wiimote mapping (sideways grip, same convention WiiKart uses):
 *
 *   D-pad left/right   steer
 *   2 or A              accel
 *   1                    reverse
 *   B (underside trigger) brake
 *   -                    parking brake
 *   D-pad up             horn
 *   +                    pause menu
 *
 * Nunchuk swaps D-pad steering for the analog stick; Classic Controller
 * gets its own full mapping. GameCube pads (Xbox pads under Dolphin) and
 * the keyboard both come from controls.json — see config/README.md.
 */
static void read_input(Input *in, float dt)
{
    const WPADData *wd;
    float steer = 0.0f;
    int tilt_ok = 1;

    (void)dt;
    memset(in, 0, sizeof(*in));

    in->accel   = (wheld & (WPAD_BUTTON_2 | WPAD_BUTTON_A)) != 0;
    in->reverse = (wheld & WPAD_BUTTON_1) != 0;
    in->brake   = (wheld & WPAD_BUTTON_B) != 0;
    in->parking_brake = (wheld & WPAD_BUTTON_MINUS) != 0;
    in->horn    = (wheld & WPAD_BUTTON_UP) != 0;
    if (wheld & WPAD_BUTTON_LEFT)  steer += STEER_LEFT;
    if (wheld & WPAD_BUTTON_RIGHT) steer += STEER_RIGHT;

    wd = WPAD_Data(0);
    if (wd) {
        if (wd->exp.type == WPAD_EXP_NUNCHUK) {
            steer += stick_x(&wd->exp.nunchuk.js) * STEER_RIGHT;
            in->accel   |= (wheld & WPAD_NUNCHUK_BUTTON_Z) != 0;
            in->reverse |= (wheld & WPAD_NUNCHUK_BUTTON_C) != 0;
            tilt_ok = 0;
        } else if (wd->exp.type == WPAD_EXP_CLASSIC) {
            steer += stick_x(&wd->exp.classic.ljs) * STEER_RIGHT;
            if (wheld & WPAD_CLASSIC_BUTTON_LEFT)  steer += STEER_LEFT;
            if (wheld & WPAD_CLASSIC_BUTTON_RIGHT) steer += STEER_RIGHT;
            in->accel   |= (wheld & WPAD_CLASSIC_BUTTON_A) != 0;
            in->reverse |= (wheld & WPAD_CLASSIC_BUTTON_B) != 0;
            in->brake   |= (wheld & (WPAD_CLASSIC_BUTTON_FULL_R |
                                     WPAD_CLASSIC_BUTTON_FULL_L)) != 0;
            in->parking_brake |= (wheld & WPAD_CLASSIC_BUTTON_MINUS) != 0;
            in->horn    |= (wheld & WPAD_CLASSIC_BUTTON_X) != 0;
            in->gear_up   |= (wheld & WPAD_CLASSIC_BUTTON_ZR) != 0;
            in->gear_down |= (wheld & WPAD_CLASSIC_BUTTON_ZL) != 0;
            tilt_ok = 0;
        }
        if (tilt_ok) {
            float tilt = wd->orient.pitch;
            if (fabsf(tilt) > 7.0f)
                steer += haul_clampf(tilt / 45.0f, -1.0f, 1.0f) * STEER_RIGHT;
        }
    }

    {
        unsigned int gc = gamecube_button_state(gheld);
        s8 sx = PAD_StickX(0);
        if (sx < -18) gc |= GC_INPUT_DPAD_LEFT; /* stick counts as a bind
                                                  * target too, see below */
        if (sx > 18)  gc |= GC_INPUT_DPAD_RIGHT;
        if (control_config.gamecube[CONTROL_STEER_LEFT] &&
           (gc & control_config.gamecube[CONTROL_STEER_LEFT]))
            steer += STEER_LEFT;
        if (control_config.gamecube[CONTROL_STEER_RIGHT] &&
           (gc & control_config.gamecube[CONTROL_STEER_RIGHT]))
            steer += STEER_RIGHT;
        if (sx < -18 || sx > 18)
            steer += haul_clampf((float)sx / 90.0f, -1.0f, 1.0f) * STEER_RIGHT;
        in->accel   |= (gc & control_config.gamecube[CONTROL_ACCEL]) != 0;
        in->reverse |= (gc & control_config.gamecube[CONTROL_REVERSE]) != 0;
        in->brake   |= (gc & control_config.gamecube[CONTROL_BRAKE]) != 0;
        in->parking_brake |=
            (gc & control_config.gamecube[CONTROL_PARKING_BRAKE]) != 0;
        in->horn      |= (gc & control_config.gamecube[CONTROL_HORN]) != 0;
        in->gear_up   |= (gc & control_config.gamecube[CONTROL_GEAR_UP]) != 0;
        in->gear_down |=
            (gc & control_config.gamecube[CONTROL_GEAR_DOWN]) != 0;
    }

    if (key_actions[CONTROL_STEER_LEFT])  steer += STEER_LEFT;
    if (key_actions[CONTROL_STEER_RIGHT]) steer += STEER_RIGHT;
    in->accel         |= key_actions[CONTROL_ACCEL];
    in->reverse       |= key_actions[CONTROL_REVERSE];
    in->brake         |= key_actions[CONTROL_BRAKE];
    in->parking_brake |= key_actions[CONTROL_PARKING_BRAKE];
    in->horn          |= key_actions[CONTROL_HORN];
    in->gear_up       |= key_actions[CONTROL_GEAR_UP];
    in->gear_down     |= key_actions[CONTROL_GEAR_DOWN];

    in->steer = haul_clampf(steer, -1.0f, 1.0f);
}

/* ------------------------------------------------------------------ */
/* Menu navigation                                                     */
/* ------------------------------------------------------------------ */

static int menu_activate(void)
{
    return (wdown & (WPAD_BUTTON_2 | WPAD_BUTTON_A |
                     WPAD_CLASSIC_BUTTON_A)) ||
          (gdown & PAD_BUTTON_A) || key_confirm_edge;
}

static int menu_back(void)
{
    return (wdown & (WPAD_BUTTON_1 | WPAD_BUTTON_B |
                     WPAD_CLASSIC_BUTTON_B)) ||
          (gdown & PAD_BUTTON_B) || key_back_edge;
}

static int menu_dcursor(void)
{
    int d = 0;
    if ((wdown & WPAD_BUTTON_UP) || (gdown & PAD_BUTTON_UP) || key_up_edge)
        d -= 1;
    if ((wdown & WPAD_BUTTON_DOWN) || (gdown & PAD_BUTTON_DOWN) ||
       key_down_edge)
        d += 1;
    return d;
}

static int menu_dvalue(void)
{
    int d = 0;
    if ((wdown & WPAD_BUTTON_LEFT) || (gdown & PAD_BUTTON_LEFT) ||
       key_left_edge)
        d -= 1;
    if ((wdown & WPAD_BUTTON_RIGHT) || (gdown & PAD_BUTTON_RIGHT) ||
       key_right_edge)
        d += 1;
    return d;
}

static int menu_pressed(void)
{
    return (wdown & WPAD_BUTTON_PLUS) || (gdown & PAD_BUTTON_START) ||
          key_menu_edge;
}

/* ------------------------------------------------------------------ */
/* Audio                                                                */
/* ------------------------------------------------------------------ */

#define ENGINE_CYCLE 512
#define NOISE_LEN    4096
#define BEEP_LEN     4096

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
    if (!engine_buf || !noise_buf || !beep_buf) return;

    /* engine: a low, buzzy diesel drone — heavier on the sub-harmonic
     * than WiiKart's cars, since a truck idles a lot lower and rougher */
    for (i = 0; i < ENGINE_CYCLE; i++) {
        float ph = (float)i / ENGINE_CYCLE;
        float v = (ph < 0.5f ? 1.0f : -1.0f) * 0.45f +
                  sinf(ph * 2.0f * (float)M_PI) * 0.20f +
                  sinf(ph * 4.0f * (float)M_PI) * 0.35f;
        engine_buf[i] = (s16)(v * 9000.0f);
    }
    {
        unsigned seed = 55555u;
        for (i = 0; i < NOISE_LEN; i++) {
            seed = seed * 1664525u + 1013904223u;
            noise_buf[i] = (s16)((int)(seed >> 16) - 32768) / 4;
        }
    }
    DCFlushRange(engine_buf, ENGINE_CYCLE * sizeof(s16));
    DCFlushRange(noise_buf, NOISE_LEN * sizeof(s16));

    ASND_SetInfiniteVoice(0, VOICE_MONO_16BIT, 9000, 0,
                          engine_buf, ENGINE_CYCLE * sizeof(s16), 0, 0);
    ASND_SetInfiniteVoice(1, VOICE_MONO_16BIT, 24000, 0,
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
    float v;
    int pitch, vol;

    if (!audio_ok) return;
    if (app_state != APP_ATTEMPT || pause_open) {
        ASND_ChangeVolumeVoice(0, 0, 0);
        ASND_ChangeVolumeVoice(1, 0, 0);
        return;
    }

    v = fabsf(game.rig.speed);
    pitch = (int)(ENGINE_CYCLE * (22.0f + v * 5.0f));
    if (pitch > 90000) pitch = 90000;
    vol = 90 + (int)(v * 3.0f);
    if (vol > 190) vol = 190;
    ASND_ChangePitchVoice(0, pitch);
    ASND_ChangeVolumeVoice(0, vol, vol);

    /* engine strain rises with articulation nearing the jackknife limit
     * — a rough stand-in for tire/frame noise under real stress */
    {
        float worst = 0.0f;
        int i;
        for (i = 0; i < game.rig.n_trailers; i++) {
            const TrailerSpec *tr =
                &trailer_specs[game.spec.trailer_idx[i]];
            float limit = tr->jackknife_limit_deg * ((float)M_PI / 180.0f);
            float frac = fabsf(rig_articulation(&game.rig, i)) /
                        (limit > 0.01f ? limit : 1.0f);
            if (frac > worst) worst = frac;
        }
        vol = (int)(haul_clampf(worst - 0.4f, 0.0f, 1.0f) * 160.0f);
        ASND_ChangeVolumeVoice(1, vol, vol);
    }
}

/* ------------------------------------------------------------------ */
/* Low-level draw helpers (generic GX primitives)                      */
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

/* box rotated by yaw (about Y) then pitched (nose up positive) — no
 * roll, since a flat yard has no banked surfaces to ride */
static void draw_box(float cx, float cy, float cz, float yaw, float pitch,
                     float hfw, float hh, float hlat, u8 r, u8 g, u8 b)
{
    float cf = cosf(yaw), sf = sinf(yaw);
    float cp = cosf(pitch), sp = sinf(pitch);
    float fx = cf * cp, fy = sp, fz = sf * cp;
    float lx = -sf,     ly = 0.0f, lz = cf;
    float ux = -sp * cf, uy = cp, uz = -sp * sf;
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

static void draw_cone(float cx, float cy_base, float cz, float radius,
                      float height, u8 r, u8 g, u8 b)
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

static void ground_quad(float cx, float cz, float half_x, float half_z,
                        float y, u8 r, u8 g, u8 b, u8 a)
{
    quad(cx - half_x, y, cz - half_z,  cx + half_x, y, cz - half_z,
        cx + half_x, y, cz + half_z,  cx - half_x, y, cz + half_z,
        r, g, b, a);
}

/* a painted line on the ground, `width` meters wide */
static void ground_line(float ax, float az, float bx, float bz, float y,
                        float width, u8 r, u8 g, u8 b, u8 a)
{
    float dx = bx - ax, dz = bz - az;
    float len = sqrtf(dx * dx + dz * dz);
    float px, pz;
    if (len < 0.001f) return;
    px = -dz / len * width * 0.5f;
    pz =  dx / len * width * 0.5f;
    quad(ax + px, y, az + pz,  bx + px, y, bz + pz,
        bx - px, y, bz - pz,  ax - px, y, az - pz,
        r, g, b, a);
}

#include "draw_rig.inc"
#include "draw_yard.inc"
#include "hud.inc"
#include "menu.inc"
#include "frames.inc"

/* ------------------------------------------------------------------ */
/* Boot                                                                 */
/* ------------------------------------------------------------------ */

static void video_setup(void)
{
    GXRModeObj *pref = VIDEO_GetPreferredMode(NULL);
    rmode = pref;
    if (pref != NULL) {
        switch (CONF_GetVideo()) {
        case CONF_VIDEO_PAL:
            rmode = (CONF_GetEuRGB60() > 0) ? &TVEurgb60Hz480Prog
                                             : pref;
            break;
        default: rmode = pref; break;
        }
    }
    video_widescreen = (CONF_GetAspectRatio() == CONF_ASPECT_16_9);
    VIDEO_Configure(rmode);
}

int main(int argc, char **argv)
{
    void *gp_fifo;
    GXColor sky = { 150, 165, 175, 255 };
    f32 yscale;
    u32 xfbHeight;
    float dt;

    VIDEO_Init();
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC);
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
        if (app_state == APP_ATTEMPT)
            attempt_frame(dt);

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
