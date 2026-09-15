#if !defined(HANDMADE_RENDER_OPENGL_H)
/*
  HOW to draw it, in OpenGL.  The other half is handmade_render_commands.h,
  which describes WHAT.

  NOTE(yigit): This is the BACKEND side of the seam.  It reads the command
  buffer that handmade_render_commands.h defines and turns each command into GL
  calls, and it is the only place in the game DLL that knows OpenGL exists.

  The _opengl on the end is the point of the name: a Vulkan backend would be
  handmade_render_vulkan.h, sitting right beside this one, implementing the
  same RenderBufferExecute against the same command stream.  Nothing in
  handmade.cpp would change.

  Everything here runs per frame.  Loading assets is handmade_assets.h.
*/

/*
  The renderer, as an object the game holds but never looks inside.

  NOTE(yigit): game_state declares this as an incomplete type and stores only a
  POINTER to it.  That is what keeps game_opengl_api out of handmade.h - the
  game knows a renderer exists and can pass it around, and knows nothing about
  what is in it.  A Vulkan backend defines the same name with entirely
  different contents and the game layer does not change.

  It owns the shader programs, which is why the hot-reload loop lives here
  rather than in handmade_shader.h.  Polling files and swapping program handles
  is backend work.
*/
/*
  Every uniform location the lit program has, looked up once per link.

  NOTE(yigit): Locations were looked up by NAME on every write, which for the
  point lights meant an snprintf plus a driver-side string hash, seven times
  per light per frame - forty-three lookups a frame for uniforms whose
  addresses had not changed since startup.

  They were not cached because a location belongs to one linked program, and
  shader hot reload relinks on every save: a stale location is silently ignored
  rather than reported, so the failure is "my edit did nothing" with no error
  anywhere.  Generation is the answer to that.  RendererUpdateShaders bumps a
  counter whenever ANY program relinks, and this refreshes itself when the
  counter it saw last no longer matches.

  Deliberately one counter for all programs rather than one each.  Relinks
  happen when a human saves a file, so re-looking-up the lit program because
  the lamp shader changed costs nothing anyone can measure, and one number is
  much harder to get wrong than three.
*/
struct point_light_locations
{
    int32 Position;
    int32 Constant;
    int32 Linear;
    int32 Quadratic;
    int32 Ambient;
    int32 Diffuse;
    int32 Specular;
};

struct lit_program_locations
{
    // 0 means "never looked up".  RendererUpdateShaders starts the renderer's
    // generation at 1, so the first frame always refreshes.
    uint32 Generation;

    int32 View;
    int32 Projection;

    int32 DirDirection;
    int32 DirAmbient;
    int32 DirDiffuse;
    int32 DirSpecular;

    int32 SpotAmbient;
    int32 SpotDiffuse;
    int32 SpotSpecular;
    int32 SpotConstant;
    int32 SpotLinear;
    int32 SpotQuadratic;
    int32 SpotCutOff;
    int32 SpotOuterCutOff;

    int32 MaterialDiffuse;
    int32 MaterialSpecular;
    int32 MaterialAlphaMask;

    point_light_locations PointLights[4];
};

// What one mesh is, in OpenGL terms.  Nothing outside this file knows these
// three numbers exist - a mesh_handle is an index into the table below.
struct opengl_mesh
{
    uint32 VAO;
    uint32 VBO;
    uint32 EBO;
};

// The 2D pass.  No EBO - the overlay writes six vertices per quad rather than
// four plus indices.
struct opengl_overlay_buffer
{
    uint32 VAO;
    uint32 VBO;
};

// Three uniforms, but cached on the same generation as the lit program: having
// one cached and the other not is more confusing than either choice alone.
struct overlay_program_locations
{
    uint32 Generation;
    int32 Projection;
    int32 Color;
    int32 Atlas;
};

#define RENDERER_MAX_MESHES           64
#define RENDERER_MAX_TEXTURES         256
#define RENDERER_MAX_OVERLAY_BUFFERS  4

struct renderer
{
    game_opengl_api *GL;

    // Indexed by render_program, NOT by the raw file table.  The mapping from
    // role to handle lives on this side on purpose: a GLuint in the command
    // stream would be OpenGL leaking into a file that is supposed to have none.
    uint32 Programs[RenderProgram_Count];
    shader_watch Watches[RenderProgram_Count];

    // Bumped whenever any program relinks.  Anything caching a uniform location
    // compares against this and refreshes when it differs - see
    // lit_program_locations.
    uint32 ShaderGeneration;
    lit_program_locations LitLocations;
    overlay_program_locations OverlayLocations;

    /*
      NOTE(yigit): The resource tables.  A mesh_handle or texture_handle is an
      index into one of these plus one, so zero means "nothing".

      Fixed-size and append-only.  There is no destroy, because nothing in this
      engine has ever wanted one - assets load once and live until the program
      exits.  Adding destruction means a free list and generation counters to
      catch a handle held past the death of what it named, and that is work
      with no caller.
    */
    opengl_mesh Meshes[RENDERER_MAX_MESHES];
    uint32 MeshCount;

    uint32 Textures[RENDERER_MAX_TEXTURES];
    uint32 TextureCount;

    opengl_overlay_buffer OverlayBuffers[RENDERER_MAX_OVERLAY_BUFFERS];
    uint32 OverlayBufferCount;

    // One white pixel, for materials that name no texture.
    texture_handle WhiteTexture;

    /*
      NOTE(yigit): The last camera command seen this frame.  Splitting camera
      and lighting into two commands means the lighting one no longer carries
      the view matrix it needs to convert world-space lights into the space the
      shaders light in - so the backend remembers it between the two.

      That is state carried ACROSS commands, which the push buffer otherwise
      avoids, and it is the price of the split.  It is also how every real
      renderer works: a command stream is ordered, and later commands read the
      state earlier ones set.

      Starts as identity rather than zeroed, so lighting pushed with no camera
      ahead of it lights in world space - visibly wrong, but still a picture.
      A zeroed matrix would collapse every light onto the origin instead, which
      looks like a shader bug.
    */
    mat4 CurrentView;
    mat4 CurrentProjection;
};

// Turning a handle back into the thing it names.  Both return a zeroed result
// for an invalid handle rather than asserting, so a model that failed to load
// draws nothing instead of taking the frame down.
internal opengl_mesh *
RendererGetMesh(renderer *Renderer, mesh_handle Handle)
{
    opengl_mesh *Result = 0;

    if(Handle.Value && (Handle.Value <= Renderer->MeshCount))
    {
        Result = Renderer->Meshes + (Handle.Value - 1);
    }

    return(Result);
}

internal opengl_overlay_buffer *
RendererGetOverlayBuffer(renderer *Renderer, overlay_buffer_handle Handle)
{
    opengl_overlay_buffer *Result = 0;

    if(Handle.Value && (Handle.Value <= Renderer->OverlayBufferCount))
    {
        Result = Renderer->OverlayBuffers + (Handle.Value - 1);
    }

    return(Result);
}

internal uint32
RendererGetTexture(renderer *Renderer, texture_handle Handle)
{
    uint32 Result = 0;

    if(Handle.Value && (Handle.Value <= Renderer->TextureCount))
    {
        Result = Renderer->Textures[Handle.Value - 1];
    }

    return(Result);
}

/*
  Hands a block of CPU pixels to the GPU and returns a handle to it.

  NOTE(yigit): The ONLY place a texture reaches OpenGL.  Its three callers - the
  image loader, the font baker and the white-pixel fallback - describe what they
  want and this decides how to express it, which is what keeps all three free of
  any graphics api.

  ChannelCount is the real one, not an assumption.  Sponza ships greyscale
  alpha masks at one byte per pixel; treating anything non-RGBA as three bytes
  reads three times the data that exists and walks off the end of the buffer.
*/
internal texture_handle
RendererUploadTexture(renderer *Renderer, uint8 *Pixels,
                      uint32 Width, uint32 Height, uint32 ChannelCount,
                      texture_wrap Wrap, texture_filter Filter)
{
    game_opengl_api *GL = Renderer->GL;
    texture_handle Result = {};

    if(Renderer->TextureCount >= RENDERER_MAX_TEXTURES)
    {
        return(Result);
    }

    uint32 Texture = 0;

    uint32 Format = GL_RGB;
    switch(ChannelCount)
    {
        case 1: { Format = GL_RED; } break;
        case 2: { Format = GL_RG; } break;
        case 3: { Format = GL_RGB; } break;
        case 4: { Format = GL_RGBA; } break;
    }

    uint32 GLWrap = (Wrap == TextureWrap_ClampToEdge) ? GL_CLAMP_TO_EDGE : GL_REPEAT;

    bool32 WantMipmaps = (Filter == TextureFilter_LinearMipmap);
    uint32 MinFilter = WantMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;

    GL->glGenTextures(1, &Texture);
    GL->glBindTexture(GL_TEXTURE_2D, Texture);

    // NOTE(yigit): Alignment 1 is required, not optional.  OpenGL assumes each
    // ROW of pixel data starts on a 4-byte boundary by default.  A greyscale
    // row is rarely a multiple of four bytes, so the default would make it skip
    // padding that is not there and shear the whole image diagonally.
    GL->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GLWrap);
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GLWrap);
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, MinFilter);
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GL->glTexImage2D(GL_TEXTURE_2D, 0, (int32)Format, (int32)Width, (int32)Height, 0,
                     Format, GL_UNSIGNED_BYTE, Pixels);

    if(WantMipmaps)
    {
        GL->glGenerateMipmap(GL_TEXTURE_2D);
    }

    // Plus one, so a zeroed handle reads as "nothing".
    Renderer->Textures[Renderer->TextureCount++] = Texture;
    Result.Value = Renderer->TextureCount;

    return(Result);
}

/*
  Hands a mesh to the GPU.  The caller supplies plain arrays and learns nothing
  about how they get there.

  NOTE(yigit): The attribute layout is fixed HERE rather than passed in, because
  obj_vertex IS the layout - position, normal, texcoord, 8 floats, 32 bytes.
  Describing it at the call site would let the two drift apart silently.

  A VAO is an OpenGL idea with no Vulkan equivalent; there, the same
  information is vertex input state baked into a pipeline object.  That is
  exactly why it is hidden behind this function: the caller asks for a mesh on
  the GPU and never learns which of those two it got.
*/
internal mesh_handle
RendererUploadMesh(renderer *Renderer,
                   obj_vertex *Vertices, uint32 VertexCount,
                   uint32 *Indices, uint32 IndexCount)
{
    game_opengl_api *GL = Renderer->GL;
    mesh_handle Result = {};

    if(Renderer->MeshCount >= RENDERER_MAX_MESHES)
    {
        return(Result);
    }

    opengl_mesh *Mesh = Renderer->Meshes + Renderer->MeshCount;

    GL->glGenVertexArrays(1, &Mesh->VAO);
    GL->glGenBuffers(1, &Mesh->VBO);
    GL->glGenBuffers(1, &Mesh->EBO);

    GL->glBindVertexArray(Mesh->VAO);

    GL->glBindBuffer(GL_ARRAY_BUFFER, Mesh->VBO);
    GL->glBufferData(GL_ARRAY_BUFFER, VertexCount * sizeof(obj_vertex),
                     Vertices, GL_STATIC_DRAW);

    // NOTE(yigit): Unlike GL_ARRAY_BUFFER, the ELEMENT buffer binding is stored
    // IN THE VAO.  So it has to be bound while the VAO is bound, and it must
    // not be unbound before the VAO is - unbind it first and the VAO forgets
    // its index buffer and the model draws nothing.
    GL->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, Mesh->EBO);
    GL->glBufferData(GL_ELEMENT_ARRAY_BUFFER, IndexCount * sizeof(uint32),
                     Indices, GL_STATIC_DRAW);

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

    // Plus one, so a zeroed handle reads as "nothing".
    ++Renderer->MeshCount;
    Result.Value = Renderer->MeshCount;

    return(Result);
}

// One white pixel, for materials that name no texture.  White times a colour
// is that colour, so the shader never has to ask whether a texture exists.
internal texture_handle
RendererCreateWhiteTexture(renderer *Renderer)
{
    uint8 White[4] = {255, 255, 255, 255};

    return(RendererUploadTexture(Renderer, White, 1, 1, 4,
                                 TextureWrap_Repeat, TextureFilter_Linear));
}


/*
  Allocates and sets up the backend.  Called once.

  NOTE(yigit): It digs game_opengl_api out of game_memory itself rather than
  being handed one.  That is the point: after this, no line in handmade.cpp
  names an OpenGL type.
*/
internal renderer *
RendererInitialize(game_memory *Memory, memory_arena *Arena)
{
    renderer *Result = PushStruct(Arena, renderer);
    *Result = {};

    Result->GL = &Memory->OpenGL;

    // NOTE(yigit): Required now that there is solid geometry.  Without it the
    // back faces draw over the front ones in whatever order they happen to be
    // listed, and objects look turned inside out.
    Result->GL->glEnable(GL_DEPTH_TEST);

    // NOTE(yigit): Starts at 1, not 0.  A cache that has never looked anything
    // up holds generation 0, so the first frame always refreshes.
    Result->ShaderGeneration = 1;

    // See the note on the fields - identity, not the zeros PushStruct leaves.
    Result->CurrentView = Mat4Identity();
    Result->CurrentProjection = Mat4Identity();

    Result->WhiteTexture = RendererCreateWhiteTexture(Result);

    return(Result);
}

/*
  Polls every shader file and rebuilds any program whose sources changed.

  This also performs the very first load: a renderer starts zeroed, so the
  stored write times are 0, which never matches a real file and triggers a
  build on frame one.  One code path, no special case for startup.
*/
internal void
RendererUpdateShaders(renderer *Renderer, thread_context *Thread, game_memory *Memory)
{
    // The file table and the program array are indexed together, so they have
    // to be the same length - and that length is RenderProgram_Count, which is
    // what ties a file to the role it fills.
    Assert(ArrayCount(GlobalShaderFiles) == RenderProgram_Count);

    game_opengl_api *GL = Renderer->GL;

    for(uint32 Index = 0;
        Index < RenderProgram_Count;
        ++Index)
    {
        shader_source_files Files = GlobalShaderFiles[Index];
        shader_watch *Watch = Renderer->Watches + Index;

        uint64 VertWriteTime = Memory->DEBUGPlatformGetFileWriteTime(Thread, Files.VertFileName);
        uint64 FragWriteTime = Memory->DEBUGPlatformGetFileWriteTime(Thread, Files.FragFileName);

        if((VertWriteTime == Watch->VertWriteTime) &&
           (FragWriteTime == Watch->FragWriteTime))
        {
            continue;
        }

        // NOTE(yigit): Record the new times even when the build fails, so a
        // broken shader is reported once instead of every single frame.
        // Saving the file again moves the timestamp and we try once more.
        Watch->VertWriteTime = VertWriteTime;
        Watch->FragWriteTime = FragWriteTime;

        uint32 NewProgram = GameBuildShaderProgram(Thread, Memory, GL,
                                                   Files.VertFileName, Files.FragFileName);
        if(NewProgram)
        {
            // NOTE(yigit): Safe on the first load - glDeleteProgram ignores 0.
            GL->glDeleteProgram(Renderer->Programs[Index]);
            Renderer->Programs[Index] = NewProgram;

            // NOTE(yigit): Every cached uniform location in the renderer names
            // a program object that has just been deleted.  Bumping this is
            // what makes them refresh - without it a shader edit would appear
            // to do nothing, because writes to a stale location are ignored
            // silently rather than reported.
            ++Renderer->ShaderGeneration;

            Memory->DEBUGPlatformLog(Thread, "SHADER RELOADED: ");
            Memory->DEBUGPlatformLog(Thread, Files.FragFileName);
            Memory->DEBUGPlatformLog(Thread, "\n");
        }
        else
        {
            Memory->DEBUGPlatformLog(Thread, "SHADER RELOAD FAILED, keeping previous program: ");
            Memory->DEBUGPlatformLog(Thread, Files.FragFileName);
            Memory->DEBUGPlatformLog(Thread, "\n");

            // NOTE(yigit): Nothing to fall back on means this was the first
            // load, so the data files are genuinely missing or broken.
            Assert(Renderer->Programs[Index]);
        }
    }
}


// Draws one loaded model, a submesh at a time, setting that submesh's material
// before each call.
//
// NOTE(yigit): One glDrawElements per material rather than one per model.  A
// material change is a uniform change, and uniforms cannot vary within a draw -
// which is the whole reason the index buffer was reordered by material.
internal void
GameDrawModel(renderer *Renderer, uint32 Program, render_model *Model,
              mat4 ModelMatrix)
{
    game_opengl_api *GL = Renderer->GL;

    // NOTE(yigit): The handle is resolved HERE, at the last possible moment.
    // Everything upstream - the loaders, the command buffer, game_state - only
    // ever moved an opaque number around.
    opengl_mesh *Mesh = RendererGetMesh(Renderer, Model->Mesh);
    if(!Mesh || !Model->IndexCount)
    {
        return;
    }

    uint32 WhiteTexture = RendererGetTexture(Renderer, Renderer->WhiteTexture);

    // The At-setters below write to whatever program is bound, so this has to
    // happen before any of them - and it replaces the glUseProgram the
    // name-based setters used to do on every single write.
    GL->glUseProgram(Program);
    GL->glBindVertexArray(Mesh->VAO);

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
        uint32 DiffuseTexture = RendererGetTexture(Renderer, Submesh->DiffuseTexture);
        if(!DiffuseTexture) { DiffuseTexture = WhiteTexture; }

        uint32 AlphaTexture = RendererGetTexture(Renderer, Submesh->AlphaTexture);
        if(!AlphaTexture) { AlphaTexture = WhiteTexture; }

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

/*
  Looks up every lit-program uniform location, but only when the shader
  generation has moved since the last time.

  NOTE(yigit): This is the ONLY place the "pointLights[2].quadratic" names are
  built, and it runs once per relink rather than once per frame.  Everything
  downstream writes to an int32 it already has.
*/
internal void
RefreshLitLocations(renderer *Renderer)
{
    lit_program_locations *L = &Renderer->LitLocations;

    if(L->Generation == Renderer->ShaderGeneration)
    {
        return;
    }

    game_opengl_api *GL = Renderer->GL;
    uint32 Program = Renderer->Programs[RenderProgram_Lit];

    L->View       = GetUniformLocation(GL, Program, "view");
    L->Projection = GetUniformLocation(GL, Program, "projection");

    L->DirDirection = GetUniformLocation(GL, Program, "dirLight.direction");
    L->DirAmbient   = GetUniformLocation(GL, Program, "dirLight.ambient");
    L->DirDiffuse   = GetUniformLocation(GL, Program, "dirLight.diffuse");
    L->DirSpecular  = GetUniformLocation(GL, Program, "dirLight.specular");

    L->SpotAmbient      = GetUniformLocation(GL, Program, "spotLight.ambient");
    L->SpotDiffuse      = GetUniformLocation(GL, Program, "spotLight.diffuse");
    L->SpotSpecular     = GetUniformLocation(GL, Program, "spotLight.specular");
    L->SpotConstant     = GetUniformLocation(GL, Program, "spotLight.constant");
    L->SpotLinear       = GetUniformLocation(GL, Program, "spotLight.linear");
    L->SpotQuadratic    = GetUniformLocation(GL, Program, "spotLight.quadratic");
    L->SpotCutOff       = GetUniformLocation(GL, Program, "spotLight.cutOff");
    L->SpotOuterCutOff  = GetUniformLocation(GL, Program, "spotLight.outerCutOff");

    L->MaterialDiffuse   = GetUniformLocation(GL, Program, "material.diffuse");
    L->MaterialSpecular  = GetUniformLocation(GL, Program, "material.specular");
    L->MaterialAlphaMask = GetUniformLocation(GL, Program, "material.alphaMask");

    // The array names, built once and then never again.  A local buffer is safe
    // because glGetUniformLocation copies the name it is handed.
    for(uint32 Index = 0; Index < ArrayCount(L->PointLights); ++Index)
    {
        point_light_locations *P = L->PointLights + Index;
        char Name[64];

        snprintf(Name, sizeof(Name), "pointLights[%u].position", Index);
        P->Position = GetUniformLocation(GL, Program, Name);
        snprintf(Name, sizeof(Name), "pointLights[%u].constant", Index);
        P->Constant = GetUniformLocation(GL, Program, Name);
        snprintf(Name, sizeof(Name), "pointLights[%u].linear", Index);
        P->Linear = GetUniformLocation(GL, Program, Name);
        snprintf(Name, sizeof(Name), "pointLights[%u].quadratic", Index);
        P->Quadratic = GetUniformLocation(GL, Program, Name);
        snprintf(Name, sizeof(Name), "pointLights[%u].ambient", Index);
        P->Ambient = GetUniformLocation(GL, Program, Name);
        snprintf(Name, sizeof(Name), "pointLights[%u].diffuse", Index);
        P->Diffuse = GetUniformLocation(GL, Program, Name);
        snprintf(Name, sizeof(Name), "pointLights[%u].specular", Index);
        P->Specular = GetUniformLocation(GL, Program, Name);
    }

    L->Generation = Renderer->ShaderGeneration;
}

/*
  Uploads one Camera command, and remembers it for the lighting command that
  follows.

  NOTE(yigit): View and projection go to EVERY 3D program, not just the lit one.
  Uniforms belong to a program rather than to the context, so the ones set on
  the lit program simply do not exist in the lamp one - setting them once and
  expecting both to see it is the classic way to end up with lamp markers that
  never move.
*/
internal void
SetCameraUniforms(renderer *Renderer, render_command_camera *Camera)
{
    RefreshLitLocations(Renderer);

    game_opengl_api *GL = Renderer->GL;
    lit_program_locations *L = &Renderer->LitLocations;

    Renderer->CurrentView = Camera->View;
    Renderer->CurrentProjection = Camera->Projection;

    // NOTE(yigit): The At-setters write to whatever program is BOUND, not to
    // whichever one the location came from.  Nothing warns when those differ,
    // so the bind has to happen here and not be assumed.
    GL->glUseProgram(Renderer->Programs[RenderProgram_Lit]);

    SetUniformMat4At(GL, L->View, Camera->View);
    SetUniformMat4At(GL, L->Projection, Camera->Projection);

    // By NAME rather than by cached location, and it rebinds the program on its
    // own - so this has to come after the At-setters above, not before.
    uint32 Lamp = Renderer->Programs[RenderProgram_Lamp];
    SetUniformMat4(GL, Lamp, "view", Camera->View);
    SetUniformMat4(GL, Lamp, "projection", Camera->Projection);
}

/*
  Uploads one Lighting command to the lit program.

  Every value arrives in the command rather than from a global in this file:
  the scene describes its lights, and this only decides how to express them.

  The world-to-view conversion stays here on purpose.  Which space this
  renderer lights in is a rendering decision, not something the scene should
  have to know about - and the view matrix it needs comes from whichever Camera
  command ran before this one, not from the command itself.
*/
internal void
SetLitUniforms(renderer *Renderer, render_command_lighting *Lighting)
{
    RefreshLitLocations(Renderer);

    game_opengl_api *GL = Renderer->GL;
    lit_program_locations *L = &Renderer->LitLocations;

    mat4 View = Renderer->CurrentView;

    // See the note in SetCameraUniforms - the At-setters need the right program
    // bound, and the lamp setters at the bottom of the camera path left the
    // LAMP program bound.
    GL->glUseProgram(Renderer->Programs[RenderProgram_Lit]);

    // The SPOTLIGHT - the flashlight held at the camera.  It needs no position
    // or direction uniform: in view space the camera is the origin looking down
    // -Z, so the shader has both as constants.
    SetUniformVec3At(GL, L->SpotAmbient,  Lighting->SpotAmbient);
    SetUniformVec3At(GL, L->SpotDiffuse,  Lighting->SpotDiffuse);
    SetUniformVec3At(GL, L->SpotSpecular, Lighting->SpotSpecular);

    SetUniformFloatAt(GL, L->SpotConstant,  Lighting->SpotConstant);
    SetUniformFloatAt(GL, L->SpotLinear,    Lighting->SpotLinear);
    SetUniformFloatAt(GL, L->SpotQuadratic, Lighting->SpotQuadratic);

    // Already cosines by the time they arrive - the scene converts from degrees
    // once, rather than this doing it every frame.
    SetUniformFloatAt(GL, L->SpotCutOff,      Lighting->SpotCutOff);
    SetUniformFloatAt(GL, L->SpotOuterCutOff, Lighting->SpotOuterCutOff);

    // NOTE(yigit): W is 0, not 1.  A direction has no location, so the view
    // matrix's translation column must not touch it.
    vec4 DirView = View * Vec4(Lighting->DirLightDirectionWorld, 0.0f);
    SetUniformVec3At(GL, L->DirDirection, Vec3(DirView.X, DirView.Y, DirView.Z));

    SetUniformVec3At(GL, L->DirAmbient,  Lighting->DirAmbient);
    SetUniformVec3At(GL, L->DirDiffuse,  Lighting->DirDiffuse);
    SetUniformVec3At(GL, L->DirSpecular, Lighting->DirSpecular);

    // NOTE(yigit): W is 1 here, not 0.  A position DOES get slid by the view
    // matrix's translation - that is the entire difference from the direction
    // above, and getting it backwards is the classic way to end up with lights
    // that drift as the camera moves.
    uint32 LightCount = Lighting->PointLightCount;
    if(LightCount > ArrayCount(L->PointLights))
    {
        LightCount = ArrayCount(L->PointLights);
    }

    for(uint32 Index = 0; Index < LightCount; ++Index)
    {
        render_point_light *Light = Lighting->PointLights + Index;
        point_light_locations *P = L->PointLights + Index;

        vec4 PosView = View * Vec4(Light->PositionWorld, 1.0f);

        SetUniformVec3At(GL, P->Position, Vec3(PosView.X, PosView.Y, PosView.Z));

        SetUniformFloatAt(GL, P->Constant,  Light->Constant);
        SetUniformFloatAt(GL, P->Linear,    Light->Linear);
        SetUniformFloatAt(GL, P->Quadratic, Light->Quadratic);

        SetUniformVec3At(GL, P->Ambient,  Light->Ambient);
        SetUniformVec3At(GL, P->Diffuse,  Light->Diffuse);
        SetUniformVec3At(GL, P->Specular, Light->Specular);
    }

    // Which texture unit each sampler reads from.  Constant, but a relink
    // resets every uniform in the program, so they go up every frame with the
    // rest.
    SetUniformIntAt(GL, L->MaterialDiffuse, 0);
    SetUniformIntAt(GL, L->MaterialSpecular, 1);
    SetUniformIntAt(GL, L->MaterialAlphaMask, 2);
}

// NOTE(yigit): Only the colour.  View and projection are the camera's, and go
// up in SetCameraUniforms along with the lit program's copies.
internal void
SetLampUniforms(game_opengl_api *GL, uint32 Program, render_command_lighting *Lighting)
{
    // Book ch. 14.4 exercise 1 - the marker takes the light's own colour, so
    // the lamp visibly matches what it is casting.
    SetUniformVec4(GL, Program, "LightColor", Lighting->SpotSpecular, 1.0f);
}

/*
  Creates the GPU-side buffer an overlay draws from.  The CPU-side array is the
  caller's, handed out by OverlayAllocate.

  NOTE(yigit): Allocated at full size ONCE with a null pointer, so the driver
  reserves the storage now and the per-frame upload only ever writes into it.
  GL_DYNAMIC_DRAW is the hint that this will be rewritten often - calling
  glBufferData every frame instead would ask the driver to reallocate every
  frame, which is the classic way to make a dynamic buffer slow.
*/
internal overlay_buffer_handle
RendererCreateOverlayBuffer(renderer *Renderer)
{
    game_opengl_api *GL = Renderer->GL;
    overlay_buffer_handle Result = {};

    Assert(Renderer->OverlayBufferCount < RENDERER_MAX_OVERLAY_BUFFERS);
    if(Renderer->OverlayBufferCount >= RENDERER_MAX_OVERLAY_BUFFERS)
    {
        return(Result);
    }

    opengl_overlay_buffer *Buffer = Renderer->OverlayBuffers + Renderer->OverlayBufferCount;

    GL->glGenVertexArrays(1, &Buffer->VAO);
    GL->glGenBuffers(1, &Buffer->VBO);

    GL->glBindVertexArray(Buffer->VAO);
    GL->glBindBuffer(GL_ARRAY_BUFFER, Buffer->VBO);

    GL->glBufferData(GL_ARRAY_BUFFER, OVERLAY_MAX_VERTICES * sizeof(overlay_vertex),
                     0, GL_DYNAMIC_DRAW);

    // Two attributes, both vec2: position in pixels, then texture coordinate.
    GL->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(overlay_vertex),
                              (void *)0);
    GL->glEnableVertexAttribArray(0);

    GL->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(overlay_vertex),
                              (void *)(2 * sizeof(real32)));
    GL->glEnableVertexAttribArray(1);

    GL->glBindVertexArray(0);

    ++Renderer->OverlayBufferCount;
    Result.Value = Renderer->OverlayBufferCount;

    return(Result);
}

// Looks up the overlay program's uniform locations, once per relink.  Same
// generation trick as RefreshLitLocations - three lookups rather than
// forty-three, but caching only some of them would be the confusing option.
internal void
RefreshOverlayLocations(renderer *Renderer)
{
    overlay_program_locations *L = &Renderer->OverlayLocations;

    if(L->Generation == Renderer->ShaderGeneration)
    {
        return;
    }

    game_opengl_api *GL = Renderer->GL;
    uint32 Program = Renderer->Programs[RenderProgram_Overlay];

    L->Projection = GetUniformLocation(GL, Program, "projection");
    L->Color      = GetUniformLocation(GL, Program, "color");
    L->Atlas      = GetUniformLocation(GL, Program, "atlas");

    L->Generation = Renderer->ShaderGeneration;
}

/*
  Uploads whatever the overlay accumulated this frame and draws it in one call.

  NOTE(yigit): The blend and depth state is set and restored here by hand,
  because there is no render state system yet - next frame's 3D pass would
  otherwise inherit blending and a disabled depth test and quietly break.
  Which state a 2D pass needs is an api question, which is why this lives on
  this side of the seam rather than in handmade_overlay.h.
*/
internal void
RendererDrawOverlay(renderer *Renderer, overlay *Overlay, mat4 Projection,
                    texture_handle TextureHandle, vec3 Color)
{
    game_opengl_api *GL = Renderer->GL;

    opengl_overlay_buffer *Buffer = RendererGetOverlayBuffer(Renderer, Overlay->Buffer);
    if(!Buffer || !Overlay->VertexCount)
    {
        return;
    }

    RefreshOverlayLocations(Renderer);
    overlay_program_locations *L = &Renderer->OverlayLocations;

    GL->glBindBuffer(GL_ARRAY_BUFFER, Buffer->VBO);
    GL->glBufferSubData(GL_ARRAY_BUFFER, 0,
                        Overlay->VertexCount * sizeof(overlay_vertex),
                        Overlay->Vertices);

    GL->glUseProgram(Renderer->Programs[RenderProgram_Overlay]);

    SetUniformMat4At(GL, L->Projection, Projection);
    SetUniformVec3At(GL, L->Color, Color);
    SetUniformIntAt(GL, L->Atlas, 0);

    GL->glActiveTexture(GL_TEXTURE0);
    GL->glBindTexture(GL_TEXTURE_2D, RendererGetTexture(Renderer, TextureHandle));

    // Standard "over": the incoming fragment contributes its own alpha, and
    // what is already on screen contributes the rest.  Needed rather than
    // discard because a glyph edge is PARTIALLY covered, which discard cannot
    // express.
    GL->glEnable(GL_BLEND);
    GL->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // NOTE(yigit): The depth TEST is off so the overlay always wins, and the
    // depth WRITE is off so it does not leave a mark that occludes the scene on
    // the next frame.  Turning off only the test would still write.
    GL->glDisable(GL_DEPTH_TEST);
    GL->glDepthMask(GL_FALSE);

    GL->glBindVertexArray(Buffer->VAO);
    GL->glDrawArrays(GL_TRIANGLES, 0, (int32)Overlay->VertexCount);
    GL->glBindVertexArray(0);

    GL->glDepthMask(GL_TRUE);
    GL->glEnable(GL_DEPTH_TEST);
    GL->glDisable(GL_BLEND);
}

/*
  Walks the command buffer and executes it.  The entire backend is this one
  function plus the helpers above.

  NOTE(yigit): The walk steps by Header->Size, not by the sizeof of whatever
  type the switch matched.  That is what lets a command this backend does not
  recognise be skipped rather than desynchronising the whole stream.
*/
internal void
RenderBufferExecute(renderer *Renderer, render_buffer *Buffer)
{
    game_opengl_api *GL = Renderer->GL;

    uint8 *At = Buffer->Base;
    uint8 *End = Buffer->Base + Buffer->Used;

    while(At < End)
    {
        render_command_header *Header = (render_command_header *)At;

        switch(Header->Type)
        {
            case RenderCommand_Clear:
            {
                render_command_clear *Command = (render_command_clear *)Header;
                GL->glClearColor(Command->Color.X, Command->Color.Y,
                                 Command->Color.Z, Command->Color.W);
                GL->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            } break;
            case RenderCommand_Camera:
            {
                render_command_camera *Command = (render_command_camera *)Header;

                // NOTE(yigit): Every program is told here and now, and none
                // can be told lazily at the draw that needs it - uniforms
                // belong to a program, not to the context, so there is nowhere
                // to stash this until later.
                SetCameraUniforms(Renderer, Command);
            } break;

            case RenderCommand_Lighting:
            {
                render_command_lighting *Command = (render_command_lighting *)Header;

                SetLitUniforms(Renderer, Command);
                SetLampUniforms(GL, Renderer->Programs[RenderProgram_Lamp], Command);
            } break;

            case RenderCommand_DrawModel:
            {
                render_command_draw_model *Command = (render_command_draw_model *)Header;

                uint32 Program = Renderer->Programs[Command->Program];
                GameDrawModel(Renderer, Program, Command->Model, Command->Transform);
            } break;

            case RenderCommand_DrawOverlay:
            {
                render_command_draw_overlay *Command = (render_command_draw_overlay *)Header;

                RendererDrawOverlay(Renderer, Command->Overlay, Command->Projection,
                                    Command->Font ? Command->Font->Texture
                                                  : Renderer->WhiteTexture,
                                    Command->Color);
            } break;

            // NOTE(yigit): A command nothing handles is a bug, not something to
            // walk past quietly.  Header->Size means the stream stays readable
            // either way, which is exactly why the failure would otherwise be
            // invisible.

            InvalidDefaultCase;

        }
        At += Header->Size;
    }

    GL->glBindVertexArray(0);
}

#define HANDMADE_RENDER_OPENGL_H
#endif
