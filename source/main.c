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

#include "game.h"

#define DEFAULT_FIFO_SIZE (256 * 1024)

/* If tilt steering feels inverted on your remote, flip this to -1.
 * (Steering polarity itself lives in game.h: STEER_LEFT/STEER_RIGHT.) */
#define TILT_SIGN (+1.0f)

static void *frameBuffer[2] = { NULL, NULL };
static GXRModeObj *rmode = NULL;
static u32 fb = 0;

static Game game;
static u32 frame_no = 0;

/* app flow */
enum { APP_MENU = 0, APP_RACE = 1 };
static int app_state = APP_MENU;
static int menu_screen = 0;          /* 0 players, 1 track, 2.. karts */
static int sel_players = 1;
static int sel_track = 0;
static int sel_spec[MAX_HUMANS] = { 1, 1, 1, 1 };
static int sel_paint[MAX_HUMANS] = { 0, 1, 2, 3 };
static Track menu_track;
static int menu_track_loaded = -1;

/* per-player camera + rumble */
static float cam_x[MAX_HUMANS], cam_y[MAX_HUMANS], cam_z[MAX_HUMANS];
static float rumble_t[MAX_HUMANS];

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
/* Input state (polled once per frame, consumed per player)            */
/* ------------------------------------------------------------------ */

static u32 wheld[MAX_HUMANS], wdown[MAX_HUMANS];
static u32 gheld[MAX_HUMANS], gdown[MAX_HUMANS];
static int keyboard_ok = 0;

/*
 * Keyboard layout is WASD-first, the way a PC driving game is expected
 * to play: A steers left, D steers right, W is the throttle, S the
 * brake. Arrow keys mirror the same four axes for anyone who prefers
 * them. Steering is fed through the virtual-stick filter, so holding A
 * winds the wheel on smoothly instead of snapping to full lock.
 *
 * All steering signs come from STEER_LEFT / STEER_RIGHT in game.h — see
 * the convention comment there. Writing raw +1/-1 here is what made the
 * controls come out mirrored before.
 */
static u8 key_left, key_right, key_accel, key_brake, key_drift, key_item;
static u8 key_confirm_edge, key_back_edge, key_menu_edge;

static void poll_keyboard(void)
{
    keyboard_event ev;

    if (!keyboard_ok)
        return;
    while (KEYBOARD_GetEvent(&ev)) {
        u8 held;
        if (ev.type == KEYBOARD_DISCONNECTED) {
            key_left = key_right = key_accel = key_brake = 0;
            key_drift = key_item = 0;
            continue;
        }
        if (ev.type != KEYBOARD_PRESSED && ev.type != KEYBOARD_RELEASED)
            continue;
        held = (ev.type == KEYBOARD_PRESSED);
        switch (ev.symbol) {
        case KS_a: case KS_A: case KS_Left:
            key_left = held;
            break;
        case KS_d: case KS_D: case KS_Right:
            key_right = held;
            break;
        case KS_w: case KS_W: case KS_Up:
            key_accel = held;
            break;
        case KS_s: case KS_S: case KS_Down:
            key_brake = held;
            break;
        case KS_space:
            key_drift = held;
            if (held)
                key_confirm_edge = 1;      /* doubles as menu confirm */
            break;
        case KS_Shift_L: case KS_Shift_R:
            key_drift = held;
            break;
        case KS_e: case KS_E:
            key_item = held;
            break;
        case KS_Return:
            if (held) key_confirm_edge = 1;
            break;
        case KS_q: case KS_Q:
            if (held) key_back_edge = 1;
            break;
        case KS_r: case KS_R:
            if (held) key_menu_edge = 1;
            break;
        case KS_Escape:
            if (held) exit(0);
            break;
        default:
            break;
        }
    }
}

static void poll_all_inputs(void)
{
    int p;

    WPAD_ScanPads();
    PAD_ScanPads();
    key_confirm_edge = key_back_edge = key_menu_edge = 0;
    poll_keyboard();

    for (p = 0; p < MAX_HUMANS; p++) {
        wheld[p] = WPAD_ButtonsHeld(p);
        wdown[p] = WPAD_ButtonsDown(p);
        gheld[p] = PAD_ButtonsHeld(p);
        gdown[p] = PAD_ButtonsDown(p);
    }

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

static SteerAxis steer_axis[MAX_HUMANS];

static void read_player_input(int p, Input *in, float dt)
{
    const WPADData *wd;
    float steer = 0.0f;
    int tilt_ok = 1;

    memset(in, 0, sizeof(*in));

    /* Wiimote (sideways grip) */
    in->accel = (wheld[p] & (WPAD_BUTTON_2 | WPAD_BUTTON_A)) != 0;
    in->brake = (wheld[p] & WPAD_BUTTON_1) != 0;
    in->hop   = (wheld[p] & WPAD_BUTTON_B) != 0;
    in->item  = (wheld[p] & (WPAD_BUTTON_MINUS |
                             WPAD_CLASSIC_BUTTON_MINUS)) != 0;
    if (wheld[p] & (WPAD_BUTTON_UP | WPAD_BUTTON_LEFT))
        steer += STEER_LEFT;
    if (wheld[p] & (WPAD_BUTTON_DOWN | WPAD_BUTTON_RIGHT))
        steer += STEER_RIGHT;

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
                                      WPAD_CLASSIC_BUTTON_FULL_L |
                                      WPAD_CLASSIC_BUTTON_ZR |
                                      WPAD_CLASSIC_BUTTON_ZL)) != 0;
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

    /* GameCube controller (= Xbox pads in Dolphin) */
    {
        s8 sx = PAD_StickX(p);
        if (sx > 18 || sx < -18)
            steer += game_clampf((float)sx / 90.0f, -1.0f, 1.0f) *
                     STEER_RIGHT;
        in->accel |= (gheld[p] & (PAD_BUTTON_A | PAD_BUTTON_X)) != 0;
        in->brake |= (gheld[p] & PAD_BUTTON_B) != 0;
        in->hop   |= (gheld[p] & (PAD_TRIGGER_R | PAD_TRIGGER_L)) != 0;
        in->item  |= (gheld[p] & PAD_BUTTON_Y) != 0;
    }

    /* USB keyboard (player 1 only): WASD, A = left, D = right */
    if (p == 0) {
        if (key_left)  steer += STEER_LEFT;
        if (key_right) steer += STEER_RIGHT;
        in->accel |= key_accel;
        in->brake |= key_brake;
        in->hop   |= key_drift;
        in->item  |= key_item;
    }

    /* every device goes through the virtual stick, so a tapped key and
     * a flicked thumbstick both move the wheel at a believable rate */
    in->steer = steer_axis_update(&steer_axis[p],
                                  game_clampf(steer, -1.0f, 1.0f),
                                  game.karts[p].speed, dt);
}

/* menu edges, driven by player 1's devices */
static int menu_confirm(void)
{
    /* Start is deliberately not a confirm: it is the in-race "back to
     * menu" button, and would otherwise bounce straight back out. */
    return (wdown[0] & (WPAD_BUTTON_2 | WPAD_BUTTON_A |
                        WPAD_CLASSIC_BUTTON_A)) ||
           (gdown[0] & PAD_BUTTON_A) ||
           key_confirm_edge;
}

static int menu_back(void)
{
    return (wdown[0] & (WPAD_BUTTON_1 | WPAD_BUTTON_B |
                        WPAD_CLASSIC_BUTTON_B)) ||
           (gdown[0] & PAD_BUTTON_B) ||
           key_back_edge;
}

/* horizontal menu movement: change the selected option */
static int menu_dx(void)
{
    int d = 0;
    if (wdown[0] & (WPAD_BUTTON_LEFT | WPAD_CLASSIC_BUTTON_LEFT))  d -= 1;
    if (wdown[0] & (WPAD_BUTTON_RIGHT | WPAD_CLASSIC_BUTTON_RIGHT)) d += 1;
    if (gdown[0] & PAD_BUTTON_LEFT)  d -= 1;
    if (gdown[0] & PAD_BUTTON_RIGHT) d += 1;
    if (key_left && (frame_no % 10) == 0)  d -= 1;
    if (key_right && (frame_no % 10) == 0) d += 1;
    return d;
}

/* vertical menu movement: in the garage this changes the paint */
static int menu_dy(void)
{
    int d = 0;
    if (wdown[0] & (WPAD_BUTTON_UP | WPAD_CLASSIC_BUTTON_UP))     d -= 1;
    if (wdown[0] & (WPAD_BUTTON_DOWN | WPAD_CLASSIC_BUTTON_DOWN)) d += 1;
    if (gdown[0] & PAD_BUTTON_UP)   d -= 1;
    if (gdown[0] & PAD_BUTTON_DOWN) d += 1;
    if (key_accel && (frame_no % 10) == 0) d -= 1;
    if (key_brake && (frame_no % 10) == 0) d += 1;
    return d;
}

static int race_to_menu_pressed(void)
{
    int p;
    for (p = 0; p < MAX_HUMANS; p++) {
        if (wdown[p] & (WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS))
            return 1;
        if (!(gheld[p] & PAD_TRIGGER_Z) && (gdown[p] & PAD_BUTTON_START))
            return 1;
    }
    return key_menu_edge;
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

    if (app_state != APP_RACE) {
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
            /* guardrails at the barrier line */
            {
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

static void draw_scene_for_player(int p)
{
    Mtx view;
    Mtx44 persp;
    guVector cam, up, look;
    const Track *t = &game.track;
    const Kart *k = &game.karts[p];
    float fwx = cosf(k->heading), fwz = sinf(k->heading);
    float tgt_x = k->x - fwx * 9.0f;
    float tgt_y = k->y + 3.6f;
    float tgt_z = k->z - fwz * 9.0f;
    float blend = 1.0f - powf(0.006f, 1.0f / 60.0f);
    float vx, vy, vw, vh;
    int i;

    cam_x[p] += (tgt_x - cam_x[p]) * blend;
    cam_y[p] += (tgt_y - cam_y[p]) * blend;
    cam_z[p] += (tgt_z - cam_z[p]) * blend;

    /* keep the camera above the road surface behind the kart */
    {
        int seg; float frac, lat, sy;
        track_locate(t, cam_x[p], cam_z[p], k->seg, &seg, &frac, &lat, &sy);
        if (cam_y[p] < sy + 1.6f)
            cam_y[p] = sy + 1.6f;
    }

    cam.x = cam_x[p];  cam.y = cam_y[p];  cam.z = cam_z[p];
    up.x  = 0.0f;      up.y  = 1.0f;      up.z  = 0.0f;
    look.x = k->x + fwx * 4.0f;
    look.y = k->y + 1.2f;
    look.z = k->z + fwz * 4.0f;

    viewport_rect(p, game.cfg.n_humans, &vx, &vy, &vw, &vh);
    GX_SetViewport(vx, vy, vw, vh, 0.0f, 1.0f);
    GX_SetScissor((u32)vx, (u32)vy, (u32)vw, (u32)vh);

    guPerspective(persp, 58.0f, vw / vh, 0.5f, 900.0f);
    GX_LoadProjectionMtx(persp, GX_PERSPECTIVE);
    guLookAt(view, &cam, &up, &look);
    GX_LoadPosMtxImm(view, GX_PNMTX0);

    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);

    draw_track(t, k->seg);
    for (i = 0; i < NUM_KARTS; i++) {
        const Kart *o = &game.karts[i];
        float ddx = o->x - cam_x[p];
        float ddz = o->z - cam_z[p];
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
    case 'L': return 56;   case 'N': return 84;   case 'O': return 63;
    case 'P': return 115;  case 'R': return 80;   case 'S': return 109;
    case 'T': return 120;  case 'U': return 62;   case 'Y': return 110;
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

static void hud_glyph(float x, float y, float w, float h, char c,
                      u8 r, u8 g, u8 b, u8 a)
{
    float t = w * 0.22f;
    int m;

    if (c == '.') {
        hud_rect(x, y + h - t, t, t, r, g, b, a);
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
    return ai_strategy_name(k->strategy);
}

static const Kart *kart_at_rank(int rank)
{
    int i;
    for (i = 0; i < NUM_KARTS; i++)
        if (game.karts[i].rank == rank)
            return &game.karts[i];
    return NULL;
}

static void draw_player_hud(int p)
{
    const Kart *k = &game.karts[p];
    char buf[24];
    float vx, vy, vw, vh;

    viewport_rect(p, game.cfg.n_humans, &vx, &vy, &vw, &vh);

    /* lap */
    {
        int lap_disp = k->lap + 1;
        if (lap_disp < 1) lap_disp = 1;
        if (lap_disp > RACE_LAPS) lap_disp = RACE_LAPS;
        snprintf(buf, sizeof(buf), "L%d/%d", lap_disp, RACE_LAPS);
        hud_text(vx + 14.0f, vy + 12.0f, 11.0f, 19.0f, buf,
                 255, 255, 255, 220);
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
        float frac = (k->push_t > 0.0f) ? k->push_t / PUSH_SECONDS
                                       : k->grip_t / TIRE_SECONDS;
        u8 cr = (k->push_t > 0.0f) ? 240 : 110;
        u8 cg = (k->push_t > 0.0f) ? 175 : 225;
        u8 cb = (k->push_t > 0.0f) ?  55 : 140;
        hud_rect(vx + 14.0f, vy + vh - 12.0f, 90.0f, 6.0f, 15, 15, 20, 160);
        hud_rect(vx + 15.0f, vy + vh - 11.0f,
                 88.0f * game_clampf(frac, 0.0f, 1.0f), 4.0f, cr, cg, cb, 230);
    }
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
        int rank, rows = NUM_KARTS < 8 ? NUM_KARTS : 8;
        float y0;

        hud_rect(0.0f, 0.0f, W, H, 0, 0, 20, 150);
        hud_text(W * 0.5f - hud_text_width(30.0f, "FINISH") * 0.5f,
                 26.0f, 30.0f, 50.0f, "FINISH", 255, 255, 255, 240);

        /* full classification, so you can see which strategy won */
        y0 = 96.0f;
        for (rank = 1; rank <= rows; rank++) {
            const Kart *k = kart_at_rank(rank);
            const u8 *c;
            float y = y0 + (float)(rank - 1) * 25.0f;
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
    guPerspective(persp, 40.0f, W / H, 0.3f, 200.0f);
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

static void draw_stat_bar(float x, float y, float w, float h, float frac,
                          u8 r, u8 g, u8 b)
{
    hud_rect(x, y, w, h, 16, 18, 26, 210);
    hud_rect(x + 1.0f, y + 1.0f,
             (w - 2.0f) * game_clampf(frac, 0.02f, 1.0f), h - 2.0f,
             r, g, b, 240);
}

static void draw_garage_overlay(int p)
{
    const KartSpec *s = &kart_specs[sel_spec[p] % SPEC_COUNT];
    const u8 *paint = paint_palette[sel_paint[p] % PAINT_COUNT];
    float W = (float)rmode->fbWidth;
    float H = (float)rmode->efbHeight;
    float x = 34.0f, y;
    char buf[32];
    int i;

    hud_ortho_fullscreen();

    /* readability panel behind the left-hand column */
    hud_rect(18.0f, 24.0f, 250.0f, H - 90.0f, 12, 15, 24, 165);

    snprintf(buf, sizeof(buf), "P%d GARAGE", p + 1);
    hud_text(x, 38.0f, 12.0f, 21.0f, buf, paint[0], paint[1], paint[2], 255);

    snprintf(buf, sizeof(buf), "%s", s->name);
    hud_text(x, 70.0f, 18.0f, 30.0f, buf, 255, 220, 60, 255);

    /* spec sheet */
    y = 118.0f;
    snprintf(buf, sizeof(buf), "HP    %d", (int)s->power_hp);
    hud_text(x, y, 9.0f, 15.0f, buf, 205, 210, 220, 255); y += 22.0f;
    snprintf(buf, sizeof(buf), "CURB  %d", (int)s->mass_kg);
    hud_text(x, y, 9.0f, 15.0f, buf, 205, 210, 220, 255); y += 22.0f;
    snprintf(buf, sizeof(buf), "0-100 %.1fS", spec_accel_time(s));
    hud_text(x, y, 9.0f, 15.0f, buf, 205, 210, 220, 255); y += 22.0f;
    snprintf(buf, sizeof(buf), "TOP   %d", (int)spec_top_speed(s));
    hud_text(x, y, 9.0f, 15.0f, buf, 205, 210, 220, 255); y += 22.0f;
    snprintf(buf, sizeof(buf), "100-0 %d", (int)s->brake_dist_100);
    hud_text(x, y, 9.0f, 15.0f, buf, 205, 210, 220, 255); y += 22.0f;
    snprintf(buf, sizeof(buf), "GRIP  %.2fG", s->lat_g);
    hud_text(x, y, 9.0f, 15.0f, buf, 205, 210, 220, 255); y += 30.0f;

    /* at-a-glance bars */
    hud_text(x, y, 8.0f, 13.0f, "POWER", 150, 158, 175, 255);
    draw_stat_bar(x + 74.0f, y, 150.0f, 12.0f,
                  s->power_hp / 320.0f, 235, 120, 60);
    y += 24.0f;
    hud_text(x, y, 8.0f, 13.0f, "BRAKE", 150, 158, 175, 255);
    draw_stat_bar(x + 74.0f, y, 150.0f, 12.0f,
                  (48.0f - s->brake_dist_100) / 22.0f, 90, 190, 235);
    y += 24.0f;
    hud_text(x, y, 8.0f, 13.0f, "GRIP", 150, 158, 175, 255);
    draw_stat_bar(x + 74.0f, y, 150.0f, 12.0f,
                  s->lat_g / 1.40f, 235, 200, 70);
    y += 24.0f;
    hud_text(x, y, 8.0f, 13.0f, "DIRT", 150, 158, 175, 255);
    draw_stat_bar(x + 74.0f, y, 150.0f, 12.0f,
                  s->offroad_grip, 130, 200, 110);
    y += 34.0f;

    /* paint shop */
    snprintf(buf, sizeof(buf), "PAINT %s", paint_names[sel_paint[p] %
                                                       PAINT_COUNT]);
    hud_text(x, y, 9.0f, 15.0f, buf, 205, 210, 220, 255);
    y += 24.0f;
    for (i = 0; i < PAINT_COUNT; i++) {
        float sx = x + (float)i * 27.0f;
        const u8 *c = paint_palette[i];
        if (i == sel_paint[p] % PAINT_COUNT)
            hud_rect(sx - 3.0f, y - 3.0f, 28.0f, 28.0f, 255, 255, 255, 235);
        hud_rect(sx, y, 22.0f, 22.0f, c[0], c[1], c[2], 255);
    }

    /* controls */
    hud_text(W * 0.5f - 60.0f, H - 52.0f, 9.0f, 15.0f, "A D   CAR",
             170, 175, 190, 225);
    hud_text(W * 0.5f - 60.0f, H - 32.0f, 9.0f, 15.0f, "W S   PAINT",
             170, 175, 190, 225);
    hud_text(W - 210.0f, H - 42.0f, 9.0f, 15.0f, "ENTER   GO",
             120, 235, 130, 235);
}

static void menu_update_track_preview(void)
{
    if (menu_track_loaded != sel_track) {
        track_init(&menu_track, sel_track);
        menu_track_loaded = sel_track;
    }
}

static void draw_menu(void)
{
    float W = (float)rmode->fbWidth;
    float H = (float)rmode->efbHeight;
    char buf[32];

    hud_ortho_fullscreen();

    hud_rect(0.0f, 0.0f, W, H, 18, 24, 40, 255);
    hud_text(W * 0.5f - hud_text_width(30.0f, "WIIKART") * 0.5f, 34.0f,
             30.0f, 50.0f, "WIIKART", 230, 40, 40, 255);

    if (menu_screen == 0) {
        hud_text(W * 0.5f - hud_text_width(16.0f, "PLAYERS") * 0.5f,
                 150.0f, 16.0f, 27.0f, "PLAYERS", 235, 235, 235, 255);
        snprintf(buf, sizeof(buf), "-  %d  -", sel_players);
        hud_text(W * 0.5f - hud_text_width(22.0f, buf) * 0.5f, 210.0f,
                 22.0f, 37.0f, buf, 255, 220, 60, 255);
    } else if (menu_screen == 1) {
        menu_update_track_preview();
        hud_text(60.0f, 140.0f, 13.0f, 22.0f, "TRACK", 235, 235, 235, 255);
        snprintf(buf, sizeof(buf), "- %s -", track_name(sel_track));
        hud_text(60.0f, 180.0f, 15.0f, 25.0f, buf, 255, 220, 60, 255);
        snprintf(buf, sizeof(buf), "LENGTH %d", (int)menu_track.total_len);
        hud_text(60.0f, 240.0f, 10.0f, 17.0f, buf, 200, 205, 215, 255);
        snprintf(buf, sizeof(buf), "RISE   %d",
                 (int)(menu_track.max_y - menu_track.min_y));
        hud_text(60.0f, 268.0f, 10.0f, 17.0f, buf, 200, 205, 215, 255);
        if (sel_track != TRACK_CLASSIC)
            hud_text(60.0f, 296.0f, 10.0f, 17.0f, "COLORADO PASS",
                     150, 190, 230, 255);
        draw_minimap(&menu_track, 0, W - 220.0f, 160.0f, 170.0f);
    }

    hud_text(W * 0.5f - hud_text_width(9.0f, "A D  CHOOSE") * 0.5f,
             H - 62.0f, 9.0f, 15.0f, "A D  CHOOSE", 160, 165, 180, 220);
    hud_text(W * 0.5f - hud_text_width(9.0f, "ENTER  OR  2  GO") * 0.5f,
             H - 40.0f, 9.0f, 15.0f, "ENTER  OR  2  GO", 160, 165, 180, 220);
}

static void start_race(void)
{
    GameConfig cfg;
    int p;

    memset(&cfg, 0, sizeof(cfg));
    cfg.track_id = sel_track;
    cfg.n_humans = sel_players;
    for (p = 0; p < MAX_HUMANS; p++) {
        cfg.spec[p] = sel_spec[p] % SPEC_COUNT;
        cfg.paint[p] = sel_paint[p] % PAINT_COUNT;
    }

    game_init(&game, &cfg);
    place_scenery(&game.track);
    for (p = 0; p < MAX_HUMANS; p++) {
        Kart *k = &game.karts[p < cfg.n_humans ? p : 0];
        cam_x[p] = k->x - cosf(k->heading) * 9.0f;
        cam_y[p] = k->y + 3.6f;
        cam_z[p] = k->z - sinf(k->heading) * 9.0f;
        rumble_t[p] = 0.0f;
        steer_axis_reset(&steer_axis[p]);
    }
    app_state = APP_RACE;
    audio_beep(660.0f, 90, 160);
}

static void menu_frame(void)
{
    int dx = menu_dx();
    int dy = menu_dy();
    int moved = 0;

    if (menu_screen == 0) {
        if (dx || dy) {
            sel_players += (dx ? dx : dy);
            if (sel_players < 1) sel_players = MAX_HUMANS;
            if (sel_players > MAX_HUMANS) sel_players = 1;
            moved = 1;
        }
        if (menu_confirm()) { menu_screen = 1; audio_beep(880.0f, 60, 140); }
    } else if (menu_screen == 1) {
        if (dx || dy) {
            int d = dx ? dx : dy;
            sel_track = ((sel_track + d) % TRACK_COUNT + TRACK_COUNT)
                        % TRACK_COUNT;
            moved = 1;
        }
        if (menu_confirm()) { menu_screen = 2; audio_beep(880.0f, 60, 140); }
        else if (menu_back()) { menu_screen = 0; }
    } else {
        /* garage: left/right swaps the car, up/down swaps the paint */
        int p = menu_screen - 2;
        if (dx) {
            sel_spec[p] = ((sel_spec[p] + dx) % SPEC_COUNT + SPEC_COUNT)
                          % SPEC_COUNT;
            moved = 1;
        }
        if (dy) {
            sel_paint[p] = ((sel_paint[p] + dy) % PAINT_COUNT + PAINT_COUNT)
                           % PAINT_COUNT;
            moved = 1;
        }
        if (menu_confirm()) {
            audio_beep(880.0f, 60, 140);
            if (p + 1 < sel_players)
                menu_screen++;
            else
                start_race();
        } else if (menu_back()) {
            menu_screen--;
        }
    }

    if (moved)
        audio_beep(440.0f, 35, 90);

    if (app_state == APP_RACE)
        return;                     /* start_race() already switched away */

    if (menu_screen >= 2) {
        int p = menu_screen - 2;
        draw_garage_scene(sel_paint[p]);
        draw_garage_overlay(p);
    } else {
        draw_menu();
    }
}

/* ------------------------------------------------------------------ */
/* Race frame                                                          */
/* ------------------------------------------------------------------ */

static int prev_countdown_n = -1;

static void race_frame(float dt)
{
    Input in[MAX_HUMANS];
    int p;

    for (p = 0; p < MAX_HUMANS; p++)
        read_player_input(p, &in[p], dt);

    if (race_to_menu_pressed()) {
        app_state = APP_MENU;
        menu_screen = 0;
        return;
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
        if (k->power_fired || k->hit_wall || k->got_item)
            rumble_t[p] = 0.18f;
        if (p == 0) {
            if (k->got_item)     audio_beep(1320.0f, 90, 150);
            if (k->power_fired)  audio_beep(260.0f, 220, 175);
            if (k->hit_wall)     audio_beep(110.0f, 120, 190);
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

    for (p = 0; p < game.cfg.n_humans; p++)
        draw_scene_for_player(p);

    /* blank the unused quadrant in 3P before HUD overlays it */
    draw_race_hud();
}

/* ------------------------------------------------------------------ */
/* Setup and main loop                                                 */
/* ------------------------------------------------------------------ */

int main(void)
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

    rmode = VIDEO_GetPreferredMode(NULL);
    frameBuffer[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
    frameBuffer[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));

    VIDEO_Configure(rmode);
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
            menu_frame();
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
