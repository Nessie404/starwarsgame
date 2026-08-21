/*
 * camera.h — the chase camera, adapted from WiiKart's for a rig instead
 * of a kart.
 *
 * Kept out of main.c and free of Wii headers so the whole thing runs in
 * the host test suite. WiiKart's reverse-swing behavior — the camera
 * walking round to the nose when the car backs up — turns out to be
 * exactly the "look back the way you're going" view a backing simulator
 * needs, driven by nothing but Rig.speed's sign, so this keeps that
 * mechanism close to verbatim. Two things are genuinely different from
 * a racetrack camera:
 *
 *   - distance scales with the whole rig's length (a bobtail yard
 *     tractor and a set of doubles cannot use the same follow distance
 *     and keep the trailer in shot);
 *   - the look-at point is pulled toward the LAST trailer's tail as the
 *     reverse swing completes, not just the tractor's nose, because
 *     watching the trailer is the entire point while backing.
 *
 * There is no track curvature to lean the camera into here, so the
 * corner lean instead reads the rig's own current steer angle — a
 * preview of which way the nose (and the tail) is about to swing.
 */
#ifndef WIIHAUL_CAMERA_H
#define WIIHAUL_CAMERA_H

#include "truck.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x, y, z;
    float look_x, look_y, look_z;
    float orbit;    /* 0 = normal chase view, PI = swung round to the nose */
    float pitch;    /* believed ground pitch, radians, smoothed            */
    int   primed;
} CameraState;

void camera_reset(CameraState *c, const HaulSettings *s, const Rig *r,
                  const RigSpec *spec, GroundFn ground_fn, void *ctx);
void camera_update(CameraState *c, const HaulSettings *s, const Rig *r,
                   const RigSpec *spec, GroundFn ground_fn, void *ctx,
                   float dt);

float camera_view_pitch(const CameraState *c);

/*
 * A small side-mirror camera: fixed relative to the tractor cab (not
 * eased or swung), looking backward and slightly outward along one side
 * of the rig — a real truck's only view of its own trailer while
 * backing. side = -1 for the driver's-side mirror, +1 for the other.
 */
void mirror_camera(const Rig *r, const RigSpec *spec, float side,
                   float *ex, float *ey, float *ez,
                   float *lx, float *ly, float *lz);

#ifdef __cplusplus
}
#endif

#endif /* WIIHAUL_CAMERA_H */
