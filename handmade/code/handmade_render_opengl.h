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

  It owns the shader programs, which is why the hot-reload loop moved here out
  of handmade_shader.h.  A program handle is a backend resource; game_state has
  no business holding one.
*/
// What one mesh is, in OpenGL terms.  Nothing outside this file knows these
// three numbers exist - a mesh_handle is an index into the table below.
struct opengl_mesh
{
    uint32 VAO;
    uint32 VBO;
    uint32 EBO;
};

#define RENDERER_MAX_MESHES   64
#define RENDERER_MAX_TEXTURES 256

struct renderer
{
    game_opengl_api *GL;

    // Indexed by render_program, NOT by the raw file table.  The mapping from
    // role to handle lives on this side on purpose: a GLuint in the command
    // stream would be OpenGL leaking into a file that is supposed to have none.
    uint32 Programs[RenderProgram_Count];
    shader_watch Watches[RenderProgram_Count];

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

    // One white pixel, for materials that name no texture.
    texture_handle WhiteTexture;
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

  NOTE(yigit): This is the ONLY place a texture reaches OpenGL.  Three callers
  used to repeat these eight calls between them - the image loader, the font
  baker, and the white-pixel fallback - each with its own slightly different
  set of parameters.  Pulling them together is the third-use rule firing, and
  it is also what lets those three become api-agnostic: they now describe what
  they want and this decides how to express it.

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
    // listed, and objects look turned inside out.  This used to be the last
    // glEnable in the game layer.
    Result->GL->glEnable(GL_DEPTH_TEST);

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

// Book ch. 17.3 - the uniform names here are "pointLights[2].quadratic" and
// friends.  The book builds them with std::string concatenation; with no
// std::string in this layer they are formatted into a scratch buffer instead.
//
// NOTE(yigit): A local buffer is safe because glGetUniformLocation copies the
// name it is handed - nothing keeps a pointer to it after the call returns.
internal void
SetPointLightUniforms(game_opengl_api *GL, uint32 Program, uint32 Index,
                      render_point_light *Light, vec3 PositionView)
{
    char Name[64];

    snprintf(Name, sizeof(Name), "pointLights[%u].position", Index);
    SetUniformVec3(GL, Program, Name, PositionView);

    snprintf(Name, sizeof(Name), "pointLights[%u].constant", Index);
    SetUniformFloat(GL, Program, Name, Light->Constant);
    snprintf(Name, sizeof(Name), "pointLights[%u].linear", Index);
    SetUniformFloat(GL, Program, Name, Light->Linear);
    snprintf(Name, sizeof(Name), "pointLights[%u].quadratic", Index);
    SetUniformFloat(GL, Program, Name, Light->Quadratic);

    snprintf(Name, sizeof(Name), "pointLights[%u].ambient", Index);
    SetUniformVec3(GL, Program, Name, Light->Ambient);
    snprintf(Name, sizeof(Name), "pointLights[%u].diffuse", Index);
    SetUniformVec3(GL, Program, Name, Light->Diffuse);
    snprintf(Name, sizeof(Name), "pointLights[%u].specular", Index);
    SetUniformVec3(GL, Program, Name, Light->Specular);
}

/*
  Uploads one Setup command to the lit program.

  NOTE(yigit): Every value now arrives in the command rather than from a global
  in this file.  That is the actual change the push buffer makes - the scene
  describes its lights, and the backend only decides how to express them.

  The world-to-view conversion stays here on purpose: which space this renderer
  lights in is a rendering decision, not something the scene should have to
  know about.
*/
internal void
SetLitUniforms(game_opengl_api *GL, uint32 Program, render_command_setup *Setup)
{
    SetUniformMat4(GL, Program, "view", Setup->View);
    SetUniformMat4(GL, Program, "projection", Setup->Projection);

    // NOTE(yigit): material.shininess, diffuseColor and specularColor are NOT
    // set here - they vary per submesh, so GameDrawModel sets them immediately
    // before each draw.

    // The SPOTLIGHT - the flashlight held at the camera.  It needs no position
    // or direction uniform: in view space the camera is the origin looking down
    // -Z, so the shader has both as constants.
    SetUniformVec3(GL, Program, "spotLight.ambient",  Setup->SpotAmbient);
    SetUniformVec3(GL, Program, "spotLight.diffuse",  Setup->SpotDiffuse);
    SetUniformVec3(GL, Program, "spotLight.specular", Setup->SpotSpecular);

    SetUniformFloat(GL, Program, "spotLight.constant",  Setup->SpotConstant);
    SetUniformFloat(GL, Program, "spotLight.linear",    Setup->SpotLinear);
    SetUniformFloat(GL, Program, "spotLight.quadratic", Setup->SpotQuadratic);

    // Already cosines by the time they arrive - the scene converts from degrees
    // once, rather than this doing it every frame.
    SetUniformFloat(GL, Program, "spotLight.cutOff",      Setup->SpotCutOff);
    SetUniformFloat(GL, Program, "spotLight.outerCutOff", Setup->SpotOuterCutOff);

    // Book ch. 17.1 - the directional light.
    //
    // NOTE(yigit): W is 0, not 1.  A direction has no location, so the view
    // matrix's translation column must not touch it.
    vec4 DirView = Setup->View * Vec4(Setup->DirLightDirectionWorld, 0.0f);
    SetUniformVec3(GL, Program, "dirLight.direction",
                   Vec3(DirView.X, DirView.Y, DirView.Z));

    SetUniformVec3(GL, Program, "dirLight.ambient",  Setup->DirAmbient);
    SetUniformVec3(GL, Program, "dirLight.diffuse",  Setup->DirDiffuse);
    SetUniformVec3(GL, Program, "dirLight.specular", Setup->DirSpecular);

    // Book ch. 17.2 - the point lights.
    //
    // NOTE(yigit): W is 1 here, not 0.  A position DOES get slid by the view
    // matrix's translation - that is the entire difference from the direction
    // above, and getting it backwards is the classic way to end up with lights
    // that drift as the camera moves.
    for(uint32 LightIndex = 0;
        LightIndex < Setup->PointLightCount;
        ++LightIndex)
    {
        render_point_light *Light = Setup->PointLights + LightIndex;
        vec4 PosView = Setup->View * Vec4(Light->PositionWorld, 1.0f);

        SetPointLightUniforms(GL, Program, LightIndex, Light,
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
SetLampUniforms(game_opengl_api *GL, uint32 Program, render_command_setup *Setup)
{
    SetUniformMat4(GL, Program, "view", Setup->View);
    SetUniformMat4(GL, Program, "projection", Setup->Projection);

    // Book ch. 14.4 exercise 1 - the marker takes the light's own colour, so
    // the lamp visibly matches what it is casting.
    SetUniformVec4(GL, Program, "LightColor", Setup->SpotSpecular, 1.0f);
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

    // Uniforms shared by a whole frame arrive in a Setup command, and the draws
    // that follow need them.  Remembered rather than re-read, so a draw never
    // has to search backwards through the buffer.
    render_command_setup *Setup = 0;

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

            case RenderCommand_Setup:
            {
                Setup = (render_command_setup *)Header;

                // Both programs are told, and neither can be told lazily -
                // uniforms belong to a program, not to the context.
                SetLitUniforms(GL, Renderer->Programs[RenderProgram_Lit], Setup);
                SetLampUniforms(GL, Renderer->Programs[RenderProgram_Lamp], Setup);
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

                // NOTE(yigit): OverlayFlush owns the blend and depth state it
                // needs, and restores it afterwards.  That bookkeeping used to
                // sit in the game layer; it belongs on this side of the seam,
                // because which state a 2D pass requires is an api question.
                OverlayFlush(Command->Overlay, GL,
                             Renderer->Programs[RenderProgram_Overlay],
                             Command->Projection,
                             RendererGetTexture(Renderer,
                                                Command->Font ? Command->Font->Texture
                                                              : Renderer->WhiteTexture),
                             Command->Color);
            } break;
        }

        At += Header->Size;
    }

    GL->glBindVertexArray(0);
}

#define HANDMADE_RENDER_OPENGL_H
#endif
