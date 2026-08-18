/*
 * camera.h — the chase camera.
 *
 * Kept out of main.c and free of Wii headers so the whole thing runs in
 * the host test suite: where the camera ends up, where it is looking, and
 * how it behaves through a reverse, a crest and a descent are all things
 * a test can assert on rather than things you have to see to believe.
 *
 * Everything it does is driven by the "camera" block of settings.json —
 * distance, height, look-ahead, how far and how fast it swings round when
 * you reverse, how much of the road's pitch it copies, and the limits it
 * will not go past. See config/README.md.
 */
#ifndef WIIKART_CAMERA_H
#define WIIKART_CAMERA_H

#include "game.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* where the camera is, and what it is aimed at */
    float x, y, z;
    float look_x, look_y, look_z;

    /*
     * How far the camera has swung round the car, in radians. 0 is the
     * normal view over the boot; PI has it round at the nose looking back
     * down the road, which is what you want when the car is reversing.
     * It is a continuous value, so everything between is a partial swing.
     */
    float orbit;

    /* the road's pitch as the camera has decided to believe it, radians,
     * positive uphill — smoothed so a bump is not a nod */
    float pitch;

    int   primed;        /* 0 until the first placement */
} CameraState;

/* Put the camera straight into its resting place behind the car, with no
 * easing — used when a race starts and after a respawn. */
void camera_reset(CameraState *c, const GameSettings *s,
                  const Kart *k, const Track *t);

/* Advance one frame. Safe to call with dt <= 0 (it does nothing) and with
 * a NaN dt (likewise). */
void camera_update(CameraState *c, const GameSettings *s,
                   const Kart *k, const Track *t, float dt);

/*
 * The angle the camera is looking down at the world, radians, positive
 * meaning "aimed at the ground". Tests use it to check the view never
 * ends up buried in the pavement or staring at the sky.
 */
float camera_view_pitch(const CameraState *c);

#ifdef __cplusplus
}
#endif

#endif /* WIIKART_CAMERA_H */
