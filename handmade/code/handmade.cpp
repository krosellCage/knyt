#include "handmade.h"
#include "handmade_math.h"
#include "handmade_shader.h"

// NOTE(yigit): stb_image is third-party and does not compile clean under
// -W4 -WX, so warnings are turned off across the include only.
// STBI_NO_STDIO removes its file-opening path entirely, which forces image
// bytes to arrive from the platform layer the same way shader source does -
// the game layer never touches the filesystem itself.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#pragma warning(push, 0)
#include "stb_image.h"
#pragma warning(pop)

// NOTE(yigit): The game layer is platform-independent - no windows.h in here.
// Anything we need from the platform arrives through game_memory.


// One-time GL setup: geometry only.
// Called from GameUpdateAndRender on the first frame (!Memory->IsInitialized)
//
// NOTE(yigit): Shaders are NOT built here any more.  They are owned by
// GameUpdateShaderPrograms, which runs every frame and does the first load
// as well - see the comment on that function.

internal uint32
GameLoadTexture(thread_context *Thread, game_memory *Memory, game_opengl_api *GL,
                const char *FileName, uint32 WrapMode)
{
    uint32 Result = 0;

    debug_read_file_result File = Memory->DEBUGPlatformReadEntireFile(Thread, FileName);
    if(!File.Contents)
    {
        Memory->DEBUGPlatformLog(Thread, "ERROR: Failed to read texture file: ");
        Memory->DEBUGPlatformLog(Thread, FileName);
        Memory->DEBUGPlatformLog(Thread, "\n");
        return(Result);
    }

    stbi_set_flip_vertically_on_load(1); // OpenGL's (0,0) is bottom-left

    int32 Width, Height, ChannelCount;
    uint8 *Pixels = stbi_load_from_memory((const uint8 *)File.Contents, (int32)File.ContentsSize,
                                          &Width, &Height, &ChannelCount, 0);
    Memory->DEBUGPlatformFreeFileMemory(Thread, File.Contents);

    if(Pixels)
    {
        uint32 Format = (ChannelCount == 4) ? GL_RGBA : GL_RGB;

        GL->glGenTextures(1, &Result);
        GL->glBindTexture(GL_TEXTURE_2D, Result);

        GL->glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // rows are packed, not padded to 4

        GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, WrapMode);
        GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, WrapMode);
        GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        GL->glTexImage2D(GL_TEXTURE_2D, 0, (int32)Format, Width, Height, 0,
                         Format, GL_UNSIGNED_BYTE, Pixels);
        GL->glGenerateMipmap(GL_TEXTURE_2D);

        stbi_image_free(Pixels);
    }
    else
    {
        Memory->DEBUGPlatformLog(Thread, "ERROR: Failed to decode texture: ");
        Memory->DEBUGPlatformLog(Thread, FileName);
        Memory->DEBUGPlatformLog(Thread, "\n");
    }

    return(Result);
}

// Book ch. 12.1, p. 113 - where the light sits in world space.  The cube drawn
// here is purely a marker; only this vector feeds the lighting maths.
global_variable const vec3 GlobalLightPos = {1.2f, 1.0f, 2.0f};

internal void
GameInitOpenGL(thread_context *Thread, game_memory *Memory, game_state *State, game_opengl_api *GL)
{
    State->Texture[0] = GameLoadTexture(Thread, Memory, GL, "data\\container.jpg",GL_REPEAT);
    State->Texture[1] = GameLoadTexture(Thread, Memory, GL, "data\\lol.png",GL_REPEAT);

    // NOTE(yigit): Required now that there is a solid object.  Without it the
    // back faces draw over the front ones in whatever order they happen to be
    // listed, and the cube looks turned inside out.  The depth buffer is
    // already cleared every frame and the context already has 24 depth bits.
    GL->glEnable(GL_DEPTH_TEST);

    // Geometry: a cube as 36 loose vertices - six faces, two triangles each,
    // three corners each.  No index buffer: every face needs its own texture
    // coordinates, so corners shared in space are NOT shared in the buffer.
    // That is why the book stops using an EBO here.
    //
    // Each vertex is 8 floats - position, normal, texture coordinate - so the
    // stride is 32 bytes.  Book ch. 13.3, p. 117.
    //
    // NOTE(yigit): A normal is the direction a surface faces.  A single vertex
    // has no surface of its own, so on a general mesh you would derive them
    // from the neighbouring triangles - but a cube is six flat planes, so every
    // vertex on a face just gets that face's outward direction, written by
    // hand.  Look down each block below and the normal never changes: the six
    // faces are (0,0,-1), (0,0,1), (-1,0,0), (1,0,0), (0,-1,0), (0,1,0).
    //
    // This is also why the cube needs 36 loose vertices rather than 8 shared
    // ones: a corner belongs to three faces pointing three different ways, and
    // it can only carry one normal.
    float Vertices[] = {
        // positions          // normals           // tex coords
        -0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,  0.0f, 0.0f,
         0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,  1.0f, 0.0f,
         0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,  1.0f, 1.0f,
         0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,  1.0f, 1.0f,
        -0.5f,  0.5f, -0.5f,   0.0f,  0.0f, -1.0f,  0.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,   0.0f,  0.0f, -1.0f,  0.0f, 0.0f,

        -0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,  0.0f, 0.0f,
         0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,  1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
         0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,  1.0f, 1.0f,
        -0.5f,  0.5f,  0.5f,   0.0f,  0.0f,  1.0f,  0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f,   0.0f,  0.0f,  1.0f,  0.0f, 0.0f,

        -0.5f,  0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
        -0.5f, -0.5f, -0.5f,  -1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
        -0.5f, -0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,  0.0f, 0.0f,
        -0.5f,  0.5f,  0.5f,  -1.0f,  0.0f,  0.0f,  1.0f, 0.0f,

         0.5f,  0.5f,  0.5f,   1.0f,  0.0f,  0.0f,  1.0f, 0.0f,
         0.5f,  0.5f, -0.5f,   1.0f,  0.0f,  0.0f,  1.0f, 1.0f,
         0.5f, -0.5f, -0.5f,   1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
         0.5f, -0.5f, -0.5f,   1.0f,  0.0f,  0.0f,  0.0f, 1.0f,
         0.5f, -0.5f,  0.5f,   1.0f,  0.0f,  0.0f,  0.0f, 0.0f,
         0.5f,  0.5f,  0.5f,   1.0f,  0.0f,  0.0f,  1.0f, 0.0f,

        -0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,  0.0f, 1.0f,
         0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,  1.0f, 1.0f,
         0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,  1.0f, 0.0f,
         0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,  1.0f, 0.0f,
        -0.5f, -0.5f,  0.5f,   0.0f, -1.0f,  0.0f,  0.0f, 0.0f,
        -0.5f, -0.5f, -0.5f,   0.0f, -1.0f,  0.0f,  0.0f, 1.0f,

        -0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,  0.0f, 1.0f,
         0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,  1.0f, 1.0f,
         0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
         0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,  1.0f, 0.0f,
        -0.5f,  0.5f,  0.5f,   0.0f,  1.0f,  0.0f,  0.0f, 0.0f,
        -0.5f,  0.5f, -0.5f,   0.0f,  1.0f,  0.0f,  0.0f, 1.0f
    };

    State->VertexCount = ArrayCount(Vertices) / 8;

    GL->glGenBuffers(ArrayCount(State->VBO), State->VBO);
    GL->glGenVertexArrays(ArrayCount(State->VAO), State->VAO);

    // ---------------------------------------------------------------------
    // VAO[0] - the container.  Owns the buffer upload.
    // ---------------------------------------------------------------------
    GL->glBindVertexArray(State->VAO[0]);

    GL->glBindBuffer(GL_ARRAY_BUFFER, State->VBO[0]);
    GL->glBufferData(GL_ARRAY_BUFFER, sizeof(Vertices), Vertices, GL_STATIC_DRAW);

    // NOTE(yigit): The stride is 8 floats for ALL THREE attributes - it is the
    // distance to the next vertex, which does not depend on which field you
    // are reading.  Only the offset differs.
    //
    // Attribute 0 - aPos, 3 floats at offset 0
    GL->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    GL->glEnableVertexAttribArray(0);

    // Attribute 1 - aNormal, 3 floats at offset 12
    GL->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
    GL->glEnableVertexAttribArray(1);

    // Attribute 2 - aTexCoord, 2 floats at offset 24
    GL->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
    GL->glEnableVertexAttribArray(2);

    // ---------------------------------------------------------------------
    // VAO[1] - the light marker.  Book ch. 12.1, p. 112.
    //
// NOTE(yigit): Same VBO, no second upload - the vertices are already on
    // the GPU.  It gets its own VAO purely so that the attribute changes the
    // lighting chapters make to the container cannot reach the light source.
    //
    // NOTE(yigit): The marker reads only the first 3 floats of each vertex and
    // ignores the other 5, but the STRIDE still has to be 8 - that is how far
    // apart the vertices are in the buffer.  Book ch. 13.3, p. 118.
    // ---------------------------------------------------------------------
    GL->glBindVertexArray(State->VAO[1]);

    GL->glBindBuffer(GL_ARRAY_BUFFER, State->VBO[0]);
    GL->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
    GL->glEnableVertexAttribArray(0);

    GL->glBindVertexArray(0);
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


    // Book ch. 10.7 - mouse look.  The book needs GLFW callbacks and a
    // lastX/lastY pair to turn absolute cursor positions into offsets, plus
    // GLFW_CURSOR_DISABLED to capture the pointer.  Win32UpdateMouseWarping
    // already hides the cursor, re-centres it every frame, leaves the movement
    // in MouseDeltaX/Y, and zeroes them when the window is inactive - which is
    // also the fix for the book's "large sudden jump" on first focus.
    CameraProcessMouseLook(&State->Camera,
                           (real32)Input->MouseDeltaX,
                           (real32)Input->MouseDeltaY);

    // Book ch. 10.9 - scroll wheel zooms by narrowing the field of view.
    // MouseZ is in whole wheel notches; the platform divides out WHEEL_DELTA.
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
    mat4 Projection = Mat4Perspective(State->Camera.Zoom, Aspect, 0.1f, 100.0f);

    // ------------------------------------------------------------------
    // Render
    // ------------------------------------------------------------------

    GL->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    GL->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    real32 Radius = 2.0f;
    vec3 LightPos = Vec3(Sin(State->TimeSeconds) * Radius,
            1.0f,
            Cos(State->TimeSeconds) * Radius);

    // Book ch. 14.3 - cycle the light's colour.  Three different frequencies so
    // the channels drift out of phase and the hue keeps changing.
    //
    // NOTE(yigit): Sin returns -1..1, but colours live in 0..1 - a negative
    // channel just clamps to zero.  Raw Sin therefore spends about half its
    // time dark, which is why the marker cube kept going black.  The
    // *0.5 + 0.5 remaps the wave into 0..1 so it dims instead of vanishing.
    vec3 LightColor = Vec3(Sin(State->TimeSeconds * 2.0f) * 0.5f + 0.5f,
                           Sin(State->TimeSeconds * 0.7f) * 0.5f + 0.5f,
                           Sin(State->TimeSeconds * 1.3f) * 0.5f + 0.5f);

    vec3 DiffuseColor = LightColor * 0.5f;
    vec3 AmbientColor = DiffuseColor * 0.2f;
    // NOTE(yigit): Textures are unused from here until ch. 15, where they come
    // back as diffuse maps.  Left bound rather than deleted so that chapter is
    // a shader change instead of a rewrite.
    GL->glActiveTexture(GL_TEXTURE0);
    GL->glBindTexture(GL_TEXTURE_2D, State->Texture[0]);
    GL->glActiveTexture(GL_TEXTURE1);
    GL->glBindTexture(GL_TEXTURE_2D, State->Texture[1]);

    // ---------------------------------------------------------------------
    // The lit objects
    // ---------------------------------------------------------------------

    // Book ch. 13.4 - the shader needs the light's world position to work out
    // which way the light is coming from at each fragment.  Same vector the
    // marker cube is drawn at, so the two can never disagree.
    SetUniformVec3(GL, State->ShaderProgram[0], "lightPos",
                   LightPos);

    // View and projection are the same for every cube, so they only need
    // setting once - only the model matrix changes between draws.
    SetUniformMat4(GL, State->ShaderProgram[0], "view", View);
    SetUniformMat4(GL, State->ShaderProgram[0], "projection", Projection);

    // The MATERIAL is the surface itself - it does not change.  Coral.
    SetUniformVec3(GL, State->ShaderProgram[0], "material.ambient",  1.0f, 0.5f, 0.31f);
    SetUniformVec3(GL, State->ShaderProgram[0], "material.diffuse",  1.0f, 0.5f, 0.31f);
    SetUniformVec3(GL, State->ShaderProgram[0], "material.specular", 0.5f, 0.5f, 0.5f);
    SetUniformFloat(GL, State->ShaderProgram[0], "material.shininess", 32.0f);

    // The LIGHT is what changes.  Book ch. 14.3 puts the animated colours here,
    // not on the material - the lamp is changing colour, the paint is not.
    SetUniformVec3(GL, State->ShaderProgram[0], "light.ambient",  AmbientColor);
    SetUniformVec3(GL, State->ShaderProgram[0], "light.diffuse",  DiffuseColor);
    SetUniformVec3(GL, State->ShaderProgram[0], "light.specular", 1.0f, 1.0f, 1.0f);

    GL->glUseProgram(State->ShaderProgram[0]);
    GL->glBindVertexArray(State->VAO[0]);

    // One container at the origin, unrotated - book ch. 12.1.
    SetUniformMat4(GL, State->ShaderProgram[0], "model", Mat4Identity());

    GL->glDrawArrays(GL_TRIANGLES, 0, State->VertexCount);

    // ---------------------------------------------------------------------
    // The light marker
    //
    // NOTE(yigit): View and projection have to be set AGAIN here.  Uniforms
    // belong to a program, not to the context - the ones set on
    // ShaderProgram[0] above simply do not exist in this one.
    // ---------------------------------------------------------------------
    SetUniformMat4(GL, State->ShaderProgram[1], "view", View);
    SetUniformMat4(GL, State->ShaderProgram[1], "projection", Projection);


    // Scale on the RIGHT so it happens first: shrink the cube at the origin,
    // then move it out to the light.  The other order would scale the
    // translation too and put the marker at a fifth of the distance.
    mat4 LightModel = Mat4Mul(Mat4Translation(LightPos.X,
                                              LightPos.Y,
                                              LightPos.Z),
                              Mat4Scale(0.2f, 0.2f, 0.2f));

    SetUniformMat4(GL, State->ShaderProgram[1], "model", LightModel);

    // Book ch. 14.4 exercise 1 - the marker takes the light's own colour, so
    // the lamp visibly matches what it is casting.
    SetUniformVec4(GL, State->ShaderProgram[1], "LightColor", LightColor, 1.0f);

    GL->glUseProgram(State->ShaderProgram[1]);
    GL->glBindVertexArray(State->VAO[1]);
    GL->glDrawArrays(GL_TRIANGLES, 0, State->VertexCount);

    GL->glBindVertexArray(0);

}

extern "C" GAME_GET_SOUND_SAMPLES(GameGetSoundSamples)
{
    game_state *State = (game_state *)Memory->PermanentStorage;

    // NOTE(yigit): The music is loaded and ready but deliberately not playing.
    // Swap these two lines to hear it.
    WriteSilence(SoundBuffer);
    // PlaySoundLooping(&State->Music, &State->MusicPlayCursor, SoundBuffer);
}
