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
// style uniform names in handmade_render_opengl.h - there is no std::string
// here to glue them together with.  Must come before the two headers below,
// which both use it.
#include <stdio.h>

// NOTE(yigit): render BEFORE assets now - the loaders take a renderer and use
// its GL pointer, so the struct has to be declared first.
#include "handmade_render_opengl.h"
#include "handmade_assets.h"

// NOTE(yigit): Scene data, and it lives in the game layer now rather than in
// the backend.  That is the push buffer's actual effect: the scene says what
// its lights are, and the backend only decides how to express them.

// The sun.  World space, pointing INTO the scene: down and
// slightly back-left.
global_variable const vec3 GlobalDirLightDirection = {-0.2f, -1.0f, -0.3f};

internal void
GameInitScene(thread_context *Thread, game_memory *Memory, game_state *State)
{
    // Every mesh in the scene comes off disk.  Anything that wants a cube loads
    // cube.obj rather than carrying a vertex table.
    State->Model = GameLoadModel(Thread, Memory, State->Renderer, &State->TransientArena,
                                 "data\\sponza.obj");

    State->MarkerModel = GameLoadModel(Thread, Memory, State->Renderer, &State->TransientArena,
                                       "data\\cube.obj");

    State->Skybox = GameLoadCubemapCross(Thread, Memory, State->Renderer, &State->TransientArena,
                                         "data\\skybox.png");

    // The CPU array is the game's; the GPU buffer behind the handle is the
    // backend's.
    OverlayAllocate(&State->Overlay, &State->WorldArena);
    State->Overlay.Buffer = RendererCreateOverlayBuffer(State->Renderer);

    // TransientArena, not WorldArena - the atlas bitmap and stb's glyph table
    // are both dead once the texture is uploaded and the quads are copied out.
    State->DebugFont = GameLoadFont(Thread, Memory, State->Renderer, &State->TransientArena,
                                    "data\\font.ttf", 18.0f);
}

// ------------------------------------------------------------------------
// Debug sliders - see debug_ui in handmade.h.
// ------------------------------------------------------------------------

/*
  Once per frame, before any DebugSlider.

  NOTE(yigit): "Just pressed" is worked out HERE, from our own copy of last
  frame's button, and not from HalfTransitionCount.  The platform never resets
  the MOUSE buttons' HalfTransitionCount, and compares against the input
  buffer from two frames ago, so it cannot answer "did this go down this
  frame?".  EndedDown is correct every frame, and that is all this needs.

  Once per frame and not once per slider: if each slider updated
  MouseWasDown, the first one would mark the press as seen and every slider
  after it would miss it.
*/
internal void
DebugUIBegin(debug_ui *UI, game_input *Input)
{
    UI->MouseX = (real32)Input->MouseX;
    UI->MouseY = (real32)Input->MouseY;

    UI->MouseDown    = Input->MouseButtons[0].EndedDown;
    UI->MousePressed = UI->MouseDown && !UI->MouseWasDown;
}

// Once per frame, after the last DebugSlider.
internal void
DebugUIEnd(debug_ui *UI)
{
    // Let go: nothing is being dragged any more.
    if(!UI->MouseDown)
    {
        UI->ActiveValue = 0;
    }

    UI->MouseWasDown = UI->MouseDown;
}

/*
  A horizontal slider that edits *Value between Min and Max.  Handles the
  mouse and draws itself in the same call, every frame.

  X, Y is the top-left of the TRACK; the label sits just above it.
*/
internal void
DebugSlider(debug_ui *UI, overlay *Overlay, loaded_font *Font,
            real32 X, real32 Y, real32 Width,
            const char *Label, real32 *Value, real32 Min, real32 Max)
{
    real32 Height = 12.0f;

    // 1. Hover.
    bool32 Over = ((UI->MouseX >= X) && (UI->MouseX < (X + Width)) &&
                   (UI->MouseY >= Y) && (UI->MouseY < (Y + Height)));

    // 2. Grab.  PRESSED, not down: a drag that started somewhere else must
    //    not pick up every slider it passes over.
    if(UI->MousePressed && Over)
    {
        UI->ActiveValue = Value;
    }

    // 3. Drag.  Keyed on being the active slider, NOT on Over, so the drag
    //    carries on when the mouse slips above, below or past the ends - it
    //    only stops when the button is let go (DebugUIEnd).
    bool32 Active = (UI->ActiveValue == Value);
    if(Active)
    {
        real32 T = (UI->MouseX - X) / Width;
        if(T < 0.0f) { T = 0.0f; }
        if(T > 1.0f) { T = 1.0f; }

        *Value = Min + T*(Max - Min);
    }

    // 4. Draw, from the value AFTER step 3 may have changed it - so the
    //    slider never shows last frame's value while being dragged.
    //
    //    Clamped again because something else can move the value too: the
    //    arrow keys, or a range change while the game is running.
    real32 Fill = (*Value - Min) / (Max - Min);
    if(Fill < 0.0f) { Fill = 0.0f; }
    if(Fill > 1.0f) { Fill = 1.0f; }

    // Brighter while hovered, brightest while dragged - so it is obvious
    // which slider the mouse is about to take.
    vec4 FillColor = Vec4(0.45f, 0.45f, 0.50f, 1.0f);
    if(Over)   { FillColor = Vec4(0.60f, 0.60f, 0.68f, 1.0f); }
    if(Active) { FillColor = Vec4(0.95f, 0.70f, 0.30f, 1.0f); }

    // Back to front: each one is pushed after, and so drawn over, the last.
    OverlayPushRect(Overlay, Font, X, Y, Width, Height,
                    Vec4(0.0f, 0.0f, 0.0f, 0.6f));
    OverlayPushRect(Overlay, Font, X, Y, Fill*Width, Height, FillColor);
    OverlayPushRect(Overlay, Font, X + Fill*Width - 2.0f, Y - 2.0f, 4.0f, Height + 4.0f,
                    Vec4(1.0f, 1.0f, 1.0f, 1.0f));

    // The label, with its baseline just above the track.
    char Text[64];
    snprintf(Text, sizeof(Text), "%s  %.2f", Label, *Value);
    OverlayPushText(Overlay, Font, X, Y - 4.0f, Text, Vec4(0.95f, 0.93f, 0.85f, 1.0f));
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
        State->Exposure = 1.0f;
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

    if(!Input->CursorFree)
    {
        CameraProcessMouseLook(&State->Camera,
                (real32)Input->MouseDeltaX,
                (real32)Input->MouseDeltaY);
    }

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
#define EXPOSURE_ADJUST_RATE 0.3f
#define EXPOSURE_MIN 0.1f
#define EXPOSURE_MAX 8.0f
    if(Keyboard->ActionUp.EndedDown)
    {
        State->Exposure += EXPOSURE_ADJUST_RATE * DeltaTime;
    }
    if(Keyboard->ActionDown.EndedDown)
    {
        
        State->Exposure -= EXPOSURE_ADJUST_RATE * DeltaTime;
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

    if(State->Exposure > EXPOSURE_MAX)
    {
        State->Exposure = EXPOSURE_MAX;
    }

    if(State->Exposure < EXPOSURE_MIN)
    {
        State->Exposure = EXPOSURE_MIN;
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

    // NOTE(yigit): FIRST, before anything is pushed.  Reset empties the
    // buffer, so every command pushed ahead of it is thrown away unexecuted.
    RenderBufferReset(&State->RenderBuffer);

    // ------------------------------------------------------------------
    // The sun's camera, for the shadow map.
    //
    // A directional light has no position - it is infinitely far away and
    // its rays are parallel.  So the camera is ORTHOGRAPHIC (no perspective:
    // parallel rays stay parallel), and it is placed "far enough back" along
    // the light direction to see the whole scene.
    //
    // The box has to contain everything that should cast a shadow.  Sponza
    // at scale 0.02 is about 76 x 30 x 48 units around the origin, so a box
    // 90 units across, looking down from 60 units away, covers all of it.
    // ------------------------------------------------------------------
    vec3 SunDirection = Normalize(GlobalDirLightDirection);
    vec3 SceneCenter  = Vec3(0.0f, 10.0f, 0.0f);

    // Step BACK from the centre, against the direction the light travels.
    vec3 SunEye = SceneCenter - SunDirection*60.0f;

    mat4 SunView       = Mat4LookAt(SunEye, SceneCenter, Vec3(0.0f, 1.0f, 0.0f));
    mat4 SunProjection = Mat4Ortho(-45.0f, 45.0f, -45.0f, 45.0f, 1.0f, 150.0f);

    // Projection AFTER view, so on the LEFT - same order as a normal camera.
    mat4 LightSpace = Mat4Mul(SunProjection, SunView);

    PushShadowPass(&State->RenderBuffer, LightSpace);

    // Only things that CAST shadows go here.  Just Sponza - the lamp cubes
    // are light sources, and a light should not block its own light.
    PushModel(&State->RenderBuffer, &State->Model,
            Mat4Scale(0.02f, 0.02f, 0.02f), RenderProgram_Shadow, StencilMode_Off);

    PushBeginScene(&State->RenderBuffer, Input->WindowWidth, Input->WindowHeight);   

    PushClear(&State->RenderBuffer, Vec4(0.0f, 0.0f, 0.0f, 1.0f));

    PushCamera(&State->RenderBuffer, View, Projection);

    // NOTE(yigit): After the camera, not before.  The backend needs the view
    // matrix to move these lights into the space it lights in, and it takes
    // that from the camera command that ran ahead of this one.
    render_command_lighting *Lighting = PushLighting(&State->RenderBuffer);
    if(Lighting)
    {
        Lighting->DirLightDirectionWorld = GlobalDirLightDirection;
        Lighting->DirAmbient  = Vec3(0.05f, 0.05f, 0.05f);
        // NOTE(yigit): Times PI since the switch to PBR.  Its diffuse term
        // divides by PI - light scattered over a whole hemisphere of
        // directions - so the same number would light the scene a third as
        // brightly.  This keeps the old look; raise it for a harsher sun.
        Lighting->DirDiffuse  = Vec3(1.0f, 0.55f, 0.2f) * Pi32;
        Lighting->DirSpecular = Vec3(0.50f, 0.50f, 0.50f);

        // The sun is the only light.  No point lights: PushLighting starts
        // PointLightCount at 0, and the shader only loops over as many as it
        // is told there are.
    }

    // NOTE(yigit): Sponza is modelled at roughly 3700 x 1550 x 2300 units and
    // the far plane is 500, so at native scale most of the atrium sits outside
    // the frustum.  Scaling the model is the right fix rather than pushing far
    // out to 5000, which would spend the depth buffer on empty space and bring
    // back z-fighting.
    PushModel(&State->RenderBuffer, &State->Model,
              Mat4Scale(0.02f, 0.02f, 0.02f), RenderProgram_Lit, StencilMode_Off);

    // After every opaque draw, so the depth test hides the sky wherever
    // something is in front of it - see render_command_draw_skybox.
    PushSkybox(&State->RenderBuffer, State->Skybox);

    PushEndScene(&State->RenderBuffer, State->Exposure);
    // ---------------------------------------------------------------------
    // The 2D overlay - pushed last, so it sits on top of everything
    //
    // NOTE(yigit): Building the text is content, not rendering - it stays in
    // the game layer.  Only the flush crossed the seam.
    // ---------------------------------------------------------------------
    OverlayReset(&State->Overlay);

    OverlayPushRect(&State->Overlay, &State->DebugFont,
            6.0f, 6.0f, 360.0f, 4.0f*State->DebugFont.LineHeight + 10.0f,
            Vec4(0.0f, 0.0f, 0.0f, 0.5f));

    vec4 TextColor = Vec4(0.95f, 0.93f, 0.85f, 1.0f);
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
        OverlayPushText(&State->Overlay, &State->DebugFont, 12.0f, LineY, Line, TextColor);
        LineY += State->DebugFont.LineHeight;

        snprintf(Line, sizeof(Line), "pos %.1f %.1f %.1f   speed %.1f",
                 State->Camera.Position.X, State->Camera.Position.Y,
                 State->Camera.Position.Z, State->Camera.MovementSpeed);
        OverlayPushText(&State->Overlay, &State->DebugFont, 12.0f, LineY, Line, TextColor);
        LineY += State->DebugFont.LineHeight;

        snprintf(Line, sizeof(Line), "%u tris  %u submeshes  %u cmd bytes",
                 State->Model.IndexCount / 3, State->Model.SubmeshCount,
                 (uint32)State->RenderBuffer.Used);

        OverlayPushText(&State->Overlay, &State->DebugFont, 12.0f, LineY, Line, TextColor);

        snprintf(Line, sizeof(Line), "exposure %.1f",
                 State->Exposure);

        LineY += State->DebugFont.LineHeight;
        OverlayPushText(&State->Overlay, &State->DebugFont, 12.0f, LineY, Line,
                Vec4(1.0f, 0.8f, 0.2f, 1.0f));
    }

    if(Input->CursorFree)
    {
        // NOTE(yigit): These change State->Exposure AFTER PushEndScene has
        // already copied it into its command, so a drag shows up one frame
        // late.  At 60 fps nobody can see that, and it keeps the sliders with
        // the rest of the overlay.
        DebugUIBegin(&State->UI, Input);

        DebugSlider(&State->UI, &State->Overlay, &State->DebugFont,
                    12.0f, 130.0f, 300.0f,
                    "exposure", &State->Exposure, EXPOSURE_MIN, EXPOSURE_MAX);
        DebugSlider(&State->UI, &State->Overlay, &State->DebugFont,
                    12.0f, 170.0f, 300.0f,
                    "camera speed", &State->Camera.MovementSpeed,
                    CAMERA_MIN_SPEED, CAMERA_MAX_SPEED);

        DebugUIEnd(&State->UI);

        // The mouse marker last, so it is drawn over the sliders.
        OverlayPushRect(&State->Overlay, &State->DebugFont,
                (real32)Input->MouseX, (real32)Input->MouseY, 10.0f, 10.0f,
                Vec4(1.0f, 0.3f, 0.2f, 1.0f));
    }
    else
    {
        // Leaving cursor mode mid-drag must not leave a slider grabbed - the
        // next time Tab is pressed it would follow the mouse with no click.
        // MouseWasDown is cleared too, so a button held across the switch
        // does not count as already seen.
        State->UI.ActiveValue = 0;
        State->UI.MouseWasDown = false;
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
