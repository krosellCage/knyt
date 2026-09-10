#include "handmade.h"
#include "handmade_math.h"
#include "handmade_shader.h"

// NOTE(yigit): Only for snprintf, which builds the "pointLights[2].quadratic"
// style uniform names in SetPointLightUniforms below.  The book concatenates
// those with std::string; there is none here.
#include <stdio.h>


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
        // NOTE(yigit): One and two channel images are real - Sponza ships its
        // alpha masks as greyscale PNGs.  Assuming three channels for anything
        // that is not RGBA reads three bytes per pixel out of a buffer holding
        // one, which walks off the end of the allocation.
        //
        // GL_UNPACK_ALIGNMENT is 1 below, which matters here: a greyscale row
        // is rarely a multiple of four bytes, and the default alignment of 4
        // would shear the image.
        uint32 Format = GL_RGB;
        switch(ChannelCount)
        {
            case 1: { Format = GL_RED; } break;
            case 2: { Format = GL_RG; } break;
            case 3: { Format = GL_RGB; } break;
            case 4: { Format = GL_RGBA; } break;
        }

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

// Book ch. 17.1 - the sun.  World space, pointing INTO the scene: down and
// slightly back-left, so it lights the tops of the containers.
global_variable const vec3 GlobalDirLightDirection = {-0.2f, -1.0f, -0.3f};

// Book ch. 17.3, p. 176 - four point lights scattered among the containers.
// WORLD space; SetPointLightUniforms converts each one to view space.  A lamp
// marker is drawn at each, so what you see is where the light is.
global_variable const vec3 GlobalPointLightPositions[] =
{
    { 0.7f,  0.2f,   2.0f},
    { 2.3f, -3.3f,  -4.0f},
    {-4.0f,  2.0f, -12.0f},
    { 0.0f,  0.0f,  -3.0f},
};

// Steps 1-2 of the OBJ loader: prove the tokenizer walks the file and the
// number parsers read it correctly, before any index de-duplication or GPU
// upload exists.  cube.obj is written by hand so the expected numbers are
// known: 8 / 4 / 6, six quads, which is 12 triangles and 24 face vertices
// before merging.
// Builds "data\Tyre.png" from "data\peugeot.obj" and "Tyre.png".
//
// NOTE(yigit): A .mtl names its textures relative to itself, not to the working
// directory, so the model's own folder has to be pasted back on.  Without this
// the loader looks for "Tyre.png" beside the executable and finds nothing.
internal void
ObjMakeSiblingFileName(char *Dest, uint32 DestSize, const char *ObjName,
                       const char *SiblingName)
{
    uint32 DirLength = 0;
    for(uint32 I = 0; ObjName[I]; ++I)
    {
        if((ObjName[I] == '\\') || (ObjName[I] == '/'))
        {
            DirLength = I + 1;
        }
    }

    uint32 Length = 0;
    while((Length < DirLength) && (Length < (DestSize - 1)))
    {
        Dest[Length] = ObjName[Length];
        ++Length;
    }

    for(uint32 I = 0; SiblingName[I] && (Length < (DestSize - 1)); ++I)
    {
        Dest[Length++] = SiblingName[I];
    }

    Dest[Length] = 0;
}

// Turns "data\peugeot.obj" into "data\peugeot.mtl".
//
// NOTE(yigit): Derived from the model's own name rather than read from the
// OBJ's "mtllib" line, which for this model names a file that was never
// distributed with it.  The name beside the model is the one that exists.
internal void
ObjMakeMaterialFileName(char *Dest, uint32 DestSize, const char *ObjName)
{
    uint32 Length = 0;
    while(ObjName[Length] && (Length < (DestSize - 1)))
    {
        Dest[Length] = ObjName[Length];
        ++Length;
    }
    Dest[Length] = 0;

    if((Length >= 4) &&
       (Dest[Length-4] == '.') && (Dest[Length-3] == 'o') &&
       (Dest[Length-2] == 'b') && (Dest[Length-1] == 'j'))
    {
        Dest[Length-3] = 'm';
        Dest[Length-2] = 't';
        Dest[Length-1] = 'l';
    }
}

// Loads a texture named by a material, reusing one already uploaded if an
// earlier submesh named the same file.
//
// NOTE(yigit): The submeshes built so far ARE the cache - a linear scan over a
// few dozen of them, cheaper than any structure built to avoid it.  It checks
// BOTH name fields, because one material's diffuse map can be another
// material's alpha mask, and uploading it twice would be silent waste.
internal uint32
GameLoadMaterialTexture(thread_context *Thread, game_memory *Memory, game_opengl_api *GL,
                        const char *ObjFileName, const char *MapName,
                        render_submesh *Submeshes, uint32 SubmeshCount)
{
    for(uint32 I = 0; I < SubmeshCount; ++I)
    {
        render_submesh *Other = Submeshes + I;

        if(Other->DiffuseTexture &&
           ObjNamesMatch2(Other->Material.DiffuseMapName, MapName))
        {
            return(Other->DiffuseTexture);
        }

        if(Other->AlphaTexture &&
           ObjNamesMatch2(Other->Material.AlphaMapName, MapName))
        {
            return(Other->AlphaTexture);
        }
    }

    char TextureFileName[256];
    ObjMakeSiblingFileName(TextureFileName, sizeof(TextureFileName), ObjFileName, MapName);

    return(GameLoadTexture(Thread, Memory, GL, TextureFileName, GL_REPEAT));
}

// Reads an OBJ, de-duplicates it into a vertex/index pair, and hands both to
// the GPU.  Chapters 18-20 of the book, without Assimp.
//
// NOTE(yigit): Arena is scratch and gets RESET on entry, so the caller must
// not keep anything in it.  A real model needs hundreds of megabytes to parse
// and none of it survives this call - glBufferData copies the vertices, so the
// CPU-side arrays are dead the moment they reach the GPU.
internal render_model
GameLoadModel(thread_context *Thread, game_memory *Memory, game_opengl_api *GL,
              memory_arena *Arena, const char *FileName)
{
    render_model Result = {};

    ResetArena(Arena);

    debug_read_file_result File = Memory->DEBUGPlatformReadEntireFile(Thread, FileName);
    if(!File.Contents)
    {
        Memory->DEBUGPlatformLog(Thread, "ERROR: Failed to read OBJ file: ");
        Memory->DEBUGPlatformLog(Thread, FileName);
        Memory->DEBUGPlatformLog(Thread, "\n");
        return(Result);
    }

    loaded_model Model = ObjLoadModel(Arena, (char *)File.Contents, File.ContentsSize);

    char Message[256];
    snprintf(Message, sizeof(Message),
             "%s: %u vertices, %u indices, %u triangles, %u submeshes, %u MB of scratch\n",
             FileName, Model.VertexCount, Model.IndexCount, Model.IndexCount / 3,
             Model.SubmeshCount, (uint32)(Arena->Used / (1024*1024)));
    Memory->DEBUGPlatformLog(Thread, Message);

    // NOTE(yigit): File.Contents is NOT freed here.  Model.Materials holds
    // names that point straight into this buffer, and they are still needed to
    // match against the .mtl below - freeing early is a use-after-free that
    // would read whatever the allocator handed out next.
    if(!Model.IndexCount)
    {
        Memory->DEBUGPlatformFreeFileMemory(Thread, File.Contents);
        return(Result);
    }

    GL->glGenVertexArrays(1, &Result.VAO);
    GL->glGenBuffers(1, &Result.VBO);
    GL->glGenBuffers(1, &Result.EBO);

    GL->glBindVertexArray(Result.VAO);

    GL->glBindBuffer(GL_ARRAY_BUFFER, Result.VBO);
    GL->glBufferData(GL_ARRAY_BUFFER, Model.VertexCount * sizeof(obj_vertex),
                     Model.Vertices, GL_STATIC_DRAW);

    // NOTE(yigit): Unlike GL_ARRAY_BUFFER, the ELEMENT buffer binding is stored
    // IN THE VAO.  So it has to be bound while the VAO is bound, and it must
    // not be unbound before the VAO is - unbind it first and the VAO forgets
    // its index buffer and the model draws nothing.  Same ordering trap as the
    // EBO in ch. 6.
    GL->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, Result.EBO);
    GL->glBufferData(GL_ELEMENT_ARRAY_BUFFER, Model.IndexCount * sizeof(uint32),
                     Model.Indices, GL_STATIC_DRAW);

    // obj_vertex was laid out to match an 8-float stride exactly, so these are
    // the three usual calls with sizeof doing the arithmetic.
    GL->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(obj_vertex),
                              (void *)0);
    GL->glEnableVertexAttribArray(0);

    GL->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(obj_vertex),
                              (void *)(3 * sizeof(real32)));
    GL->glEnableVertexAttribArray(1);

    GL->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(obj_vertex),
                              (void *)(6 * sizeof(real32)));
    GL->glEnableVertexAttribArray(2);

    GL->glBindVertexArray(0);

    Result.IndexCount = Model.IndexCount;

    // ---- Materials ------------------------------------------------------
    //
    // The .mtl is optional.  Without one every submesh keeps
    // ObjDefaultMaterial's grey, which is visibly placeholder rather than
    // invisible or black.
    char MaterialFileName[256];
    ObjMakeMaterialFileName(MaterialFileName, sizeof(MaterialFileName), FileName);

    obj_material *Materials = PushArray(Arena, Model.MaterialCount, obj_material);
    debug_read_file_result MaterialFile =
        Memory->DEBUGPlatformReadEntireFile(Thread, MaterialFileName);

    ObjParseMaterialLibrary((char *)MaterialFile.Contents, MaterialFile.ContentsSize,
                            Model.Materials, Model.MaterialCount, Materials);

    if(MaterialFile.Contents)
    {
        Memory->DEBUGPlatformFreeFileMemory(Thread, MaterialFile.Contents);
    }
    else
    {
        Memory->DEBUGPlatformLog(Thread, "  no material library at ");
        Memory->DEBUGPlatformLog(Thread, MaterialFileName);
        Memory->DEBUGPlatformLog(Thread, " - using defaults\n");
    }

    // The OBJ-side submesh list lives in scratch that the next load resets, so
    // it is copied into the render_model rather than pointed at.
    for(uint32 I = 0; I < Model.SubmeshCount; ++I)
    {
        if(Result.SubmeshCount >= MAX_SUBMESHES_PER_MODEL)
        {
            Memory->DEBUGPlatformLog(Thread, "WARNING: submesh limit hit, tail dropped\n");
            break;
        }

        render_submesh *Submesh = Result.Submeshes + Result.SubmeshCount++;
        Submesh->FirstIndex = Model.Submeshes[I].FirstIndex;
        Submesh->IndexCount = Model.Submeshes[I].IndexCount;
        Submesh->Material = Materials[Model.Submeshes[I].MaterialIndex];
        Submesh->DiffuseTexture = 0;
        Submesh->AlphaTexture = 0;

        if(Submesh->Material.HasDiffuseMap)
        {
            Submesh->DiffuseTexture =
                GameLoadMaterialTexture(Thread, Memory, GL, FileName,
                                        Submesh->Material.DiffuseMapName,
                                        Result.Submeshes, Result.SubmeshCount - 1);
        }

        if(Submesh->Material.HasAlphaMap)
        {
            Submesh->AlphaTexture =
                GameLoadMaterialTexture(Thread, Memory, GL, FileName,
                                        Submesh->Material.AlphaMapName,
                                        Result.Submeshes, Result.SubmeshCount - 1);
        }
    }

    // Safe now - every name has been resolved into a material by value.
    Memory->DEBUGPlatformFreeFileMemory(Thread, File.Contents);

    return(Result);
}

// Makes the fallback texture: a single white pixel.
internal uint32
GameCreateWhiteTexture(game_opengl_api *GL)
{
    uint32 Result = 0;
    uint8 White[4] = {255, 255, 255, 255};

    GL->glGenTextures(1, &Result);
    GL->glBindTexture(GL_TEXTURE_2D, Result);
    GL->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GL->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, White);

    return(Result);
}

// Draws one loaded model, a submesh at a time, setting that submesh's material
// before each call.
//
// NOTE(yigit): One glDrawElements per material rather than one per model.  A
// material change is a uniform change, and uniforms cannot vary within a draw -
// which is the whole reason the index buffer was reordered by material.
internal void
GameDrawModel(game_opengl_api *GL, uint32 Program, render_model *Model,
              mat4 ModelMatrix, uint32 WhiteTexture)
{
    if(!Model->IndexCount)
    {
        return;
    }

    // The At-setters below write to whatever program is bound, so this has to
    // happen before any of them - and it replaces the glUseProgram the
    // name-based setters used to do on every single write.
    GL->glUseProgram(Program);
    GL->glBindVertexArray(Model->VAO);

    // NOTE(yigit): Hoisted out of the loop.  A uniform's location is fixed for
    // the life of a linked program, and these four do not change between the
    // submeshes below - so a model with 24 materials goes from 96 driver string
    // lookups a frame to 4.
    //
    // Looked up per frame rather than cached in game_state on purpose: shader
    // hot reload relinks the program, and every location from the old one is
    // then meaningless.
    int32 ModelLocation         = GetUniformLocation(GL, Program, "model");
    int32 DiffuseColorLocation  = GetUniformLocation(GL, Program, "material.diffuseColor");
    int32 SpecularColorLocation = GetUniformLocation(GL, Program, "material.specularColor");
    int32 ShininessLocation     = GetUniformLocation(GL, Program, "material.shininess");

    SetUniformMat4At(GL, ModelLocation, ModelMatrix);

    for(uint32 SubmeshIndex = 0;
        SubmeshIndex < Model->SubmeshCount;
        ++SubmeshIndex)
    {
        render_submesh *Submesh = Model->Submeshes + SubmeshIndex;
        obj_material *Material = &Submesh->Material;

        SetUniformVec3At(GL, DiffuseColorLocation, Material->Diffuse);
        SetUniformVec3At(GL, SpecularColorLocation, Material->Specular);
        SetUniformFloatAt(GL, ShininessLocation, Material->Shininess);

        // White when the material named no texture, so the multiply in the
        // shader leaves Kd untouched.
        uint32 DiffuseTexture = Submesh->DiffuseTexture
            ? Submesh->DiffuseTexture
            : WhiteTexture;

        uint32 AlphaTexture = Submesh->AlphaTexture
            ? Submesh->AlphaTexture
            : WhiteTexture;

        GL->glActiveTexture(GL_TEXTURE0);
        GL->glBindTexture(GL_TEXTURE_2D, DiffuseTexture);
        GL->glActiveTexture(GL_TEXTURE1);
        GL->glBindTexture(GL_TEXTURE_2D, WhiteTexture);
        GL->glActiveTexture(GL_TEXTURE2);
        GL->glBindTexture(GL_TEXTURE_2D, AlphaTexture);

        // The last argument is a byte OFFSET into the bound element buffer,
        // not a pointer - a leftover from when this call could read indices
        // straight out of client memory.  It was 0 while there was one draw
        // per model; now it is where this material's run begins.
        GL->glDrawElements(GL_TRIANGLES, (int32)Submesh->IndexCount, GL_UNSIGNED_INT,
                           (void *)(memory_index)(Submesh->FirstIndex * sizeof(uint32)));
    }
}


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

    State->MarkerModel = GameLoadModel(Thread, Memory, GL, &State->TransientArena,
                                       "data\\cube.obj");

    // NOTE(yigit): Required now that there is a solid object.  Without it the
    // back faces draw over the front ones in whatever order they happen to be
    // listed, and the object looks turned inside out.  The depth buffer is
    // already cleared every frame and the context already has 24 depth bits.
    GL->glEnable(GL_DEPTH_TEST);
}

// Everything the container's shader needs except the model matrix, which
// changes per draw and so stays at the call site.  Pulled out of
// GameUpdateAndRender because the run of calls said nothing the name does not.
// Book ch. 17.3 - the uniform names here are "pointLights[2].quadratic" and
// friends.  The book builds them with std::string concatenation; with no
// std::string in this layer they are formatted into a scratch buffer instead.
//
// NOTE(yigit): A local buffer is safe because glGetUniformLocation copies the
// name it is handed - nothing keeps a pointer to it after the call returns.
internal void
SetPointLightUniforms(game_opengl_api *GL, uint32 Program, uint32 Index,
                      vec3 PositionView)
{
    char Name[64];

    snprintf(Name, sizeof(Name), "pointLights[%u].position", Index);
    SetUniformVec3(GL, Program, Name, PositionView);

    // Book ch. 16.2 - the table row for a range of about 50 units.
    snprintf(Name, sizeof(Name), "pointLights[%u].constant", Index);
    SetUniformFloat(GL, Program, Name, 1.0f);
    snprintf(Name, sizeof(Name), "pointLights[%u].linear", Index);
    SetUniformFloat(GL, Program, Name, 0.09f);
    snprintf(Name, sizeof(Name), "pointLights[%u].quadratic", Index);
    SetUniformFloat(GL, Program, Name, 0.032f);

    snprintf(Name, sizeof(Name), "pointLights[%u].ambient", Index);
    SetUniformVec3(GL, Program, Name, 0.05f, 0.05f, 0.05f);
    snprintf(Name, sizeof(Name), "pointLights[%u].diffuse", Index);
    SetUniformVec3(GL, Program, Name, 0.8f, 0.8f, 0.8f);
    snprintf(Name, sizeof(Name), "pointLights[%u].specular", Index);
    SetUniformVec3(GL, Program, Name, 1.0f, 1.0f, 1.0f);
}

internal void
SetLitUniforms(game_opengl_api *GL, uint32 Program,
               mat4 View, mat4 Projection,
               vec3 AmbientColor, vec3 DiffuseColor)
{
    SetUniformMat4(GL, Program, "view", View);
    SetUniformMat4(GL, Program, "projection", Projection);

    // NOTE(yigit): material.shininess, diffuseColor and specularColor are NOT
    // set here any more - they vary per submesh, so GameDrawModel sets them
    // immediately before each draw.

    // The SPOTLIGHT - the flashlight held at the camera.  It needs no position
    // or direction uniform: in view space the camera is the origin looking down
    // -Z, so the shader has both as constants.
    SetUniformVec3(GL, Program, "spotLight.ambient",  AmbientColor);
    SetUniformVec3(GL, Program, "spotLight.diffuse",  DiffuseColor);
    SetUniformVec3(GL, Program, "spotLight.specular", 1.0f, 1.0f, 1.0f);

    // Book ch. 16.2 - the table row for a range of about 50 units.  The
    // 3250-unit row (0.0014 / 0.000007) gives no visible falloff in a scene
    // this small.
    SetUniformFloat(GL, Program, "spotLight.constant", 1.0f);
    SetUniformFloat(GL, Program, "spotLight.linear", 0.09f);
    SetUniformFloat(GL, Program, "spotLight.quadratic", 0.032f);

    // Cos takes RADIANS.  Passing 12.5 raw is 12.5 radians, which works out as
    // a 3.8 degree cone - wrong, but close enough to look plausible.
    SetUniformFloat(GL, Program, "spotLight.cutOff", Cos(12.5f*Pi32 / 180.0f));
    SetUniformFloat(GL, Program, "spotLight.outerCutOff", Cos(17.5f*Pi32 / 180.0f));

    // Book ch. 17.1 - the directional light.  Its direction is given in WORLD
    // space and has to reach the shader in VIEW space, like everything else in
    // this pipeline.
    //
    // NOTE(yigit): W is 0, not 1.  A direction has no location, so the view
    // matrix's translation column must not touch it - the same reason the
    // vertex shader used mat3(view) back when this lived there.
    vec4 DirView = View * Vec4(GlobalDirLightDirection, 0.0f);
    SetUniformVec3(GL, Program, "dirLight.direction",
                   Vec3(DirView.X, DirView.Y, DirView.Z));

    SetUniformVec3(GL, Program, "dirLight.ambient",  0.05f, 0.05f, 0.05f);
    SetUniformVec3(GL, Program, "dirLight.diffuse",  0.4f,  0.4f,  0.4f);
    SetUniformVec3(GL, Program, "dirLight.specular", 0.5f,  0.5f,  0.5f);

    // Book ch. 17.2 - the four point lights.
    //
    // NOTE(yigit): W is 1 here, not 0.  A position DOES get slid by the view
    // matrix's translation - that is the entire difference from the direction
    // above, and getting it backwards is the classic way to end up with lights
    // that drift as the camera moves.
    for(uint32 LightIndex = 0;
        LightIndex < ArrayCount(GlobalPointLightPositions);
        ++LightIndex)
    {
        vec4 PosView = View * Vec4(GlobalPointLightPositions[LightIndex], 1.0f);
        SetPointLightUniforms(GL, Program, LightIndex,
                              Vec3(PosView.X, PosView.Y, PosView.Z));
    }


    // Which texture unit each sampler reads from.  Constant, but uniforms do
    // not survive a program rebuild, so they are re-sent every frame like the
    // rest.
    SetUniformInt(GL, Program, "material.diffuse", 0);
    SetUniformInt(GL, Program, "material.specular", 1);
    SetUniformInt(GL, Program, "material.alphaMask", 2);
}

// NOTE(yigit): View and projection have to be set here as well as on the lit
// program.  Uniforms belong to a program, not to the context - the ones set
// over there simply do not exist in this one.
internal void
SetLampUniforms(game_opengl_api *GL, uint32 Program,
                mat4 View, mat4 Projection, vec3 LightColor)
{
    SetUniformMat4(GL, Program, "view", View);
    SetUniformMat4(GL, Program, "projection", Projection);

    // Book ch. 14.4 exercise 1 - the marker takes the light's own colour, so
    // the lamp visibly matches what it is casting.
    SetUniformVec4(GL, Program, "LightColor", LightColor, 1.0f);
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
    mat4 Projection = Mat4Perspective(State->Camera.Zoom, Aspect, 0.1f, 100.0f);

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

}

extern "C" GAME_GET_SOUND_SAMPLES(GameGetSoundSamples)
{
    game_state *State = (game_state *)Memory->PermanentStorage;

    // NOTE(yigit): The music is loaded and ready but deliberately not playing.
    // Swap these two lines to hear it.
    WriteSilence(SoundBuffer);
    // PlaySoundLooping(&State->Music, &State->MusicPlayCursor, SoundBuffer);
}
