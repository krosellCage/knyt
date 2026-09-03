#if !defined(HANDMADE_CAMERA_H)
/*
  NOTE(yigit): The book's Camera class (ch. 10.8, camera.h) with the same data
  and the same behaviour, but as a plain struct and free functions.  No
  methods, no constructor, no private section - the camera is data the caller
  owns, and every function takes a pointer to it.

  What that buys here: a camera can sit directly in game_state, which means it
  lives in PermanentStorage, survives a DLL reload, and is captured verbatim by
  the input recording system.  A class with a constructor would fight all three.

  Included from handmade.h, after the internal/Assert macros and Pi32.
*/

#include "handmade_math.h"

enum camera_movement
{
    CameraMovement_Forward,
    CameraMovement_Backward,
    CameraMovement_Left,
    CameraMovement_Right,
};

// NOTE(yigit): Angles are RADIANS throughout, not the degrees the book stores,
// because Sin and Cos take radians.  Conversions happen once, here.
#define CAMERA_DEFAULT_YAW         (-0.5f*Pi32)             // -90 deg, looks down -Z
#define CAMERA_DEFAULT_PITCH       0.0f
#define CAMERA_DEFAULT_SPEED       2.5f                     // units per second
#define CAMERA_DEFAULT_SENSITIVITY (0.04f*Pi32 / 180.0f)     // per pixel of mouse movement
#define CAMERA_DEFAULT_ZOOM        (45.0f*Pi32 / 180.0f)    // vertical field of view
#define CAMERA_PITCH_LIMIT         (89.0f*Pi32 / 180.0f)

// Bounds and rate for adjusting MovementSpeed at runtime (Q and E).
#define CAMERA_SPEED_ADJUST_RATE   4.0f                     // units/sec, per second held
#define CAMERA_MIN_SPEED           0.25f
#define CAMERA_MAX_SPEED           40.0f

struct camera
{
    vec3 Position;
    vec3 Front;     // unit vector the camera looks along
    vec3 Up;        // corrected up - perpendicular to Front, not the world up
    vec3 Right;
    vec3 WorldUp;   // which way is up in the world; never changes

    real32 Yaw;
    real32 Pitch;

    real32 MovementSpeed;
    real32 MouseSensitivity;
    real32 Zoom;    // vertical FOV, for Mat4Perspective
};

// Rebuilds Front/Right/Up from Yaw and Pitch.  Everything that changes an
// angle has to call this, which is why it comes first.
internal void
CameraUpdateVectors(camera *Camera)
{
    // Euler angles to a direction vector (book p. 101).  Cos(Pitch) scales the
    // horizontal part: looking straight up leaves nothing pointing sideways.
    Camera->Front = Normalize(Vec3(Cos(Camera->Yaw) * Cos(Camera->Pitch),
                                   Sin(Camera->Pitch),
                                   Sin(Camera->Yaw) * Cos(Camera->Pitch)));

    // NOTE(yigit): Right and Up are derived from Front rather than stored, so
    // they can never drift out of agreement with it.  Up is re-derived from
    // Right and Front - it is NOT WorldUp, which is why the camera stays level
    // when you look up or down instead of rolling.
    Camera->Right = Normalize(Cross(Camera->Front, Camera->WorldUp));
    Camera->Up = Normalize(Cross(Camera->Right, Camera->Front));
}

internal void
CameraInitialize(camera *Camera, vec3 Position)
{
    Camera->Position = Position;
    Camera->WorldUp = Vec3(0.0f, 1.0f, 0.0f);

    Camera->Yaw = CAMERA_DEFAULT_YAW;
    Camera->Pitch = CAMERA_DEFAULT_PITCH;

    Camera->MovementSpeed = CAMERA_DEFAULT_SPEED;
    Camera->MouseSensitivity = CAMERA_DEFAULT_SENSITIVITY;
    Camera->Zoom = CAMERA_DEFAULT_ZOOM;

    CameraUpdateVectors(Camera);
}

internal mat4
CameraGetViewMatrix(camera *Camera)
{
    // The target is always one unit ahead, so the camera keeps facing the same
    // way wherever it moves.
    return(Mat4LookAt(Camera->Position,
                      Camera->Position + Camera->Front,
                      Camera->Up));
}

// DeltaTime scales the step so the camera covers the same ground per second
// whatever the frame rate.
internal void
CameraProcessMovement(camera *Camera, camera_movement Direction, real32 DeltaTime)
{
    real32 Velocity = Camera->MovementSpeed * DeltaTime;

    switch(Direction)
    {
        case CameraMovement_Forward:  { Camera->Position += Camera->Front * Velocity; } break;
        case CameraMovement_Backward: { Camera->Position -= Camera->Front * Velocity; } break;
        case CameraMovement_Left:     { Camera->Position -= Camera->Right * Velocity; } break;
        case CameraMovement_Right:    { Camera->Position += Camera->Right * Velocity; } break;
    }
}

// DeltaX and DeltaY are raw pixel movement since the last frame.  DeltaY is
// negated inside because screen Y grows downward, while pushing the mouse up
// should raise the pitch.
internal void
CameraProcessMouseLook(camera *Camera, real32 DeltaX, real32 DeltaY)
{
    Camera->Yaw += DeltaX * Camera->MouseSensitivity;
    Camera->Pitch -= DeltaY * Camera->MouseSensitivity;

    // NOTE(yigit): Stop just short of straight up or down.  At exactly 90
    // degrees Front lines up with WorldUp, Cross returns zero, and the whole
    // basis collapses - the LookAt flip the book warns about on p. 103.
    if(Camera->Pitch > CAMERA_PITCH_LIMIT)
    {
        Camera->Pitch = CAMERA_PITCH_LIMIT;
    }
    if(Camera->Pitch < -CAMERA_PITCH_LIMIT)
    {
        Camera->Pitch = -CAMERA_PITCH_LIMIT;
    }

    CameraUpdateVectors(Camera);
}

// NOTE(yigit): Book ch. 10.9.  Nothing calls this yet - the platform layer
// sets Input->MouseZ to 0 and has a TODO about mousewheel support, so there is
// no scroll delta to feed it.
internal void
CameraProcessZoom(camera *Camera, real32 DeltaZ)
{
    Camera->Zoom -= DeltaZ * (1.0f*Pi32 / 180.0f);

    real32 MinZoom = 1.0f*Pi32 / 180.0f;
    real32 MaxZoom = 45.0f*Pi32 / 180.0f;
    if(Camera->Zoom < MinZoom) { Camera->Zoom = MinZoom; }
    if(Camera->Zoom > MaxZoom) { Camera->Zoom = MaxZoom; }
}

#define HANDMADE_CAMERA_H
#endif
