// SSSV Recompiled - Free Camera Mod
// Hold Middle Mouse Button and move the mouse to freely orbit the camera
// around your current animal.
//
// How it works:
//   SSSV stores its camera yaw as an angle in "256 units per full circle"
//   (gCameras[gCameraId].unk24 = target yaw, unk20 = current smoothed yaw,
//   unk28 = yaw velocity used by the game's smoothing). The vanilla C-Left /
//   C-Right buttons snap unk24 in 32-unit (45 degree) steps and the engine
//   smoothly chases it, including its own camera-vs-wall avoidance.
//
//   This mod hooks func_8033E7C8_74FE78 (the per-frame camera input
//   dispatcher, which receives the controller pad and only runs while the
//   camera is accepting player input). While the middle mouse button is held,
//   mouse X deltas are added straight into the yaw, and the current yaw is
//   snapped to the target for crisp 1:1 response. Vanilla C-button behavior
//   is untouched and still works.
//
//   Mouse state comes from a tiny companion native library
//   (sssv_freecam_native) because the recompiled game code cannot touch SDL
//   directly.
//
// Symbols verified against:
//   - SSSVRecompSyms/sssv.us.syms.toml     (func_8033E7C8_74FE78 @ 0x8033E7C8)
//   - SSSVRecompSyms/sssv.us.datasyms.toml (gCameras @ 0x803F28E0, gCameraId @ 0x803F2A98)
//   - mkst/sssv decomp: src.us/sssv/camera.c, include/structs.h (Camera, size 0xDC)

#include "modding.h"

typedef signed char        s8;
typedef unsigned char      u8;
typedef signed short       s16;
typedef unsigned short     u16;
typedef signed int         s32;
typedef unsigned int       u32;
typedef float              f32;

// ---------------------------------------------------------------------------
// Tuning - edit these and rebuild to taste.
// ---------------------------------------------------------------------------

// Yaw sensitivity: camera angle units per mouse count. 256 units = 360
// degrees, so 0.25 means ~2.8 degrees per 10 counts of mouse movement.
#define FREECAM_SENSITIVITY   0.22f

// Set to 1 to invert horizontal orbit direction.
#define FREECAM_INVERT_X      0

// Delay (in game frames, 30 per second) after you release MMB before the
// game's auto-recenter (which rotates the camera back behind the animal's
// heading) is allowed to kick in. Vanilla C-button rotation uses 200 (~6.7s).
#define FREECAM_RECENTER_DELAY  300

// Set to 1 to disable auto-recentering entirely while in chase modes.
// The camera then always stays where you put it (wall avoidance still works).
#define FREECAM_NEVER_RECENTER  0

// ---------------------------------------------------------------------------
// Game structures (minimal, offsets from the decomp's Camera struct)
// ---------------------------------------------------------------------------

typedef struct {
    u16 button;
    s8  stick_x;
    s8  stick_y;
} OSContPad;

typedef struct {
    /* 0x00 */ s16 cameraMode;
    /* 0x02 */ s16 unk2;
    /* 0x04 */ s16 unk4;
    /* 0x06 */ s16 unk6;
    /* 0x08 */ f32 targetX;      // unk8  - look-at target
    /* 0x0C */ f32 targetZ;      // unkC
    /* 0x10 */ f32 targetY;      // unk10
    /* 0x14 */ f32 pitchCur;     // unk14 - current pitch (256 units/circle)
    /* 0x18 */ f32 pitchTarget;  // unk18 - target pitch (rewritten per-frame by mode code)
    /* 0x1C */ f32 pitchVel;     // unk1C
    /* 0x20 */ f32 yawCur;       // unk20 - current smoothed yaw
    /* 0x24 */ f32 yawTarget;    // unk24 - target yaw, what C-buttons snap
    /* 0x28 */ f32 yawVel;       // unk28 - yaw smoothing velocity
    /* 0x2C */ f32 unk2C;
    /* 0x30 */ f32 distCur;      // unk30 - current camera distance
    /* 0x34 */ f32 distTarget;   // unk34
    /* 0x38 */ f32 distVel;      // unk38
    /* 0x3C */ u8  pad3C[0x70 - 0x3C];
    /* 0x70 */ s16 autoAlignDelay;  // unk70 - frames until auto-recenter may run;
                                    //   decremented once per frame by the camera
                                    //   update, C-buttons arm it to 200/60
    /* 0x72 */ u8  pad72[0xD6 - 0x72];
    /* 0xD6 */ s8  playerControlled; // unkD6 - 1 when the player may move this camera
    /* 0xD7 */ u8  canRotateLeft;    // unkD7 - set by wall checks
    /* 0xD8 */ u8  canRotateRight;   // unkD8
    /* 0xD9 */ u8  padD9[3];
} SSSVCamera; // size 0xDC

_Static_assert(sizeof(SSSVCamera) == 0xDC, "SSSVCamera size must match the game's Camera struct");

extern SSSVCamera gCameras[2];
extern s16 gCameraId;

// Camera modes (from the decomp's camera_enums.h) in which the player has
// rotational control of the chase camera. Everything else (cutscene, waypoint,
// fixed-angle rooms, behind-modes) is left alone.
#define CAMERA_MODE_1   1
#define CAMERA_MODE_2   2
#define CAMERA_MODE_12  12
#define CAMERA_MODE_26  26

// ---------------------------------------------------------------------------
// Native import: fills out[0..3] with { dx, dy, sdl_button_mask, has_focus }.
// dx/dy are raw mouse counts since the previous poll. "." imports from this
// mod itself, which resolves to the native library declared in mod.toml.
// ---------------------------------------------------------------------------
RECOMP_IMPORT(".", void SSSVFreeCam_PollMouse(s32* out));

#define SDL_BUTTON_MMASK 2  // SDL_BUTTON(SDL_BUTTON_MIDDLE) == 1 << (3-1) >> 1 == bit 1

// ---------------------------------------------------------------------------
// The hook. Runs at the entry of the vanilla camera-input dispatcher, every
// frame the camera processes player input, before vanilla logic executes.
// ---------------------------------------------------------------------------
RECOMP_HOOK("func_8033E7C8_74FE78")
void freecam_on_camera_input(OSContPad* cont) {
    s32 mouse[4] = { 0, 0, 0, 0 };
    SSSVCamera* cam;
    f32 delta;

    (void)cont;

    SSSVFreeCam_PollMouse(mouse);

    // Ignore input when the window isn't focused.
    if (mouse[3] == 0) {
        return;
    }

    cam = &gCameras[gCameraId];

#if FREECAM_NEVER_RECENTER
    // Keep the suppression timer topped up every frame in player chase modes,
    // so auto-recenter never engages. Wall-escape rotation is not gated by
    // this timer, so the camera can still free itself from geometry.
    if (cam->playerControlled == 1 &&
        (cam->cameraMode == CAMERA_MODE_1 || cam->cameraMode == CAMERA_MODE_2 ||
         cam->cameraMode == CAMERA_MODE_12 || cam->cameraMode == CAMERA_MODE_26)) {
        if (cam->autoAlignDelay < 1000) {
            cam->autoAlignDelay = 1000;
        }
    }
#endif

    // Only act while the middle mouse button is held.
    if ((mouse[2] & SDL_BUTTON_MMASK) == 0) {
        return;
    }

    // Only touch cameras the player is allowed to control, in the normal
    // chase-camera modes. This keeps cutscenes, waypoint cams, tank mode,
    // and fixed rooms exactly as the game intends.
    if (cam->playerControlled != 1) {
        return;
    }
    if (cam->cameraMode != CAMERA_MODE_1 && cam->cameraMode != CAMERA_MODE_2 &&
        cam->cameraMode != CAMERA_MODE_12 && cam->cameraMode != CAMERA_MODE_26) {
        return;
    }

    delta = (f32)mouse[0] * FREECAM_SENSITIVITY;
#if FREECAM_INVERT_X
    delta = -delta;
#endif

    cam->yawTarget += delta;

    // Keep the angle in [0, 256) like the game does everywhere else.
    while (cam->yawTarget >= 256.0f) { cam->yawTarget -= 256.0f; }
    while (cam->yawTarget <   0.0f) { cam->yawTarget += 256.0f; }

    // Snap the smoothed yaw to the target and kill the smoothing velocity so
    // the orbit tracks the mouse 1:1 instead of lagging behind. The moment
    // the button is released, the game's own smoothing and wall avoidance
    // resume from wherever you left the camera.
    cam->yawCur = cam->yawTarget;
    cam->yawVel = 0.0f;

    // Arm the game's own auto-recenter suppression timer (the same field the
    // vanilla C-buttons set to 200). It counts down once per frame starting
    // the moment you release MMB, so this is the post-release delay before
    // the camera starts drifting back behind the animal's heading.
    cam->autoAlignDelay = FREECAM_RECENTER_DELAY;
}
