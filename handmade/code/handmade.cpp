/*
  NOTE(yigit): The game layer.  Platform-independent by construction - no
  windows.h, no Xlib, no file handles.  Everything it needs from the platform
  arrives through game_memory.

  Asset loading lives in handmade_assets.h and per-frame drawing in
  handmade_render_opengl.h; what is left here is the two entry points the platform
  calls and the one-time setup they need.
*/

#include "handmade.h"
#include "handmade_math.h"
#include "handmade_shader.h"

// NOTE(yigit): Only for snprintf, which builds the "pointLights[2].quadratic"
// style uniform names in handmade_render_opengl.h.  The book concatenates those with
// std::string; there is none here.  Must come before the two headers below,
// which both use it.
#include <stdio.h>

// NOTE(yigit): render BEFORE assets now - the loaders take a renderer and use
// its GL pointer, so the struct has to be declared first.
#include "handmade_render_opengl.h"
#include "handmade_assets.h"

// NOTE(yigit): Scene data, and it lives in the game layer now rather than in
// the backend.  That is the push buffer's actual effect: the scene says what
// its lights are, and the backend only decides how to express them.

// Book ch. 17.1 - the sun.  World space, pointing INTO the scene: down and
// slightly back-left.
global_variable const vec3 GlobalDirLightDirection = {-0.2f, -1.0f, -0.3f};

// Book ch. 17.3, p. 176 - four point lights.  WORLD space; the backend
// converts to view space, because which space this renderer lights in is not
// something the scene should have to know.  A lamp marker is drawn at each, so
// what you see is where the light is.
global_variable const vec3 GlobalPointLightPositions[] =
{
    { 0.7f,  0.2f,   2.0f},
    { 2.3f, -3.3f,  -4.0f},
    {-4.0f,  2.0f, -12.0f},
    { 0.0f,  0.0f,  -3.0f},
};

internal void
GameInitScene(thread_context *Thread, game_memory *Memory, game_state *State)
{
    // NOTE(yigit): Both meshes come off disk now.  The 36-vertex cube table
    // that used to sit here - six faces written out by hand with their normals
    // - is gone, and so are the ten hardcoded container positions.  Anything
    // that wants a cube loads cube.obj.
    State->Model = GameLoadModel(Thread, Memory, State->Renderer, &State->TransientArena,
                                 "data\\sponza.obj");

    State->MarkerModel = GameLoadModel(Thread, Memory, State->Renderer, &State->TransientArena,
                                       "data\\cube.obj");

    OverlayInitialize(&State->Overlay, State->Renderer->GL, &State->WorldArena);

    // TransientArena, not WorldArena - the atlas bitmap and stb's glyph table
    // are both dead once the texture is uploaded and the quads are copied out.
    State->DebugFont = GameLoadFont(Thread, Memory, State->Renderer, &State->TransientArena,
                                    "data\\font.ttf", 18.0f);
}

extern "C" GAME_UPDATE_AND_RENDER(GameUpdateAndRender)
{
    Assert((&Input->Controllers[0].Terminator - &Input->Controllers[0].Buttons[0]) ==
           (ArrayCount(Input->Controllers[0].Buttons)));
    Assert(sizeof(game_state) <= Memory->PermanentStorageSize);

    game_state *State = (game_state *)Memory->PermanentStorage;

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

        // NOTE(yigit): A megabyte of command buffer, out of WorldArena so the
        // allocation happens once.  The BUFFER is reset every frame, not the
        // arena - the arena hands out its block a single time and never again.
        RenderBufferInitialize(&State->RenderBuffer, &State->WorldArena,
                               Megabytes(1));

        // NOTE(yigit): PermanentStorage starts zeroed, so these would be all
        // zeros - and a zero-length CameraFront would make LookAt degenerate.
        CameraInitialize(&State->Camera, Vec3(0.0f, 0.0f, 3.0f));

        // NOTE(yigit): Loaded once into WorldArena.  Must come after
        // InitializeArena above, or PushArray has nowhere to put it.
        State->Music = LoadWAV(Thread, Memory, &State->WorldArena, "data\\cakkidikabusu.wav");


        // NOTE(yigit): The renderer comes first - every loader below takes one
        // and pulls its api pointer out of it.
        State->Renderer = RendererInitialize(Memory, &State->WorldArena);

        GameInitScene(Thread, Memory, State);
        Memory->IsInitialized = true;
    }

    // NOTE(yigit): Builds the programs on the first frame and rebuilds any of
    // them whose .vert/.frag changed on disk since the last frame.  Unlike
    // GameInitScene this is deliberately outside the IsInitialized guard, so
    // it keeps working across DLL reloads too.

    RendererUpdateShaders(State->Renderer, Thread, Memory);

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
    //
    // NOTE(yigit): Nothing below calls OpenGL.  It describes a frame into a
    // buffer, and RenderBufferExecute at the bottom turns that description
    // into GL calls.  Swapping in a Vulkan backend means writing a second
    // RenderBufferExecute and changing nothing here.
    // ------------------------------------------------------------------
    RenderBufferReset(&State->RenderBuffer);

    PushClear(&State->RenderBuffer, Vec4(0.0f, 0.0f, 0.0f, 1.0f));

    render_command_setup *Setup = PushSetup(&State->RenderBuffer, View, Projection);
    if(Setup)
    {
        Setup->DirLightDirectionWorld = GlobalDirLightDirection;
        Setup->DirAmbient  = Vec3(0.05f, 0.05f, 0.05f);
        Setup->DirDiffuse  = Vec3(0.40f, 0.40f, 0.40f);
        Setup->DirSpecular = Vec3(0.50f, 0.50f, 0.50f);

        Setup->PointLightCount = ArrayCount(GlobalPointLightPositions);
        Assert(Setup->PointLightCount <= ArrayCount(Setup->PointLights));

        for(uint32 LightIndex = 0;
            LightIndex < Setup->PointLightCount;
            ++LightIndex)
        {
            render_point_light *Light = Setup->PointLights + LightIndex;

            Light->PositionWorld = GlobalPointLightPositions[LightIndex];
            Light->Ambient  = Vec3(0.05f, 0.05f, 0.05f);
            Light->Diffuse  = Vec3(0.80f, 0.80f, 0.80f);
            Light->Specular = Vec3(1.00f, 1.00f, 1.00f);

            // Book ch. 16.2 - the table row for a range of about 50 units.
            Light->Constant  = 1.0f;
            Light->Linear    = 0.09f;
            Light->Quadratic = 0.032f;
        }

        // The flashlight held at the camera.  No position or direction: in view
        // space the camera is the origin looking down -Z, so the shader has
        // both as constants.
        Setup->SpotAmbient  = Vec3(0.10f, 0.10f, 0.10f);
        Setup->SpotDiffuse  = Vec3(0.50f, 0.50f, 0.50f);
        Setup->SpotSpecular = Vec3(1.00f, 1.00f, 1.00f);
        Setup->SpotConstant  = 1.0f;
        Setup->SpotLinear    = 0.09f;
        Setup->SpotQuadratic = 0.032f;

        // NOTE(yigit): Converted to cosines HERE, once, rather than in the
        // backend every frame.  Cos takes radians - passing 12.5 raw would be
        // 12.5 radians, which works out as a 3.8 degree cone: wrong, but close
        // enough to look plausible.
        Setup->SpotCutOff      = Cos(12.5f*Pi32 / 180.0f);
        Setup->SpotOuterCutOff = Cos(17.5f*Pi32 / 180.0f);
    }

    // NOTE(yigit): Sponza is modelled at roughly 3700 x 1550 x 2300 units and
    // the far plane is 500, so at native scale most of the atrium sits outside
    // the frustum.  Scaling the model is the right fix rather than pushing far
    // out to 5000, which would spend the depth buffer on empty space and bring
    // back z-fighting.
    PushModel(&State->RenderBuffer, &State->Model,
              Mat4Scale(0.02f, 0.02f, 0.02f), RenderProgram_Lit);

    for(uint32 LightIndex = 0;
        LightIndex < ArrayCount(GlobalPointLightPositions);
        ++LightIndex)
    {
        vec3 P = GlobalPointLightPositions[LightIndex];

        // Scale on the RIGHT so it happens first: shrink the cube at the
        // origin, then move it out to the light.  The other order would scale
        // the translation too and put the marker at a fifth of the distance.
        PushModel(&State->RenderBuffer, &State->MarkerModel,
                  Mat4Mul(Mat4Translation(P.X, P.Y, P.Z),
                          Mat4Scale(0.2f, 0.2f, 0.2f)),
                  RenderProgram_Lamp);
    }

    // ---------------------------------------------------------------------
    // The 2D overlay - pushed last, so it sits on top of everything
    //
    // NOTE(yigit): Building the text is content, not rendering - it stays in
    // the game layer.  Only the flush crossed the seam.
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

        snprintf(Line, sizeof(Line), "%u tris  %u submeshes  %u cmd bytes",
                 State->Model.IndexCount / 3, State->Model.SubmeshCount,
                 (uint32)State->RenderBuffer.Used);
        OverlayPushText(&State->Overlay, &State->DebugFont, 12.0f, LineY, Line);
    }

    PushOverlay(&State->RenderBuffer, &State->Overlay, &State->DebugFont,
                OverlayProjection, Vec3(0.95f, 0.93f, 0.85f));

    // ------------------------------------------------------------------
    // Execute
    //
    // ONE walk of the buffer for the whole frame, at the very end.  Everything
    // above only described what it wanted.
    //
    // NOTE(yigit): The program handles are gathered here every frame rather
    // than kept in the backend, because shader hot reload relinks them and a
    // stored handle would be stale the moment a .frag is saved.
    // ------------------------------------------------------------------
    RenderBufferExecute(State->Renderer, &State->RenderBuffer);
}

extern "C" GAME_GET_SOUND_SAMPLES(GameGetSoundSamples)
{
    game_state *State = (game_state *)Memory->PermanentStorage;

    // NOTE(yigit): The music is loaded and ready but deliberately not playing.
    // Swap these two lines to hear it.
    WriteSilence(SoundBuffer);
    // PlaySoundLooping(&State->Music, &State->MusicPlayCursor, SoundBuffer);
}
