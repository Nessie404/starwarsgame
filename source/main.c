/*
 * WiiKart — Wii platform layer.
 *
 * Everything libogc-specific lives here: video / GX setup, flat-shaded
 * 3D rendering of the track and karts, the 2D HUD (7-segment glyphs,
 * minimap) and Wiimote input (held sideways, Mario-Kart-Wii-style).
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
#include <wiikeyboard/keyboard.h>
#include <wiikeyboard/keysym.h>

#include "game.h"

#define DEFAULT_FIFO_SIZE (256 * 1024)

/* If tilt steering feels inverted on your remote, flip this to -1. */
#define TILT_SIGN (+1.0f)

static void *frameBuffer[2] = { NULL, NULL };
static GXRModeObj *rmode = NULL;
static u32 fb = 0;

static Game game;
static u32 frame_no = 0;
static float rumble_t = 0.0f;

/* camera state (smoothed) */
static float cam_x, cam_y, cam_z;

static const u8 kart_colors[NUM_KARTS][3] = {
    { 230,  40,  40 },   /* player: red   */
    {  50,  90, 230 },   /* blue          */
    {  40, 170,  70 },   /* green         */
    { 240, 200,  40 },   /* yellow        */
    { 160,  70, 220 },   /* purple        */
};

/* directional light for fake flat shading */
static const float LX = 0.45f, LY = 0.85f, LZ = 0.28f;

/* ------------------------------------------------------------------ */
/* Scenery placed at init (kept off the road)                          */
/* ------------------------------------------------------------------ */

#define MAX_TREES 24
static float tree_x[MAX_TREES], tree_z[MAX_TREES];
static int n_trees = 0;

static void place_scenery(void)
{
    /* deterministic pseudo-random candidates over the track's bounds,
     * keeping only spots well away from the road */
    const Track *t = &game.track;
    int i;
    unsigned seed = 12345u;

    n_trees = 0;
    for (i = 0; i < 200 && n_trees < MAX_TREES; i++) {
        float fx, fz, lat, frac;
        int seg;
        seed = seed * 1664525u + 1013904223u;
        fx = t->min_x - 30.0f +
             (t->max_x - t->min_x + 60.0f) * ((seed >> 8) & 0xffff) / 65535.0f;
        seed = seed * 1664525u + 1013904223u;
        fz = t->min_z - 30.0f +
             (t->max_z - t->min_z + 60.0f) * ((seed >> 8) & 0xffff) / 65535.0f;
        track_locate(t, fx, fz, -1, &seg, &frac, &lat);
        if (fabsf(lat) > t->wall_half + 5.0f) {
            tree_x[n_trees] = fx;
            tree_z[n_trees] = fz;
            n_trees++;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Low-level draw helpers (world space, view matrix already loaded)    */
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

/* Axis-aligned-in-local-space box, rotated by yaw around Y, with fake
 * directional shading per face. hfw/hh/hlat are half-extents along the
 * forward / up / lateral axes. */
static void draw_box(float cx, float cy, float cz, float yaw,
                     float hfw, float hh, float hlat,
                     u8 r, u8 g, u8 b)
{
    float fx = cosf(yaw), fz = sinf(yaw);   /* forward  */
    float lx = -fz,       lz = fx;          /* lateral (left) */
    float corner[8][3];
    int i;

    for (i = 0; i < 8; i++) {
        float sf = (i & 1) ? 1.0f : -1.0f;   /* forward sign  */
        float sl = (i & 2) ? 1.0f : -1.0f;   /* lateral sign  */
        float su = (i & 4) ? 1.0f : -1.0f;   /* up sign       */
        corner[i][0] = cx + fx * hfw * sf + lx * hlat * sl;
        corner[i][1] = cy + hh * su;
        corner[i][2] = cz + fz * hfw * sf + lz * hlat * sl;
    }

    {
        /* face shading from the light direction */
        float s_top   = 0.55f + 0.45f * LY;
        float s_front = 0.55f + 0.45f * fmaxf(0.0f,  fx * LX + fz * LZ);
        float s_back  = 0.55f + 0.45f * fmaxf(0.0f, -fx * LX - fz * LZ);
        float s_left  = 0.55f + 0.45f * fmaxf(0.0f,  lx * LX + lz * LZ);
        float s_right = 0.55f + 0.45f * fmaxf(0.0f, -lx * LX - lz * LZ);

        /* top: corners with su=+1 -> indices 4,5,7,6 */
        quad(corner[4][0], corner[4][1], corner[4][2],
             corner[5][0], corner[5][1], corner[5][2],
             corner[7][0], corner[7][1], corner[7][2],
             corner[6][0], corner[6][1], corner[6][2],
             shade(r, s_top), shade(g, s_top), shade(b, s_top), 255);
        /* front (+fwd): indices 1,3,7,5 */
        quad(corner[1][0], corner[1][1], corner[1][2],
             corner[3][0], corner[3][1], corner[3][2],
             corner[7][0], corner[7][1], corner[7][2],
             corner[5][0], corner[5][1], corner[5][2],
             shade(r, s_front), shade(g, s_front), shade(b, s_front), 255);
        /* back (-fwd): 0,4,6,2 */
        quad(corner[0][0], corner[0][1], corner[0][2],
             corner[4][0], corner[4][1], corner[4][2],
             corner[6][0], corner[6][1], corner[6][2],
             corner[2][0], corner[2][1], corner[2][2],
             shade(r, s_back), shade(g, s_back), shade(b, s_back), 255);
        /* left (+lat): 2,6,7,3 */
        quad(corner[2][0], corner[2][1], corner[2][2],
             corner[6][0], corner[6][1], corner[6][2],
             corner[7][0], corner[7][1], corner[7][2],
             corner[3][0], corner[3][1], corner[3][2],
             shade(r, s_left), shade(g, s_left), shade(b, s_left), 255);
        /* right (-lat): 0,1,5,4 */
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

static void draw_track(void)
{
    const Track *t = &game.track;
    const float RW = t->road_half;
    const float ROAD_Y = 0.05f;
    int i;

    /* grass */
    quad(t->min_x - 80.0f, 0.0f, t->min_z - 80.0f,
         t->max_x + 80.0f, 0.0f, t->min_z - 80.0f,
         t->max_x + 80.0f, 0.0f, t->max_z + 80.0f,
         t->min_x - 80.0f, 0.0f, t->max_z + 80.0f,
         58, 142, 60, 255);

    for (i = 0; i < t->n; i++) {
        int in = (i + 1) % t->n;
        float l0x = -t->dz[i],  l0z = t->dx[i];
        float l1x = -t->dz[in], l1z = t->dx[in];
        u8 r, g, b;

        if (track_is_pad_seg(t, i)) {
            /* boost pad: pulsing orange */
            float pulse = 0.85f + 0.15f * sinf((float)frame_no * 0.2f);
            r = shade(245, pulse); g = shade(150, pulse); b = 30;
        } else if (i & 1) {
            r = 95; g = 95; b = 100;
        } else {
            r = 85; g = 85; b = 90;
        }

        /* road surface */
        quad(t->px[i]  + l0x * RW, ROAD_Y, t->pz[i]  + l0z * RW,
             t->px[in] + l1x * RW, ROAD_Y, t->pz[in] + l1z * RW,
             t->px[in] - l1x * RW, ROAD_Y, t->pz[in] - l1z * RW,
             t->px[i]  - l0x * RW, ROAD_Y, t->pz[i]  - l0z * RW,
             r, g, b, 255);

        /* curbs */
        if (i & 1) { r = 210; g = 40; b = 40; }
        else       { r = 235; g = 235; b = 235; }
        quad(t->px[i]  + l0x * (RW + 0.9f), ROAD_Y, t->pz[i]  + l0z * (RW + 0.9f),
             t->px[in] + l1x * (RW + 0.9f), ROAD_Y, t->pz[in] + l1z * (RW + 0.9f),
             t->px[in] + l1x * RW,          ROAD_Y, t->pz[in] + l1z * RW,
             t->px[i]  + l0x * RW,          ROAD_Y, t->pz[i]  + l0z * RW,
             r, g, b, 255);
        quad(t->px[i]  - l0x * RW,          ROAD_Y, t->pz[i]  - l0z * RW,
             t->px[in] - l1x * RW,          ROAD_Y, t->pz[in] - l1z * RW,
             t->px[in] - l1x * (RW + 0.9f), ROAD_Y, t->pz[in] - l1z * (RW + 0.9f),
             t->px[i]  - l0x * (RW + 0.9f), ROAD_Y, t->pz[i]  - l0z * (RW + 0.9f),
             r, g, b, 255);
    }

    /* start/finish: checkered strip over segment 0 */
    {
        int in = 1;
        float l0x = -t->dz[0], l0z = t->dx[0];
        float l1x = -t->dz[in], l1z = t->dx[in];
        int cx, cz;
        for (cz = 0; cz < 2; cz++) {           /* along the road    */
            for (cx = 0; cx < 6; cx++) {       /* across the road   */
                float w0 = -RW + 2.0f * RW * (float)cx / 6.0f;
                float w1 = -RW + 2.0f * RW * (float)(cx + 1) / 6.0f;
                float f0 = (float)cz / 2.0f, f1 = (float)(cz + 1) / 2.0f;
                float ax = t->px[0] + (t->px[in] - t->px[0]) * f0;
                float az = t->pz[0] + (t->pz[in] - t->pz[0]) * f0;
                float bx = t->px[0] + (t->px[in] - t->px[0]) * f1;
                float bz = t->pz[0] + (t->pz[in] - t->pz[0]) * f1;
                float lax = l0x + (l1x - l0x) * f0, laz = l0z + (l1z - l0z) * f0;
                float lbx = l0x + (l1x - l0x) * f1, lbz = l0z + (l1z - l0z) * f1;
                u8 c = ((cx + cz) & 1) ? 235 : 25;
                quad(ax + lax * w0, 0.08f, az + laz * w0,
                     bx + lbx * w0, 0.08f, bz + lbz * w0,
                     bx + lbx * w1, 0.08f, bz + lbz * w1,
                     ax + lax * w1, 0.08f, az + laz * w1,
                     c, c, c, 255);
            }
        }
    }

    /* start arch */
    {
        float yaw = atan2f(t->dz[0], t->dx[0]);
        float lx = -t->dz[0], lz = t->dx[0];
        draw_box(t->px[0] + lx * 6.8f, 2.75f, t->pz[0] + lz * 6.8f, yaw,
                 0.4f, 2.75f, 0.4f, 225, 225, 230);
        draw_box(t->px[0] - lx * 6.8f, 2.75f, t->pz[0] - lz * 6.8f, yaw,
                 0.4f, 2.75f, 0.4f, 225, 225, 230);
        draw_box(t->px[0], 5.9f, t->pz[0], yaw,
                 0.3f, 0.55f, 7.2f, 200, 30, 30);
    }

    /* trees and far mountains */
    for (i = 0; i < n_trees; i++) {
        draw_box(tree_x[i], 0.6f, tree_z[i], 0.0f, 0.25f, 0.6f, 0.25f,
                 110, 75, 40);
        draw_cone(tree_x[i], 1.2f, tree_z[i], 1.7f, 3.2f, 30, 130, 45);
    }
    draw_cone(t->min_x - 60.0f, 0.0f, t->min_z - 55.0f, 34.0f, 26.0f, 130, 130, 145);
    draw_cone(t->max_x + 60.0f, 0.0f, t->min_z - 45.0f, 40.0f, 32.0f, 120, 120, 135);
    draw_cone(t->max_x + 55.0f, 0.0f, t->max_z + 55.0f, 30.0f, 22.0f, 135, 135, 150);
    draw_cone(t->min_x - 55.0f, 0.0f, t->max_z + 50.0f, 38.0f, 30.0f, 125, 125, 140);
}

static void draw_kart(const Kart *k, const u8 col[3])
{
    float yaw = k->heading + (k->drifting ? (float)k->drifting * 0.35f : 0.0f)
                + k->steer_vis * 0.06f;
    float fx = cosf(yaw), fz = sinf(yaw);
    float lx = -fz, lz = fx;
    int w;

    /* shadow */
    quad(k->x + fx * 1.3f + lx * 0.85f, 0.10f, k->z + fz * 1.3f + lz * 0.85f,
         k->x + fx * 1.3f - lx * 0.85f, 0.10f, k->z + fz * 1.3f - lz * 0.85f,
         k->x - fx * 1.3f - lx * 0.85f, 0.10f, k->z - fz * 1.3f - lz * 0.85f,
         k->x - fx * 1.3f + lx * 0.85f, 0.10f, k->z - fz * 1.3f + lz * 0.85f,
         0, 0, 0, 90);

    /* body + cab */
    draw_box(k->x, 0.42f, k->z, yaw, 1.10f, 0.28f, 0.65f,
             col[0], col[1], col[2]);
    draw_box(k->x - fx * 0.25f, 0.92f, k->z - fz * 0.25f, yaw,
             0.30f, 0.26f, 0.30f, 40, 40, 45);

    /* wheels: front pair steers visually */
    for (w = 0; w < 4; w++) {
        float sf = (w < 2) ? 1.0f : -1.0f;
        float sl = (w & 1) ? 1.0f : -1.0f;
        float wyaw = yaw + ((w < 2) ? k->steer_vis * 0.45f : 0.0f);
        draw_box(k->x + fx * 0.85f * sf + lx * 0.72f * sl, 0.30f,
                 k->z + fz * 0.85f * sf + lz * 0.72f * sl,
                 wyaw, 0.30f, 0.30f, 0.14f, 25, 25, 28);
    }

    /* boost flame */
    if (k->boost_t > 0.0f) {
        float flick = 0.7f + 0.3f * ((frame_no & 2) ? 1.0f : 0.4f);
        float len = 1.1f * flick;
        u8 fr = 255, fg = (frame_no & 2) ? 170 : 110;
        GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
        GX_Position3f32(k->x - fx * 1.15f + lx * 0.35f, 0.45f,
                        k->z - fz * 1.15f + lz * 0.35f);
        GX_Color4u8(fr, fg, 20, 230);
        GX_Position3f32(k->x - fx * 1.15f - lx * 0.35f, 0.45f,
                        k->z - fz * 1.15f - lz * 0.35f);
        GX_Color4u8(fr, fg, 20, 230);
        GX_Position3f32(k->x - fx * (1.15f + len), 0.42f,
                        k->z - fz * (1.15f + len));
        GX_Color4u8(255, 240, 90, 200);
        GX_End();
    }

    /* drift sparks at the rear wheels */
    if (k->drifting && k->drift_charge > 0.35f) {
        u8 sr, sg, sb;
        float jx = 0.15f * sinf((float)frame_no * 1.7f);
        if (k->drift_charge > 2.2f)      { sr = 255; sg = 120; sb = 30; }
        else if (k->drift_charge > 1.0f) { sr = 255; sg = 200; sb = 60; }
        else                             { sr = 90;  sg = 160; sb = 255; }
        for (w = 0; w < 2; w++) {
            float sl = w ? 1.0f : -1.0f;
            float sx = k->x - fx * 1.0f + lx * (0.75f * sl) + jx;
            float sz = k->z - fz * 1.0f + lz * (0.75f * sl) - jx;
            quad(sx - 0.14f, 0.16f, sz - 0.14f,
                 sx + 0.14f, 0.16f, sz - 0.14f,
                 sx + 0.14f, 0.16f, sz + 0.14f,
                 sx - 0.14f, 0.16f, sz + 0.14f,
                 sr, sg, sb, 230);
        }
    }
}

static void draw_scene(void)
{
    Mtx view;
    Mtx44 persp;
    guVector cam, up, look;
    const Kart *p = &game.karts[0];
    float fwx = cosf(p->heading), fwz = sinf(p->heading);
    float tgt_x = p->x - fwx * 8.5f;
    float tgt_y = 3.4f;
    float tgt_z = p->z - fwz * 8.5f;
    float blend = 1.0f - powf(0.006f, 1.0f / 60.0f);
    int i;

    /* smoothed chase camera */
    cam_x += (tgt_x - cam_x) * blend;
    cam_y += (tgt_y - cam_y) * blend;
    cam_z += (tgt_z - cam_z) * blend;

    cam.x = cam_x;  cam.y = cam_y;  cam.z = cam_z;
    up.x  = 0.0f;   up.y  = 1.0f;   up.z  = 0.0f;
    look.x = p->x + fwx * 4.0f;
    look.y = 1.1f;
    look.z = p->z + fwz * 4.0f;

    guPerspective(persp, 58.0f,
                  (f32)rmode->fbWidth / (f32)rmode->efbHeight, 0.5f, 700.0f);
    GX_LoadProjectionMtx(persp, GX_PERSPECTIVE);
    guLookAt(view, &cam, &up, &look);
    GX_LoadPosMtxImm(view, GX_PNMTX0);

    GX_SetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);

    draw_track();
    for (i = 0; i < NUM_KARTS; i++)
        draw_kart(&game.karts[i], kart_colors[i]);
}

/* ------------------------------------------------------------------ */
/* HUD: 7-segment glyphs drawn with quads                              */
/* ------------------------------------------------------------------ */

/* segment bits: a=1 b=2 c=4 d=8 e=16 f=32 g=64 (a top, clockwise,
 * g middle) */
static int glyph_mask(char c)
{
    switch (c) {
    case '0': return 63;   case '1': return 6;    case '2': return 91;
    case '3': return 79;   case '4': return 102;  case '5': return 109;
    case '6': return 125;  case '7': return 7;    case '8': return 127;
    case '9': return 111;
    case 'A': return 119;  case 'D': return 94;   case 'E': return 121;
    case 'F': return 113;  case 'G': return 61;   case 'H': return 118;
    case 'I': return 48;   case 'L': return 56;   case 'N': return 84;
    case 'O': return 63;   case 'P': return 115;  case 'R': return 80;
    case 'S': return 109;  case 'T': return 120;  case 'U': return 62;
    default:  return 0;    /* space and unknown */
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

    if (c == '/') {
        GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
        GX_Position3f32(x + w - t, y,         -5.0f); GX_Color4u8(r, g, b, a);
        GX_Position3f32(x + w,     y,         -5.0f); GX_Color4u8(r, g, b, a);
        GX_Position3f32(x + t,     y + h,     -5.0f); GX_Color4u8(r, g, b, a);
        GX_Position3f32(x,         y + h,     -5.0f); GX_Color4u8(r, g, b, a);
        GX_End();
        return;
    }

    m = glyph_mask(c);
    if (m & 1)   hud_rect(x + t, y, w - 2.0f * t, t, r, g, b, a);           /* a */
    if (m & 2)   hud_rect(x + w - t, y + t * 0.5f, t, h * 0.5f - t, r, g, b, a); /* b */
    if (m & 4)   hud_rect(x + w - t, y + h * 0.5f + t * 0.5f, t,
                          h * 0.5f - t, r, g, b, a);                        /* c */
    if (m & 8)   hud_rect(x + t, y + h - t, w - 2.0f * t, t, r, g, b, a);   /* d */
    if (m & 16)  hud_rect(x, y + h * 0.5f + t * 0.5f, t, h * 0.5f - t,
                          r, g, b, a);                                      /* e */
    if (m & 32)  hud_rect(x, y + t * 0.5f, t, h * 0.5f - t, r, g, b, a);    /* f */
    if (m & 64)  hud_rect(x + t, y + h * 0.5f - t * 0.5f, w - 2.0f * t, t,
                          r, g, b, a);                                      /* g */
}

static void hud_text(float x, float y, float cw, float ch, const char *s,
                     u8 r, u8 g, u8 b, u8 a)
{
    while (*s) {
        hud_glyph(x, y, cw, ch, *s, r, g, b, a);
        x += cw * 1.35f;
        s++;
    }
}

static void draw_minimap(void)
{
    const Track *t = &game.track;
    float span_x = t->max_x - t->min_x;
    float span_z = t->max_z - t->min_z;
    float span = (span_x > span_z) ? span_x : span_z;
    float scale = 100.0f / span;
    float ox = (float)rmode->fbWidth - 130.0f;
    float oy = (float)rmode->efbHeight - 140.0f;
    int i;

    /* Track outline. Map world x -> screen x, world z -> screen y
     * (mirrored so the map matches the on-screen driving direction). */
    GX_SetLineWidth(14, GX_TO_ZERO);
    GX_Begin(GX_LINESTRIP, GX_VTXFMT0, (u16)(t->n + 1));
    for (i = 0; i <= t->n; i++) {
        int j = i % t->n;
        GX_Position3f32(ox + (t->px[j] - t->min_x) * scale,
                        oy + (t->max_z - t->pz[j]) * scale, -5.0f);
        GX_Color4u8(240, 240, 240, 200);
    }
    GX_End();

    for (i = NUM_KARTS - 1; i >= 0; i--) {
        const Kart *k = &game.karts[i];
        float mx = ox + (k->x - t->min_x) * scale;
        float my = oy + (t->max_z - k->z) * scale;
        float s = k->is_player ? 5.0f : 4.0f;
        hud_rect(mx - s * 0.5f, my - s * 0.5f, s, s,
                 kart_colors[i][0], kart_colors[i][1], kart_colors[i][2], 255);
    }
}

static void draw_hud(void)
{
    Mtx44 ortho;
    Mtx ident;
    const Kart *p = &game.karts[0];
    char buf[8];
    float W = (float)rmode->fbWidth;
    float H = (float)rmode->efbHeight;

    guOrtho(ortho, 0.0f, H, 0.0f, W, 0.0f, 300.0f);
    GX_LoadProjectionMtx(ortho, GX_ORTHOGRAPHIC);
    guMtxIdentity(ident);
    GX_LoadPosMtxImm(ident, GX_PNMTX0);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);

    /* lap counter, top-left: LAP n/3 */
    {
        int lap_disp = p->lap + 1;
        if (lap_disp < 1) lap_disp = 1;
        if (lap_disp > RACE_LAPS) lap_disp = RACE_LAPS;
        hud_text(28.0f, 28.0f, 13.0f, 22.0f, "LAP", 255, 255, 255, 220);
        buf[0] = (char)('0' + lap_disp);
        buf[1] = '/';
        buf[2] = (char)('0' + RACE_LAPS);
        buf[3] = '\0';
        hud_text(96.0f, 28.0f, 13.0f, 22.0f, buf, 255, 255, 255, 220);
    }

    /* position, top-right (left of minimap column) */
    {
        hud_text(W - 208.0f, 28.0f, 13.0f, 22.0f, "POS", 255, 255, 255, 220);
        buf[0] = (char)('0' + p->rank);
        buf[1] = '\0';
        hud_text(W - 138.0f, 24.0f, 17.0f, 28.0f, buf, 255, 220, 60, 240);
    }

    /* speed bar + drift charge, bottom-left */
    {
        float vfrac = fabsf(p->speed) / (KART_VMAX * KART_BOOST_MULT);
        if (vfrac > 1.0f) vfrac = 1.0f;
        hud_rect(26.0f, H - 46.0f, 154.0f, 16.0f, 15, 15, 20, 160);
        if (p->boost_t > 0.0f)
            hud_rect(28.0f, H - 44.0f, 150.0f * vfrac, 12.0f,
                     255, 150, 30, 230);
        else
            hud_rect(28.0f, H - 44.0f, 150.0f * vfrac, 12.0f,
                     90, 220, 90, 230);

        if (p->drifting) {
            float cfrac = p->drift_charge / 2.2f;
            u8 cr = 90, cg = 160, cb = 255;
            if (cfrac > 1.0f) { cfrac = 1.0f; cr = 255; cg = 120; cb = 30; }
            else if (p->drift_charge > 1.0f) { cr = 255; cg = 200; cb = 60; }
            hud_rect(26.0f, H - 66.0f, 154.0f, 10.0f, 15, 15, 20, 160);
            hud_rect(28.0f, H - 64.0f, 150.0f * cfrac, 6.0f, cr, cg, cb, 230);
        }
    }

    draw_minimap();

    /* countdown / GO */
    if (game.state == STATE_COUNTDOWN) {
        float c = game.countdown - 0.2f;
        if (c > 0.0f) {
            int n = (int)ceilf(c);
            if (n > 3) n = 3;
            buf[0] = (char)('0' + n);
            buf[1] = '\0';
            hud_text(W * 0.5f - 40.0f, H * 0.5f - 70.0f, 80.0f, 130.0f,
                     buf, 255, 230, 60, 240);
        }
    } else if (game.state == STATE_RACING && game.race_t < 0.8f) {
        hud_text(W * 0.5f - 110.0f, H * 0.5f - 70.0f, 80.0f, 130.0f,
                 "GO", 120, 255, 120, 240);
    }

    /* player finished: results overlay */
    if (game.state == STATE_FINISHED) {
        hud_rect(0.0f, 0.0f, W, H, 0, 0, 20, 110);
        hud_text(W * 0.5f - 198.0f, H * 0.5f - 100.0f, 52.0f, 86.0f,
                 "FINISH", 255, 255, 255, 240);
        hud_text(W * 0.5f - 66.0f, H * 0.5f + 20.0f, 40.0f, 66.0f,
                 "P", 255, 220, 60, 240);
        buf[0] = (char)('0' + game.karts[0].final_rank);
        buf[1] = '\0';
        hud_text(W * 0.5f + 4.0f, H * 0.5f + 20.0f, 40.0f, 66.0f,
                 buf, 255, 220, 60, 240);
    }
}

/* ------------------------------------------------------------------ */
/* Input: Wiimote (tilt / D-pad), Nunchuk, Classic Controller,        */
/* GameCube pad (= Xbox pads in Dolphin) and USB keyboard              */
/* ------------------------------------------------------------------ */

static int keyboard_ok = 0;
static u8 key_left, key_right, key_accel, key_brake, key_drift;

static void restart_race(void)
{
    game_init(&game);
    place_scenery();
}

/* signed x deflection (-1..1, right positive) of a wiiuse joystick */
static float stick_x(const joystick_t *js)
{
    if (js->mag < 0.2f)
        return 0.0f;
    return game_clampf(js->mag, 0.0f, 1.0f) *
           sinf(js->ang * ((float)M_PI / 180.0f));
}

static void poll_keyboard(void)
{
    keyboard_event ev;

    if (!keyboard_ok)
        return;
    while (KEYBOARD_GetEvent(&ev)) {
        u8 held;
        if (ev.type == KEYBOARD_DISCONNECTED) {
            key_left = key_right = key_accel = key_brake = key_drift = 0;
            continue;
        }
        if (ev.type != KEYBOARD_PRESSED && ev.type != KEYBOARD_RELEASED)
            continue;
        held = (ev.type == KEYBOARD_PRESSED);
        switch (ev.symbol) {
        case KS_Left:                       key_left = held;  break;
        case KS_Right:                      key_right = held; break;
        case KS_Up: case KS_x: case KS_X:   key_accel = held; break;
        case KS_Down: case KS_z: case KS_Z: key_brake = held; break;
        case KS_space:
        case KS_Shift_L: case KS_Shift_R:   key_drift = held; break;
        case KS_Return: case KS_r: case KS_R:
            if (held) restart_race();
            break;
        case KS_Escape:
            if (held) exit(0);
            break;
        default:
            break;
        }
    }
}

static void read_input(Input *in)
{
    u32 held, down;
    u32 gheld, gdown;
    const WPADData *wd;
    float steer = 0.0f;
    int tilt_ok = 1;

    WPAD_ScanPads();
    PAD_ScanPads();
    poll_keyboard();

    held = WPAD_ButtonsHeld(0);
    down = WPAD_ButtonsDown(0);
    gheld = PAD_ButtonsHeld(0);
    gdown = PAD_ButtonsDown(0);

    if ((down & (WPAD_BUTTON_HOME | WPAD_CLASSIC_BUTTON_HOME)) ||
        ((gheld & PAD_TRIGGER_Z) && (gdown & PAD_BUTTON_START)))
        exit(0);
    if ((down & (WPAD_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_PLUS)) ||
        (!(gheld & PAD_TRIGGER_Z) && (gdown & PAD_BUTTON_START)))
        restart_race();

    memset(in, 0, sizeof(*in));

    /* --- Wiimote buttons (sideways grip) --- */
    in->accel = (held & (WPAD_BUTTON_2 | WPAD_BUTTON_A)) != 0;
    in->brake = (held & WPAD_BUTTON_1) != 0;
    in->hop   = (held & WPAD_BUTTON_B) != 0;

    /* D-pad steering. Held sideways, the pad's UP points left; also
     * accept LEFT/RIGHT for normal grip. Positive steer = turn left. */
    if (held & (WPAD_BUTTON_UP | WPAD_BUTTON_LEFT))
        steer += 1.0f;
    if (held & (WPAD_BUTTON_DOWN | WPAD_BUTTON_RIGHT))
        steer -= 1.0f;

    /* --- Wiimote expansions --- */
    wd = WPAD_Data(0);
    if (wd) {
        if (wd->exp.type == WPAD_EXP_NUNCHUK) {
            /* stick steers; C or Z drifts; A/B on the remote as usual */
            steer -= stick_x(&wd->exp.nunchuk.js);
            if (held & (WPAD_NUNCHUK_BUTTON_C | WPAD_NUNCHUK_BUTTON_Z))
                in->hop = 1;
            in->brake |= (held & WPAD_BUTTON_B) != 0;
            tilt_ok = 0;
        } else if (wd->exp.type == WPAD_EXP_CLASSIC) {
            steer -= stick_x(&wd->exp.classic.ljs);
            if (held & (WPAD_CLASSIC_BUTTON_LEFT))  steer += 1.0f;
            if (held & (WPAD_CLASSIC_BUTTON_RIGHT)) steer -= 1.0f;
            in->accel |= (held & (WPAD_CLASSIC_BUTTON_A |
                                  WPAD_CLASSIC_BUTTON_X)) != 0;
            in->brake |= (held & (WPAD_CLASSIC_BUTTON_B |
                                  WPAD_CLASSIC_BUTTON_Y)) != 0;
            in->hop   |= (held & (WPAD_CLASSIC_BUTTON_FULL_R |
                                  WPAD_CLASSIC_BUTTON_FULL_L |
                                  WPAD_CLASSIC_BUTTON_ZR |
                                  WPAD_CLASSIC_BUTTON_ZL)) != 0;
            tilt_ok = 0;
        }

        /* Tilt steering (remote held sideways, no expansion): raising
         * the nose steers left, like a steering wheel. 7 deg deadzone. */
        if (tilt_ok) {
            float tilt = TILT_SIGN * -wd->orient.pitch;
            if (fabsf(tilt) > 7.0f)
                steer += game_clampf(tilt / 45.0f, -1.0f, 1.0f);
        }
    }

    /* --- GameCube controller (Dolphin: map an Xbox/any pad to
     * "GameCube Controller Port 1 > Standard Controller") --- */
    {
        s8 sx = PAD_StickX(0);
        if (sx > 18 || sx < -18)
            steer -= game_clampf((float)sx / 90.0f, -1.0f, 1.0f);
        in->accel |= (gheld & (PAD_BUTTON_A | PAD_BUTTON_X)) != 0;
        in->brake |= (gheld & PAD_BUTTON_B) != 0;
        in->hop   |= (gheld & (PAD_TRIGGER_R | PAD_TRIGGER_L)) != 0;
    }

    /* --- USB keyboard (Dolphin: Config > Wii > Connect USB Keyboard) */
    if (key_left)  steer += 1.0f;
    if (key_right) steer -= 1.0f;
    in->accel |= key_accel;
    in->brake |= key_brake;
    in->hop   |= key_drift;

    in->steer = game_clampf(steer, -1.0f, 1.0f);
}

/* ------------------------------------------------------------------ */
/* Setup and main loop                                                 */
/* ------------------------------------------------------------------ */

int main(void)
{
    void *gp_fifo;
    GXColor sky = { 110, 170, 235, 255 };
    f32 yscale;
    u32 xfbHeight;
    float dt;

    VIDEO_Init();
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC);
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

    /* fixed timestep from the video standard */
    dt = 1.0f / 60.0f;
    if ((rmode->viTVMode >> 2) == VI_PAL)
        dt = 1.0f / 50.0f;

    game_init(&game);
    place_scenery();
    cam_x = game.karts[0].x - cosf(game.karts[0].heading) * 8.5f;
    cam_y = 3.4f;
    cam_z = game.karts[0].z - sinf(game.karts[0].heading) * 8.5f;

    while (1) {
        Input in;

        read_input(&in);
        game_update(&game, &in, dt);

        /* rumble on boosts and wall hits */
        if (game.karts[0].just_boosted || game.karts[0].hit_wall)
            rumble_t = 0.18f;
        if (rumble_t > 0.0f) {
            rumble_t -= dt;
            WPAD_Rumble(0, 1);
            PAD_ControlMotor(PAD_CHAN0, PAD_MOTOR_RUMBLE);
        } else {
            WPAD_Rumble(0, 0);
            PAD_ControlMotor(PAD_CHAN0, PAD_MOTOR_STOP);
        }

        draw_scene();
        draw_hud();

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
