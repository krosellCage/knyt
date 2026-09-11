/*
  NOTE(yigit): The game layer.  Platform-independent by construction - no
  windows.h, no Xlib, no file handles.  Everything it needs from the platform
  arrives through game_memory.

  Asset loading lives in handmade_assets.h and per-frame drawing in
  handmade_render.h; what is left here is the two entry points the platform
  calls and the one-time setup they need.
*/

#include "handmade.h"
#include "handmade_math.h"
#include "handmade_shader.h"

// NOTE(yigit): Only for snprintf, which builds the "pointLights[2].quadratic"
// style uniform names in handmade_render.h.  The book concatenates those with
// std::string; there is none here.  Must come before the two headers below,
// which both use it.
#include <stdio.h>

#include "handmade_assets.h"
#include "handmade_render.h"

internal void
GameInitOpenGL(thread_context *Thread, game_memory *Memory, game_state *State, game_opengl_api *GL)
{
    // NOTE(yigit): Both meshes come off disk now.  The 36-vertex cube table
    // that used to sit here - six faces written out by hand with their normals
    // - is gone, and so are the ten hardcoded container positions.  Anything
    // that wants a cube loads cube.obj.
    State->Model = GameLoadModel(Thread, Memory, GL, &State->TransientArena,
                                 "data\\sponza.obj");
    State->WhiteTexture = GameCreateWhiteTexture(GL);

    OverlayInitialize(&State->Overlay, GL, &State->WorldArena);

    // TransientArena, not WorldArena - the atlas bitmap and stb's glyph table
    // are both dead once the texture is uploaded and the quads are copied out.
    State->DebugFont = GameLoadFont(Thread, Memory, GL, &State->TransientArena,
                                    "data\\font.ttf", 18.0f);

    State->MarkerModel = GameLoadModel(Thread, Memory, GL, &State->TransientArena,
                                       "data\\cube.obj");

    // NOTE(yigit): Required now that there is a solid object.  Without it the
    // back faces draw over the front ones in whatever order they happen to be
    // listed, and the object looks turned inside out.  The depth buffer is
    // already cleared every frame and the context already has 24 depth bits.
    GL->glEnable(GL_DEPTH_TEST);
}

extern "C" GAME_UPDATE_AND_RENDER(GameUpdateAndRender)
{
    Assert((&Input->Controllers[0].Terminator - &Input->Controllers[0].Buttons[0]) ==
           (ArrayCount(Input->Controllers[0].Buttons)));
    Assert(sizeof(game_state) <= Memory->PermanentStorageSize);

    game_state *State = (game_state *)Memory->PermanentStorage;
    game_opengl_api *GL = &Memory->OpenGL;

    if(!Memory->IsInitialized)
    {
        // NOTE(yigit): game_state sits at the very front of PermanentStorage,
        // so the arena starts past it and is that much smaller.  Base it at
        // offset 0 instead and the first allocation would hand back memory
        // game_state is already using - silently corrupting shader handles
        // and texture IDs.
        InitializeArena(&State->WorldArena,
                        (memory_index)(Memory->PermanentStorageSize - sizeof(game_state)),
                        (uint8 *)Memory->PermanentStorage + sizeof(game_state));

        // NOTE(yigit): Model loading needs far more scratch than WorldArena
        // holds - the car alone parses to a couple of hundred megabytes - and
        // none of it outlives the upload to the GPU.  TransientStorage is a
        // whole gigabyte and was sitting unused.
        InitializeArena(&State->TransientArena,
                        (memory_index)Memory->TransientStorageSize,
                        (uint8 *)Memory->TransientStorage);

        // NOTE(yigit): PermanentStorage starts zeroed, so these would be all
        // zeros - and a zero-length CameraFront would make LookAt degenerate.
        CameraInitialize(&State->Camera, Vec3(0.0f, 0.0f, 3.0f));

        // NOTE(yigit): Loaded once into WorldArena.  Must come after
        // InitializeArena above, or PushArray has nowhere to put it.
        State->Music = LoadWAV(Thread, Memory, &State->WorldArena, "data\\cakkidikabusu.wav");


        GameInitOpenGL(Thread, Memory, State, GL);
        Memory->IsInitialized = true;
    }

    // NOTE(yigit): Builds the programs on the first frame and rebuilds any of
    // them whose .vert/.frag changed on disk since the last frame.  Unlike
    // GameInitOpenGL this is deliberately outside the IsInitialized guard, so
    // it keeps working across DLL reloads too.

    GameUpdateShaderPrograms(Thread, Memory, State, GL);

    State->TimeSeconds += Input->dtForFrame;

    game_controller_input *Keyboard = GetController(Input, 0);

    real32 DeltaTime = Input->dtForFrame;


    CameraProcessMouseLook(&State->Camera,
                           (real32)Input->MouseDeltaX,
                           (real32)Input->MouseDeltaY);

    CameraProcessZoom(&State->Camera, (real32)Input->MouseZ);

    if(Keyboard->MoveUp.EndedDown) // forward
    {
        CameraProcessMovement(&State->Camera, CameraMovement_Forward, DeltaTime);
    }
    if(Keyboard->MoveDown.EndedDown) // backward
    {
        CameraProcessMovement(&State->Camera, CameraMovement_Backward, DeltaTime);
    }
    if(Keyboard->MoveLeft.EndedDown) // strafe left
    {
        CameraProcessMovement(&State->Camera, CameraMovement_Left, DeltaTime);
    }
    if(Keyboard->MoveRight.EndedDown) // strafe right
    {
        CameraProcessMovement(&State->Camera, CameraMovement_Right, DeltaTime);
    }
    // Q and E adjust how fast the camera flies.  Held rather than tapped, so
    // the rate is scaled by DeltaTime like the movement itself - otherwise it
    // would change faster on a machine with a higher frame rate.
    if(Keyboard->LeftShoulder.EndedDown) // Q - slower
    {
        State->Camera.MovementSpeed -= CAMERA_SPEED_ADJUST_RATE * DeltaTime;
    }
    if(Keyboard->RightShoulder.EndedDown) // E - faster
    {
        State->Camera.MovementSpeed += CAMERA_SPEED_ADJUST_RATE * DeltaTime;
    }

    // NOTE(yigit): Clamped because the speed lives in game_state and persists
    // across DLL reloads.  Without a floor it would go negative and invert the
    // controls; without a ceiling a moment of leaning on E would leave the
    // camera unusable until restart.
    if(State->Camera.MovementSpeed < CAMERA_MIN_SPEED)
    {
        State->Camera.MovementSpeed = CAMERA_MIN_SPEED;
    }
    if(State->Camera.MovementSpeed > CAMERA_MAX_SPEED)
    {
        State->Camera.MovementSpeed = CAMERA_MAX_SPEED;
    }

    game_controller_input *Pad = GetController(Input, 1);
    if(Pad->IsConnected && Pad->IsAnalog)
    {

    }

    // NOTE(yigit): Guard the divide - a minimised window reports height 0,
    // which would make Aspect infinite and fill the matrix with NaNs.
    real32 Aspect = 1.0f;
    if(Input->WindowHeight > 0)
    {
        Aspect = (real32)Input->WindowWidth / (real32)Input->WindowHeight;
    }

    mat4 View = CameraGetViewMatrix(&State->Camera);
    // NOTE(yigit): NEAR is the lever here, not far.  The depth buffer is
    // non-linear, and the smallest distance it can resolve at depth z is
    // roughly z*z / (near * 2^24) - far barely appears in that at all.  Moving
    // near from 0.1 to 0.5 buys five times the usable range for nothing, which
    // is why far can go to 500 without z-fighting coming back.
    mat4 Projection = Mat4Perspective(State->Camera.Zoom, Aspect, 0.5f, 500.0f);

    // ------------------------------------------------------------------
    // Render
    // ------------------------------------------------------------------

    GL->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    GL->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    vec3 LightColor =  Vec3(1.0f, 1.0f, 1.0f);

    vec3 DiffuseColor = LightColor * 0.5f;
    vec3 AmbientColor = DiffuseColor * 0.2f;


    // NOTE(yigit): Named here rather than spelled State->ShaderProgram[N] at
    // every call site.  The names say which program is which; the array index
    // does not.
    uint32 LitProgram = State->ShaderProgram[0];
    uint32 LampProgram = State->ShaderProgram[1];
    uint32 OverlayProgram = State->ShaderProgram[2];

    // ---------------------------------------------------------------------
    // The model
    // ---------------------------------------------------------------------
    SetLitUniforms(GL, LitProgram, View, Projection, AmbientColor, DiffuseColor);

    GL->glUseProgram(LitProgram);

    // NOTE(yigit): Sponza is modelled at roughly 3700 x 1550 x 2300 units and
    // the projection has a far plane of 100, so at native scale the whole
    // atrium sits outside the frustum.  Scaling the model is the right fix
    // rather than pushing the far plane out to 5000, which would spend the
    // depth buffer on empty space and bring back z-fighting.
    GameDrawModel(GL, LitProgram, &State->Model,
                  Mat4Scale(0.02f, 0.02f, 0.02f), State->WhiteTexture);

    // ---------------------------------------------------------------------
    // The light markers - one per point light, so you can see where they are
    // ---------------------------------------------------------------------
    SetLampUniforms(GL, LampProgram, View, Projection, LightColor);

    GL->glUseProgram(LampProgram);

    for(uint32 LightIndex = 0;
        LightIndex < ArrayCount(GlobalPointLightPositions);
        ++LightIndex)
    {
        vec3 P = GlobalPointLightPositions[LightIndex];

        // Scale on the RIGHT so it happens first: shrink the cube at the
        // origin, then move it out to the light.  The other order would scale
        // the translation too and put the marker at a fifth of the distance.
        GameDrawModel(GL, LampProgram, &State->MarkerModel,
                      Mat4Mul(Mat4Translation(P.X, P.Y, P.Z),
                              Mat4Scale(0.2f, 0.2f, 0.2f)),
                      State->WhiteTexture);
    }

    GL->glBindVertexArray(0);

    // ---------------------------------------------------------------------
    // The 2D overlay - drawn last, so it sits on top of everything
    // ---------------------------------------------------------------------
    OverlayReset(&State->Overlay);

    // NOTE(yigit): TOP-left origin - Bottom and Top are passed the other way
    // round from the usual OpenGL convention, so Y grows DOWNWARD.  That is
    // what stb_truetype's glyph offsets assume and what text layout is
    // naturally expressed in, so matching it here means no per-quad flipping.
    mat4 OverlayProjection = Mat4Ortho(0.0f, (real32)Input->WindowWidth,
                                       (real32)Input->WindowHeight, 0.0f,
                                       -1.0f, 1.0f);

    {
        char Line[128];
        real32 LineY = State->DebugFont.LineHeight + 6.0f;

        snprintf(Line, sizeof(Line), "%.2f ms  %.0f fps",
                 1000.0f*Input->dtForFrame,
                 (Input->dtForFrame > 0.0f) ? (1.0f / Input->dtForFrame) : 0.0f);
        OverlayPushText(&State->Overlay, &State->DebugFont, 12.0f, LineY, Line);
        LineY += State->DebugFont.LineHeight;

        snprintf(Line, sizeof(Line), "pos %.1f %.1f %.1f   speed %.1f",
                 State->Camera.Position.X, State->Camera.Position.Y,
                 State->Camera.Position.Z, State->Camera.MovementSpeed);
        OverlayPushText(&State->Overlay, &State->DebugFont, 12.0f, LineY, Line);
        LineY += State->DebugFont.LineHeight;

        snprintf(Line, sizeof(Line), "%u tris  %u submeshes",
                 State->Model.IndexCount / 3, State->Model.SubmeshCount);
        OverlayPushText(&State->Overlay, &State->DebugFont, 12.0f, LineY, Line);
    }

    OverlayFlush(&State->Overlay, GL, OverlayProgram, OverlayProjection,
                 State->DebugFont.Texture, Vec3(0.95f, 0.93f, 0.85f));

}

extern "C" GAME_GET_SOUND_SAMPLES(GameGetSoundSamples)
{
    game_state *State = (game_state *)Memory->PermanentStorage;

    // NOTE(yigit): The music is loaded and ready but deliberately not playing.
    // Swap these two lines to hear it.
    WriteSilence(SoundBuffer);
    // PlaySoundLooping(&State->Music, &State->MusicPlayCursor, SoundBuffer);
}
