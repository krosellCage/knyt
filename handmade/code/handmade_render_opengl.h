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

    int32 PointLightCount;

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

// How many times bloom halves the image.  Six takes 1920x1080 down to 30x16,
// about as small as is still useful.  More levels = a wider glow.
#define BLOOM_MIP_COUNT               6
#define RENDERER_MAX_OVERLAY_BUFFERS  4

// The shadow map's size, in pixels, on each side.  Bigger = sharper shadow
// edges, at the cost of memory and fill time: 2048x2048 of 24-bit depth is
// 12 MB.  It is fixed, not the window's size - it covers the SCENE, from the
// sun, and has nothing to do with the window.
#define SHADOW_MAP_SIZE               2048

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

    // The HDR target.  The 3D scene draws into this instead of the window.
    uint32 HDRFramebuffer;      // the container
    uint32 HDRColor;            // a texture: where the colours go
    uint32 HDRDepthStencil;     // a renderbuffer: where depth + stencil go
    int32 HDRWidth;             // the size they were created at, so we can
    int32 HDRHeight;            // tell when the window has been resized

    // Bound for any fullscreen triangle.  It describes no vertex data at all -
    // the vertex shader builds the corners from gl_VertexID - but core profile
    // refuses to draw with no VAO bound.
    uint32 FullscreenVAO;

    // The bloom chain: the HDR image at half size, quarter size, and so on.
    // One framebuffer, reused for every level - each pass attaches whichever
    // texture it wants to draw into.
    uint32 BloomFramebuffer;
    uint32 BloomMips[BLOOM_MIP_COUNT];
    int32 BloomMipWidth[BLOOM_MIP_COUNT];
    int32 BloomMipHeight[BLOOM_MIP_COUNT];

    // The sun's view of the scene: depth only.  Drawn first every frame.
    uint32 ShadowFramebuffer;
    uint32 ShadowDepth;             // depth texture - the shadow map itself

    // The sun camera's view and projection multiplied together.  The shadow
    // pass draws WITH it; the lit shader later uses the same matrix to find
    // where each pixel lands in the shadow map.  Kept here, like CurrentView,
    // because the command that sets it and the one that uses it are different.
    mat4 LightSpace;
};

/*
  Creates the HDR target at this size, or does nothing if it is already
  that size.  Called every frame; only does work on the first frame and
  when the window is resized.
*/
internal void
RendererResizeHDRTarget(renderer *Renderer, int32 Width, int32 Height)
{
    // A minimised window reports 0x0.  A 0x0 texture is an error.
    if((Width <= 0) || (Height <= 0))
    {
        return;
    }

    if((Width == Renderer->HDRWidth) && (Height == Renderer->HDRHeight))
    {
        return;
    }

    game_opengl_api *GL = Renderer->GL;

    // Throw away the old ones.  All three ignore 0, so this is safe on the
    // very first call when nothing exists yet.
    GL->glDeleteFramebuffers(1, &Renderer->HDRFramebuffer);
    GL->glDeleteTextures(1, &Renderer->HDRColor);
    GL->glDeleteRenderbuffers(1, &Renderer->HDRDepthStencil);

    // 1. The colour texture.
    GL->glGenTextures(1, &Renderer->HDRColor);
    GL->glBindTexture(GL_TEXTURE_2D, Renderer->HDRColor);

    // Last argument 0 = no pixel data, just reserve the memory.  The GPU will
    // fill it when we draw.
    GL->glTexImage2D(GL_TEXTURE_2D, 0, (int32)GL_RGBA16F, Width, Height, 0,
                     GL_RGBA, GL_FLOAT, 0);

    // NOTE: MIN_FILTER must be set.  The default expects mipmaps, this texture
    // has none, and a texture missing mipmaps it was told to expect reads as
    // pure black - with no error anywhere.
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 2. The depth + stencil renderbuffer.  Same 24/8 split the window has.
    GL->glGenRenderbuffers(1, &Renderer->HDRDepthStencil);
    GL->glBindRenderbuffer(GL_RENDERBUFFER, Renderer->HDRDepthStencil);
    GL->glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, Width, Height);

    // 3. The box, with both attached.
    GL->glGenFramebuffers(1, &Renderer->HDRFramebuffer);
    GL->glBindFramebuffer(GL_FRAMEBUFFER, Renderer->HDRFramebuffer);
    GL->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, Renderer->HDRColor, 0);
    GL->glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
                                  GL_RENDERBUFFER, Renderer->HDRDepthStencil);

    // Anything but COMPLETE means every draw into it silently does nothing.
    // If this fires, look at Status in the debugger and compare it against
    // the GL_FRAMEBUFFER_INCOMPLETE_* codes in handmade_opengl.h.
    uint32 Status = GL->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    Assert(Status == GL_FRAMEBUFFER_COMPLETE);

    // 4. The bloom chain.  Each level half the size of the one before.
    //    Remade on every resize, like the HDR texture it is shrunk from.
    GL->glDeleteTextures(BLOOM_MIP_COUNT, Renderer->BloomMips);

    int32 MipWidth = Width;
    int32 MipHeight = Height;
    for(uint32 Mip = 0;
        Mip < BLOOM_MIP_COUNT;
        ++Mip)
    {
        // Never below 1x1, or a tiny window would ask for a 0-pixel texture.
        MipWidth  = (MipWidth  > 1) ? (MipWidth  / 2) : 1;
        MipHeight = (MipHeight > 1) ? (MipHeight / 2) : 1;

        Renderer->BloomMipWidth[Mip]  = MipWidth;
        Renderer->BloomMipHeight[Mip] = MipHeight;

        GL->glGenTextures(1, &Renderer->BloomMips[Mip]);
        GL->glBindTexture(GL_TEXTURE_2D, Renderer->BloomMips[Mip]);

        // Float, same as the HDR texture: the whole point is to keep the
        // difference between "bright" and "twenty times brighter".
        GL->glTexImage2D(GL_TEXTURE_2D, 0, (int32)GL_RGBA16F, MipWidth, MipHeight, 0,
                         GL_RGBA, GL_FLOAT, 0);

        // NOTE: LINEAR is not optional.  The bloom shaders read BETWEEN
        // pixels on purpose, and linear filtering averages the four around
        // each read for free - that is half of the blur.
        GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    // Made once and never resized - it owns nothing, only whatever texture
    // is attached to it at the moment.
    if(!Renderer->BloomFramebuffer)
    {
        GL->glGenFramebuffers(1, &Renderer->BloomFramebuffer);
    }

    Renderer->HDRWidth = Width;
    Renderer->HDRHeight = Height;
}

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
                      texture_wrap Wrap, texture_filter Filter,
                      texture_encoding Encoding)
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
    // NOTE: TWO formats now, and until today they happened to be the same.
    //   Format         - what the bytes in Pixels ARE (GL_RGB: 3 bytes each).
    //   InternalFormat - what the GPU should STORE them as.
    // An sRGB texture's bytes are identical; only the storage format tells
    // the GPU to decode on every read.
    uint32 InternalFormat = Format;
    if(Encoding == TextureEncoding_SRGB)
    {
        if(ChannelCount == 3) { InternalFormat = GL_SRGB8; }
        if(ChannelCount == 4) { InternalFormat = GL_SRGB8_ALPHA8; }
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

    GL->glTexImage2D(GL_TEXTURE_2D, 0, (int32)InternalFormat, (int32)Width, (int32)Height, 0,
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
  Six square faces become one cube map.  Faces[] is in OpenGL's face order,
  +X -X +Y -Y +Z -Z, each tightly packed and top row first.

  NOTE(yigit): Top row first is the OPPOSITE of what RendererUploadTexture
  wants.  Cube maps do not follow the bottom-left origin of 2D textures - the
  face layout was fixed by RenderMan long before OpenGL, and it puts each
  face's first row at the top.  So the caller must NOT flip these.

  The handle goes in the same table as 2D textures.  Which target to bind it
  to is up to whoever draws with it, the same as for any texture.
*/
internal texture_handle
RendererUploadCubemap(renderer *Renderer, uint8 **Faces,
                      uint32 FaceSize, uint32 ChannelCount)
{
    game_opengl_api *GL = Renderer->GL;
    texture_handle Result = {};

    if(Renderer->TextureCount >= RENDERER_MAX_TEXTURES)
    {
        return(Result);
    }

    uint32 Format = (ChannelCount == 4) ? GL_RGBA : GL_RGB;

    // A sky is always a colour, so always sRGB.  See texture_encoding.
    uint32 InternalFormat = (ChannelCount == 4) ? GL_SRGB8_ALPHA8 : GL_SRGB8;

    uint32 Texture = 0;
    GL->glGenTextures(1, &Texture);
    GL->glBindTexture(GL_TEXTURE_CUBE_MAP, Texture);
    GL->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    for(uint32 FaceIndex = 0;
        FaceIndex < 6;
        ++FaceIndex)
    {
        GL->glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + FaceIndex, 0, (int32)InternalFormat,
                         (int32)FaceSize, (int32)FaceSize, 0,
                         Format, GL_UNSIGNED_BYTE, Faces[FaceIndex]);
    }

    // No mipmaps: the sky is always the same distance away, so it is never
    // shrunk.  CLAMP_TO_EDGE on all THREE axes - R is the third one a cube
    // map has - so no face ever wraps around and samples its opposite edge.
    GL->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    GL->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GL->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    GL->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    GL->glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

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

    // Linear: white is 1.0 under either encoding, and a placeholder is not a
    // colour anyone chose.
    return(RendererUploadTexture(Renderer, White, 1, 1, 4,
                                 TextureWrap_Repeat, TextureFilter_Linear,
                                 TextureEncoding_Linear));
}

/*
  Creates the shadow map: a depth texture and a framebuffer that draws into
  it.  Once, at startup - the size is fixed, so a window resize never
  touches it.
*/
internal void
RendererCreateShadowMap(renderer *Renderer)
{
    game_opengl_api *GL = Renderer->GL;

    // 1. The depth texture.  DEPTH_COMPONENT24: one 24-bit depth per pixel,
    //    the same precision as the scene's own depth buffer.
    GL->glGenTextures(1, &Renderer->ShadowDepth);
    GL->glBindTexture(GL_TEXTURE_2D, Renderer->ShadowDepth);
    GL->glTexImage2D(GL_TEXTURE_2D, 0, (int32)GL_DEPTH_COMPONENT24,
                     SHADOW_MAP_SIZE, SHADOW_MAP_SIZE, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, 0);

    // NEAREST, not LINEAR.  These are distances, and averaging the distance
    // to a roof with the distance to the floor below it gives a distance to
    // nothing at all.  Softening happens in the shader instead (E8).
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // 2. The framebuffer, with ONLY a depth attachment.
    GL->glGenFramebuffers(1, &Renderer->ShadowFramebuffer);
    GL->glBindFramebuffer(GL_FRAMEBUFFER, Renderer->ShadowFramebuffer);
    GL->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                               GL_TEXTURE_2D, Renderer->ShadowDepth, 0);

    // NOTE: No colour attachment, so say so.  By default a framebuffer draws
    // colour into COLOR_ATTACHMENT0, and in GL 3.3 pointing at an attachment
    // that does not exist makes the whole framebuffer incomplete.
    GL->glDrawBuffer(GL_NONE);
    GL->glReadBuffer(GL_NONE);

    uint32 Status = GL->glCheckFramebufferStatus(GL_FRAMEBUFFER);
    Assert(Status == GL_FRAMEBUFFER_COMPLETE);

    // Back to the window, so nothing after this draws into the shadow map
    // by accident.
    GL->glBindFramebuffer(GL_FRAMEBUFFER, 0);
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
    Result->GL->glEnable(GL_STENCIL_TEST);
    Result->GL->glDepthFunc(GL_LESS);
    Result->GL->glEnable(GL_CULL_FACE);  
    Result->GL->glCullFace(GL_BACK);
    /*
      NOTE(yigit): Asking the pixel format for stencil bits is a
      REQUEST, and a context can come back without them - at which point every
      stencil call still succeeds, glGetError stays clean, the test behaves as
      though it always passes, and the outline simply never appears.  There is
      nothing to find by reading the code, because the code is correct.

      So the assumption is checked once, here, and stated out loud.  This is
      also the one place that would catch a pixel format changed by hand, or a
      driver that quietly declined the request.
    */
    int32 StencilBits = 0;
    Result->GL->glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER, GL_STENCIL,
        GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE, &StencilBits);
    Assert(StencilBits >= 8);

    // NOTE(yigit): Starts at 1, not 0.  A cache that has never looked anything
    // up holds generation 0, so the first frame always refreshes.
    Result->ShaderGeneration = 1;

    // See the note on the fields - identity, not the zeros PushStruct leaves.
    Result->CurrentView = Mat4Identity();
    Result->CurrentProjection = Mat4Identity();

    // See the note on GL_TEXTURE_CUBE_MAP_SEAMLESS.  Global, not per texture.
    Result->GL->glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

    Result->GL->glGenVertexArrays(1, &Result->FullscreenVAO);

    RendererCreateShadowMap(Result);

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


/*
  Puts the stencil state in place for one draw.  See stencil_mode
  for what each mode is for.

  The three calls, in plain words:
    glStencilFunc(test, ref, mask) - which pixels may be drawn: "compare the
                                     stored value to ref using test".
    glStencilOp(fail, zfail, pass) - what to store afterwards, for a pixel that
                                     failed the stencil test, failed the depth
                                     test, or passed both.
    glStencilMask(bits)            - which bits may be written at all.  0x00
                                     turns writing off entirely.

  NOTE(yigit): Every mode sets all three.  Stencil state is global and sticks
  until changed, so a mode that set only one would inherit the other two from
  whatever drew last - and the mask left at 0x00 by an outline would then
  silently stop the next frame's clear from clearing.
*/
internal void
SetStencilMode(game_opengl_api *GL, stencil_mode Mode)
{
    switch(Mode)
    {
        case StencilMode_Off:
        {
            // Every pixel passes, nothing is written.
            GL->glStencilFunc(GL_ALWAYS, 0, 0xFF);
            GL->glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            GL->glStencilMask(0xFF);
        } break;

        case StencilMode_Write:
        {
            // Every pixel passes, and each one that ends up visible (passes
            // the depth test too) stores a 1.
            GL->glStencilFunc(GL_ALWAYS, 1, 0xFF);
            GL->glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
            GL->glStencilMask(0xFF);
        } break;

        case StencilMode_Outside:
        {
            // Only pixels whose stored value is NOT 1 pass - that is, pixels
            // outside the object drawn with StencilMode_Write.  Nothing is
            // written, so the mark stays intact for the next outline.
            GL->glStencilFunc(GL_NOTEQUAL, 1, 0xFF);
            GL->glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            GL->glStencilMask(0x00);
        } break;

        InvalidDefaultCase;
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
              mat4 ModelMatrix, stencil_mode Stencil)
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
    SetStencilMode(GL, Stencil);
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

    L->PointLightCount = GetUniformLocation(GL, Program, "pointLightCount");

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
    //
    // NOTE(yigit): Every 3D program needs its own copy of these.  A program
    // added later and forgotten here is invisible until it draws, and then its
    // geometry sits at the origin in a way that looks like a bad model rather
    // than a missing uniform.
    uint32 Lamp = Renderer->Programs[RenderProgram_Lamp];
    SetUniformMat4(GL, Lamp, "view", Camera->View);
    SetUniformMat4(GL, Lamp, "projection", Camera->Projection);

    uint32 Outline = Renderer->Programs[RenderProgram_Outline];
    SetUniformMat4(GL, Outline, "view", Camera->View);
    SetUniformMat4(GL, Outline, "projection", Camera->Projection);
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

    // The shader loops over exactly this many - see the note on
    // pointLightCount in fragmentShader.frag for why it cannot loop over all
    // four and let the unused ones contribute nothing.
    SetUniformIntAt(GL, L->PointLightCount, (int32)LightCount);

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

    // The shadow map, and the matrix that finds each pixel in it - the same
    // one the shadow pass drew with, kept from the ShadowPass command.
    //
    // NOTE(yigit): Texture unit 3.  GameDrawModel rebinds units 0, 1 and 2
    // for every submesh (diffuse, specular, alpha mask), so a shadow map on
    // any of those would be replaced before the first draw.  Nothing else in
    // the frame touches unit 3, so binding it once here is enough.
    uint32 Lit = Renderer->Programs[RenderProgram_Lit];
    SetUniformMat4(GL, Lit, "lightSpace", Renderer->LightSpace);
    SetUniformInt(GL, Lit, "shadowMap", 3);

    GL->glActiveTexture(GL_TEXTURE3);
    GL->glBindTexture(GL_TEXTURE_2D, Renderer->ShadowDepth);
    GL->glActiveTexture(GL_TEXTURE0);
}

// NOTE(yigit): Only the colour.  View and projection are the camera's, and go
// up in SetCameraUniforms along with the lit program's copies.
internal void
SetLampUniforms(game_opengl_api *GL, uint32 Program, render_command_lighting *Lighting)
{
    // White.  It used to take the flashlight's colour, and there is no
    // flashlight any more - nor any lamp drawn with this program, but it is
    // kept for whenever point lights come back.
    SetUniformVec4(GL, Program, "LightColor", Vec3(1.0f, 1.0f, 1.0f), 1.0f);
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
    GL->glVertexAttribPointer(2,                          // attribute number 2 (the shader's "location = 2")
            4,                          // it is 4 floats (vec4: r, g, b, a)
            GL_FLOAT, GL_FALSE,
            sizeof(overlay_vertex),     // 32 bytes from one vertex to the next
            (void *)(4 * sizeof(real32)));  // it starts 16 bytes into each vertex:
                                            // after Position (2 floats) + TexCoord (2 floats)
    GL->glEnableVertexAttribArray(2);
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
    SetStencilMode(GL, StencilMode_Off);

    // NOTE(yigit): Culling off for the 2D pass.  The overlay projection flips
    // Y so that it grows downward, and flipping one axis reverses the winding
    // of every triangle - quads that are counter-clockwise in pixel space come
    // out CLOCKWISE on screen, which is a back face, and every glyph is culled.
    // A flat overlay has no back to hide, so there is nothing to cull anyway.
    GL->glDisable(GL_CULL_FACE);

    GL->glBindVertexArray(Buffer->VAO);
    GL->glDrawArrays(GL_TRIANGLES, 0, (int32)Overlay->VertexCount);
    GL->glBindVertexArray(0);

    GL->glEnable(GL_CULL_FACE);
    GL->glDepthMask(GL_TRUE);
    GL->glEnable(GL_DEPTH_TEST);
    GL->glDisable(GL_BLEND);
}

/*
  Draws the sky behind everything already in the depth buffer.

  NOTE(yigit): No cube mesh.  One fullscreen triangle, pinned to the far
  plane, and the vertex shader works out which DIRECTION each corner of the
  screen is looking in by running the camera matrices backwards.  The
  fragment shader samples the cube map with that direction.  A cube mesh
  would have to be kept centred on the camera and drawn inside-out; this
  has no geometry to get wrong.

  Depth test stays ON, so the sky only lands where nothing else did - but
  with LEQUAL rather than LESS.  The triangle sits at depth exactly 1.0,
  which is also what an untouched pixel was cleared to, and 1.0 < 1.0 is
  false: under LESS the sky would fail everywhere and draw nothing at all.
*/
internal void
RendererDrawSkybox(renderer *Renderer, texture_handle Cubemap)
{
    game_opengl_api *GL = Renderer->GL;

    uint32 Program = Renderer->Programs[RenderProgram_Skybox];
    GL->glUseProgram(Program);

    SetUniformMat4(GL, Program, "view", Renderer->CurrentView);
    SetUniformMat4(GL, Program, "projection", Renderer->CurrentProjection);
    SetUniformInt(GL, Program, "sky", 0);

    GL->glActiveTexture(GL_TEXTURE0);
    GL->glBindTexture(GL_TEXTURE_CUBE_MAP, RendererGetTexture(Renderer, Cubemap));

    // Depth WRITE off as well: the sky is infinitely far away, and nothing
    // drawn after it should ever be hidden behind it.
    GL->glDepthFunc(GL_LEQUAL);
    GL->glDepthMask(GL_FALSE);
    SetStencilMode(GL, StencilMode_Off);

    GL->glBindVertexArray(Renderer->FullscreenVAO);
    GL->glDrawArrays(GL_TRIANGLES, 0, 3);
    GL->glBindVertexArray(0);

    GL->glDepthMask(GL_TRUE);
    GL->glDepthFunc(GL_LESS);
}

/*
  Blurs the HDR image into BloomMips[0], for the tonemap pass to mix in.

  Down: HDR -> mip 0 -> mip 1 -> ... -> the smallest, each half the size.
  Up:   the smallest -> ... -> mip 0, each ADDED on top of the level above.

  NOTE(yigit): A texture must never be read while it is the one being drawn
  into - the result is undefined, and in practice garbage.  Every pass here
  reads one level and writes a DIFFERENT one, which is why the chain is
  separate textures rather than one texture read and written in place.

  Expects depth and stencil testing already off - see RenderCommand_EndScene.
*/
internal void
RendererRenderBloom(renderer *Renderer)
{
    game_opengl_api *GL = Renderer->GL;

    GL->glBindFramebuffer(GL_FRAMEBUFFER, Renderer->BloomFramebuffer);
    GL->glBindVertexArray(Renderer->FullscreenVAO);
    GL->glActiveTexture(GL_TEXTURE0);

    // ---- Down ------------------------------------------------------------
    uint32 Down = Renderer->Programs[RenderProgram_BloomDown];
    GL->glUseProgram(Down);
    SetUniformInt(GL, Down, "srcTexture", 0);

    // The first level reads the HDR image itself.
    uint32 Source = Renderer->HDRColor;
    int32 SourceWidth = Renderer->HDRWidth;
    int32 SourceHeight = Renderer->HDRHeight;

    for(uint32 Mip = 0;
        Mip < BLOOM_MIP_COUNT;
        ++Mip)
    {
        // Draw into this level...
        GL->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, Renderer->BloomMips[Mip], 0);
        GL->glViewport(0, 0, Renderer->BloomMipWidth[Mip], Renderer->BloomMipHeight[Mip]);

        // ...reading the one above it.
        SetUniformVec2(GL, Down, "srcTexelSize",
                       1.0f / (real32)SourceWidth, 1.0f / (real32)SourceHeight);
        GL->glBindTexture(GL_TEXTURE_2D, Source);

        GL->glDrawArrays(GL_TRIANGLES, 0, 3);

        // What was just drawn is the next level's source.
        Source = Renderer->BloomMips[Mip];
        SourceWidth = Renderer->BloomMipWidth[Mip];
        SourceHeight = Renderer->BloomMipHeight[Mip];
    }

    // ---- Up --------------------------------------------------------------
    uint32 Up = Renderer->Programs[RenderProgram_BloomUp];
    GL->glUseProgram(Up);
    SetUniformInt(GL, Up, "srcTexture", 0);
    SetUniformFloat(GL, Up, "filterRadius", 0.005f);

    // ADD, don't replace: new * 1 + existing * 1.  Each bigger level keeps
    // its own blur and gains the wider blur from below.
    GL->glEnable(GL_BLEND);
    GL->glBlendFunc(GL_ONE, GL_ONE);

    // NOTE(yigit): Signed, because it counts DOWN.  An unsigned counter would
    // wrap from 0 to four billion instead of going negative, and "Mip > 0"
    // would then be true forever.
    for(int32 Mip = BLOOM_MIP_COUNT - 1;
        Mip > 0;
        --Mip)
    {
        // Read this level, draw into the next bigger one.
        GL->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, Renderer->BloomMips[Mip - 1], 0);
        GL->glViewport(0, 0, Renderer->BloomMipWidth[Mip - 1], Renderer->BloomMipHeight[Mip - 1]);

        GL->glBindTexture(GL_TEXTURE_2D, Renderer->BloomMips[Mip]);
        GL->glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // Blending is global state.  Left on, it would leak into the next
    // frame's 3D pass and every draw would add onto the last.
    GL->glDisable(GL_BLEND);
    GL->glBindVertexArray(0);
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
            case RenderCommand_BeginScene:
                {
                    // The shadow pass turned it off.
                    GL->glEnable(GL_CULL_FACE);

                    render_command_begin_scene *Command = (render_command_begin_scene *)Header;

                    RendererResizeHDRTarget(Renderer, Command->Width, Command->Height);

                    // From here on, every draw lands in our texture, not the window.
                    GL->glBindFramebuffer(GL_FRAMEBUFFER, Renderer->HDRFramebuffer);
                    GL->glViewport(0, 0, Command->Width, Command->Height);
                } break;

            case RenderCommand_EndScene:
                {
                    // NOTE: Every pass from here on is a fullscreen triangle
                    // that must never be rejected - bloom's as well as the
                    // tonemap's.  Depth test off: the window's depth buffer is
                    // never cleared any more, so it holds garbage.  Stencil
                    // off: the lamp outlines leave stencil state behind.
                    
                    render_command_end_scene *Command = (render_command_end_scene *)Header;
                    GL->glDisable(GL_DEPTH_TEST);
                    SetStencilMode(GL, StencilMode_Off);

                    // Blur the HDR image into Renderer->BloomMips[0].
                    RendererRenderBloom(Renderer);

                    // Into the window.  The viewport MUST be set again here:
                    // bloom left it at the size of its last level.
                    GL->glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    GL->glViewport(0, 0, Renderer->HDRWidth, Renderer->HDRHeight);

                    uint32 Program = Renderer->Programs[RenderProgram_Tonemap];
                    GL->glUseProgram(Program);

                    // Two textures now, so two texture units: the sharp HDR
                    // image on 0, the blurred one on 1.
                    GL->glActiveTexture(GL_TEXTURE0);
                    GL->glBindTexture(GL_TEXTURE_2D, Renderer->HDRColor);
                    SetUniformInt(GL, Program, "hdrScene", 0);

                    GL->glActiveTexture(GL_TEXTURE1);
                    GL->glBindTexture(GL_TEXTURE_2D, Renderer->BloomMips[0]);
                    SetUniformInt(GL, Program, "bloomTexture", 1);

                    // Back to unit 0, which every other draw assumes is the
                    // active one.
                    GL->glActiveTexture(GL_TEXTURE0);

                    SetUniformFloat(GL, Program, "exposure", Command->Exposure);
                    SetUniformFloat(GL, Program, "bloomStrength", 0.04f);

                    // Three vertices, no buffer: the vertex shader makes them up.
                    GL->glBindVertexArray(Renderer->FullscreenVAO);
                    GL->glDrawArrays(GL_TRIANGLES, 0, 3);
                    GL->glBindVertexArray(0);

                    // Put back what the 3D pass expects next frame.
                    GL->glEnable(GL_DEPTH_TEST);
                } break;
            case RenderCommand_Clear:
            {
                render_command_clear *Command = (render_command_clear *)Header;
                GL->glClearColor(Command->Color.X, Command->Color.Y,
                                 Command->Color.Z, Command->Color.W);

                // NOTE(yigit): glClear obeys the write masks - a buffer whose
                // mask is off is silently NOT cleared.  So both are turned back
                // on first, whatever the last frame left them at.
                GL->glDepthMask(GL_TRUE);
                GL->glStencilMask(0xFF);
                GL->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
                            GL_STENCIL_BUFFER_BIT);
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
                GameDrawModel(Renderer, Program, Command->Model,
                              Command->Transform, Command->Stencil);
            } break;

            case RenderCommand_DrawOverlay:
            {
                render_command_draw_overlay *Command = (render_command_draw_overlay *)Header;

                RendererDrawOverlay(Renderer, Command->Overlay, Command->Projection,
                                    Command->Font ? Command->Font->Texture
                                                  : Renderer->WhiteTexture,
                                    Command->Color);
            } break;

            case RenderCommand_DrawSkybox:
            {
                render_command_draw_skybox *Command = (render_command_draw_skybox *)Header;

                RendererDrawSkybox(Renderer, Command->Cubemap);
            } break;

            case RenderCommand_ShadowPass:
            {
                render_command_shadow_pass *Command = (render_command_shadow_pass *)Header;

                // Kept for the lit pass, which needs the same matrix to look
                // things up in the map this pass is about to draw.
                Renderer->LightSpace = Command->LightSpace;

                GL->glBindFramebuffer(GL_FRAMEBUFFER, Renderer->ShadowFramebuffer);
                GL->glViewport(0, 0, SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);

                // Clear to the farthest possible depth: "the sun sees nothing".
                // Depth mask on first - glClear obeys it (see the Clear case).
                GL->glDepthMask(GL_TRUE);
                GL->glClear(GL_DEPTH_BUFFER_BIT);

                // NOTE: Culling OFF for the shadow pass.  Sponza is full of
                // one-sided surfaces - leaves, curtains, cloth - and with back
                // faces culled, any of them turned away from the sun would
                // cast no shadow at all.  BeginScene turns it back on.
                GL->glDisable(GL_CULL_FACE);

                uint32 Shadow = Renderer->Programs[RenderProgram_Shadow];
                SetUniformMat4(GL, Shadow, "lightSpace", Command->LightSpace);

                // For the alpha test in shadow.frag: the units GameDrawModel
                // binds each submesh's diffuse map and alpha mask to.
                SetUniformInt(GL, Shadow, "diffuseMap", 0);
                SetUniformInt(GL, Shadow, "alphaMask", 2);
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
