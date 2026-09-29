#if !defined(HANDMADE_RENDER_COMMANDS_H)
/*
  WHAT the game wants drawn.  The other half is handmade_render_opengl.h, which
  decides HOW.

  NOTE(yigit): This is the GAME side of the seam - it describes a frame as DATA
  rather than as calls.  There must never be a graphics api in this file.

  The game fills a buffer with commands, and something else - an OpenGL backend
  today, a Vulkan one later - walks it and decides how to execute them.  A
  direct call happens NOW, in THIS api, on THIS thread.  A pushed command
  happens later, in whatever, wherever.

  Five things fall out of that indirection:

    - a second backend, which is why this exists
    - sorting, because commands are data and can be reordered before submitting
    - recording a frame to disk and replaying it, the same trick the input
      system already does one layer up
    - filling the buffer on one thread while another drains it
    - a backend that counts commands instead of drawing them, so the game layer
      can be tested with no GPU at all

  It is called a push BUFFER rather than a command list because of how it is
  allocated: an arena you push into, never free, and reset at the top of every
  frame.  Exactly what TransientArena and ResetArena already do for models.
*/

// Which shader a draw wants, by ROLE rather than by handle.
//
// NOTE(yigit): A GLuint in here would put OpenGL in the command stream, which
// is the usual way this abstraction gets quietly broken.  The backend owns the
// mapping from role to whatever its api calls a program.
enum render_program
{
    RenderProgram_Lit,
    RenderProgram_Lamp,

    // One flat colour, for the second pass of a stencil outline.
    // Separate from Lamp because the lamp takes the light's own colour, and a
    // white light would make an outline the same colour as the thing it is
    // outlining - a feature that works perfectly and cannot be seen.
    RenderProgram_Outline,

    RenderProgram_Overlay,

    // The sky: a cube map sampled by view direction, drawn behind everything.
    RenderProgram_Skybox,

    RenderProgram_Tonemap,

    // Bloom: shrink the HDR image down a chain of smaller textures, then grow
    // it back up, adding each level together.
    RenderProgram_BloomDown,
    RenderProgram_BloomUp,

    // Depth only, from the sun.  Fills the shadow map.
    RenderProgram_Shadow,

    // Image-based lighting: bake the sky into lookup textures, once.
    RenderProgram_IBLIrradiance,    // the sky blurred for diffuse light
    RenderProgram_IBLPrefilter,     // the sky blurred by roughness, for reflections
    RenderProgram_IBLBrdf,          // the Fresnel/geometry table

    RenderProgram_Count,
};

/*
  What a draw does with the stencil buffer.  An outline takes two
  draws of the same object:

    1. StencilMode_Write   - draw the object normally, and mark every pixel it
                             covers with a 1 in the stencil buffer.
    2. StencilMode_Outside - draw a slightly BIGGER copy, but only on pixels
                             that are NOT marked.  What survives is the thin
                             rim around the first draw: the outline.

  Everything else draws with StencilMode_Off, which neither tests nor writes.

  NOTE(yigit): The stencil buffer is cleared once per frame, in the Clear
  command, not per object.  So an outline also stays off every object marked
  EARLIER in the frame, which is what you want - one lamp's rim should not be
  painted over another lamp.
*/
enum stencil_mode
{
    StencilMode_Off,
    StencilMode_Write,
    StencilMode_Outside,
};

/*
  How a texture should behave once it is on the GPU, described without naming
  any api's constants.

  NOTE(yigit): Every graphics api spells these as its own magic numbers, and
  passing one of those down from the asset loaders would put that api straight
  back into code just cleaned of it.  So the loaders say what they WANT, and
  the backend picks whichever constant means it.
*/
enum texture_wrap
{
    TextureWrap_Repeat,         // tiles - what a surface texture wants
    TextureWrap_ClampToEdge,    // stops at the border - what an atlas wants
};

enum texture_filter
{
    TextureFilter_Linear,           // no mipmaps: drawn at 1:1 and never shrunk
    TextureFilter_LinearMipmap,     // build mipmaps: this will be seen at a distance
};

enum texture_encoding
{
    TextureEncoding_Linear,     // data: masks, normals, coverage
    TextureEncoding_SRGB,       // colour: diffuse maps, skies
};

enum render_command_type
{
    RenderCommand_BeginScene,
    RenderCommand_EndScene,
    RenderCommand_Clear,
    RenderCommand_Camera,
    RenderCommand_Lighting,
    RenderCommand_DrawModel,
    RenderCommand_DrawOverlay,
    RenderCommand_DrawSkybox,
    RenderCommand_ShadowPass,
};

// NOTE(yigit): Size is what lets commands be different sizes.  A Clear is
// small and a Setup is several hundred bytes; without a size the walker would
// have to understand every type just to step past one it does not care about.
struct render_command_header
{
    render_command_type Type;
    uint32 Size;
};

struct render_point_light
{
    vec3 PositionWorld;

    vec3 Ambient;
    vec3 Diffuse;
    vec3 Specular;

    real32 Constant;
    real32 Linear;
    real32 Quadratic;
};

struct render_command_clear
{
    render_command_header Header;
    vec4 Color;
};

// Everything after this draws into the HDR target instead of the window.
// Carries the window size because the backend has no other way to learn it.
struct render_command_begin_scene
{
    render_command_header Header;
    int32 Width;
    int32 Height;
};

// Puts the HDR target on screen and goes back to drawing into the window.
struct render_command_end_scene
{
    render_command_header Header;
    real32 Exposure;
};

/*
  The sky.  Push it AFTER the opaque geometry: it is drawn at the far plane
  and depth tested, so it only lands on pixels nothing else covered - and the
  depth test rejects the rest before the fragment shader ever runs for them.
  Pushed first, it would shade every pixel on screen only to be painted over.

  Uses whichever camera command came before it, like a model draw does.
*/
struct render_command_draw_skybox
{
    render_command_header Header;
    texture_handle Cubemap;
};

/*
  Everything after this draws into the shadow map, from the sun, until the
  next BeginScene switches back.  Push the models that should CAST shadows
  between the two, with RenderProgram_Shadow.

  Carries the sun's camera as one matrix - view and projection already
  multiplied - because nothing needs them separately.
*/
struct render_command_shadow_pass
{
    render_command_header Header;
    mat4 LightSpace;
};

/*
  Where the frame is being looked at from.  Pushed once, ahead of everything
  that depends on it.

  NOTE(yigit): Separate from the lighting command below even though both are
  pushed together today, because they change for completely different reasons.
  A camera is a camera under any lighting model; the Phong block below stops
  existing the day a second model turns up.  Keeping them fused would mean
  rewriting the camera path to change how lights work.
*/
struct render_command_camera
{
    render_command_header Header;

    mat4 View;
    mat4 Projection;
};

/*
  Every light in the scene.  Pushed once, AFTER the camera and ahead of the
  draws - see the ordering note in render_command_camera.

  NOTE(yigit): Positions and directions are given in WORLD space, not view
  space, even though the shaders want view space.  The conversion needs the
  view matrix, which is a rendering detail - so the backend does it, using the
  camera it was handed.  Handing the backend view-space values would bake this
  renderer's choice of lighting space into the command stream.

  NOTE(yigit): This whole struct is Phong-shaped, and deliberately so.  Ambient,
  diffuse and specular PER LIGHT is not a universal way to describe lighting -
  it is how the Blinn-Phong shaders in handmade/data want to be fed.  A physical
  model would replace it with a colour and an intensity per light and move the
  rest onto the material.  That is a NEW command type alongside this one rather
  than an edit to it; Header->Size is what makes adding one safe.
*/
struct render_command_lighting
{
    render_command_header Header;

    vec3 DirLightDirectionWorld;
    vec3 DirAmbient;
    vec3 DirDiffuse;
    vec3 DirSpecular;

    render_point_light PointLights[4];
    uint32 PointLightCount;
};

struct render_command_draw_model
{
    render_command_header Header;

    // NOTE(yigit): A pointer, not a handle.  The model lives in game_state,
    // which outlives the frame, and the backend runs in the same process - so
    // a handle would be indirection with nothing on the other end of it.  That
    // changes when a backend needs its own resource table, which is the point
    // at which a Vulkan spike will say so.
    render_model *Model;

    mat4 Transform;
    render_program Program;
    stencil_mode Stencil;
};

/*
  The 2D pass: whatever quads the overlay accumulated this frame, drawn with
  blending on and depth off.

  NOTE(yigit): One command for the whole overlay rather than one per quad.  The
  quads already live in a contiguous array that goes to the GPU in a single
  upload, so per-quad commands would be a second copy of a list that exists.
  Where a push buffer earns per-item commands is when items need SORTING, and
  an overlay is deliberately drawn in the order it was written.
*/
struct render_command_draw_overlay
{
    render_command_header Header;

    overlay *Overlay;
    loaded_font *Font;

    mat4 Projection;
    vec3 Color;
};

// NOTE(yigit): 16 rather than 8, so a command struct could hold a __m128
// without this having to change.  The waste is a few bytes per command on a
// handful of commands a frame.
#define RENDER_COMMAND_ALIGNMENT 16

struct render_buffer
{
    uint8 *Base;
    memory_index Size;
    memory_index Used;
};

internal void
RenderBufferInitialize(render_buffer *Buffer, memory_arena *Arena, memory_index Size)
{
    // NOTE(yigit): Over-allocate and walk the base forward to an aligned
    // address.  The arena hands out whatever offset it is at, which for a
    // block sitting after game_state is any number at all - and the first
    // command would then be misaligned before a single push happened.
    uint8 *Block = (uint8 *)PushSize_(Arena, Size + RENDER_COMMAND_ALIGNMENT);
    memory_index Misalignment = ((memory_index)Block) & (RENDER_COMMAND_ALIGNMENT - 1);
    memory_index Adjust = Misalignment ? (RENDER_COMMAND_ALIGNMENT - Misalignment) : 0;

    Buffer->Base = Block + Adjust;
    Buffer->Size = Size;
    Buffer->Used = 0;
}

// Call at the top of every frame.  Nothing is freed, the cursor just goes back
// to zero - which is why this is a push buffer and not a list of allocations.
inline void
RenderBufferReset(render_buffer *Buffer)
{
    Buffer->Used = 0;
}

internal void *
PushRenderCommand_(render_buffer *Buffer, render_command_type Type, uint32 Size)
{
    /*
      NOTE(yigit): The size is rounded UP so the next command starts aligned.

      Every command today is built from 4-byte fields, so the cursor happens to
      stay aligned on its own - which is exactly the problem.  Nothing enforces
      it.  Put one double or one __m128 in a command struct and the command
      AFTER it gets misaligned loads: a crash, or silently wrong data, nowhere
      near the struct that caused it.
    */
    uint32 AlignedSize = (Size + (RENDER_COMMAND_ALIGNMENT - 1)) &
                         ~(uint32)(RENDER_COMMAND_ALIGNMENT - 1);

    // NOTE(yigit): Dropping is still the right thing to DO at runtime - growing
    // mid-frame would move a buffer that already holds commands.  But a silent
    // drop looks exactly like "the lamps stopped rendering" with no clue why,
    // so a debug build stops here and says which frame overflowed.
    Assert((Buffer->Used + AlignedSize) <= Buffer->Size);
    if((Buffer->Used + AlignedSize) > Buffer->Size)
    {
        return(0);
    }

    render_command_header *Header = (render_command_header *)(Buffer->Base + Buffer->Used);
    Buffer->Used += AlignedSize;

    Header->Type = Type;

    // The ALIGNED size, not the struct size - this is what the walker steps by,
    // so it has to match what was actually consumed.
    Header->Size = AlignedSize;

    return(Header);
}

#define PushRenderCommand(Buffer, type, TypeEnum) \
    (type *)PushRenderCommand_(Buffer, TypeEnum, sizeof(type))

internal void
PushClear(render_buffer *Buffer, vec4 Color)
{
    render_command_clear *Command =
        PushRenderCommand(Buffer, render_command_clear, RenderCommand_Clear);

    if(Command)
    {
        Command->Color = Color;
    }
}

internal void
PushBeginScene(render_buffer *Buffer, int32 Width, int32 Height)
{
    render_command_begin_scene *Command =
        PushRenderCommand(Buffer, render_command_begin_scene, RenderCommand_BeginScene);

    if(Command)
    {
        Command->Width = Width;
        Command->Height = Height;
    }
}

internal void
PushEndScene(render_buffer *Buffer, real32 Exposure)
{
    render_command_end_scene *Command = 
        PushRenderCommand(Buffer, render_command_end_scene, RenderCommand_EndScene);
    if(Command)
    {
        Command->Exposure = Exposure;
    }
}

internal void
PushSkybox(render_buffer *Buffer, texture_handle Cubemap)
{
    // A sky that failed to load draws nothing, rather than a black cube.
    if(!Cubemap.Value)
    {
        return;
    }

    render_command_draw_skybox *Command =
        PushRenderCommand(Buffer, render_command_draw_skybox, RenderCommand_DrawSkybox);

    if(Command)
    {
        Command->Cubemap = Cubemap;
    }
}

internal void
PushShadowPass(render_buffer *Buffer, mat4 LightSpace)
{
    render_command_shadow_pass *Command =
        PushRenderCommand(Buffer, render_command_shadow_pass, RenderCommand_ShadowPass);

    if(Command)
    {
        Command->LightSpace = LightSpace;
    }
}

internal void
PushCamera(render_buffer *Buffer, mat4 View, mat4 Projection)
{
    render_command_camera *Command =
        PushRenderCommand(Buffer, render_command_camera, RenderCommand_Camera);

    if(Command)
    {
        Command->View = View;
        Command->Projection = Projection;
    }
}

// Returns the command so the caller can fill in the lights, which are too many
// to pass as arguments without the call site becoming unreadable.
//
// NOTE(yigit): Must be pushed after a PushCamera.  The backend needs the view
// matrix to put these lights in the space its shaders light in, and it takes
// that from whichever camera command came before.
internal render_command_lighting *
PushLighting(render_buffer *Buffer)
{
    render_command_lighting *Command =
        PushRenderCommand(Buffer, render_command_lighting, RenderCommand_Lighting);

    if(Command)
    {
        Command->PointLightCount = 0;
    }

    return(Command);
}

internal void
PushModel(render_buffer *Buffer, render_model *Model, mat4 Transform,
          render_program Program, stencil_mode Stencil)
{
    if(!Model || !Model->IndexCount)
    {
        return;
    }

    render_command_draw_model *Command =
        PushRenderCommand(Buffer, render_command_draw_model, RenderCommand_DrawModel);

    if(Command)
    {
        Command->Model = Model;
        Command->Transform = Transform;
        Command->Program = Program;
        Command->Stencil = Stencil;
    }
}

internal void
PushOverlay(render_buffer *Buffer, overlay *Overlay, loaded_font *Font,
            mat4 Projection, vec3 Color)
{
    if(!Overlay || !Overlay->VertexCount)
    {
        return;
    }

    render_command_draw_overlay *Command =
        PushRenderCommand(Buffer, render_command_draw_overlay, RenderCommand_DrawOverlay);

    if(Command)
    {
        Command->Overlay = Overlay;
        Command->Font = Font;
        Command->Projection = Projection;
        Command->Color = Color;
    }
}

#define HANDMADE_RENDER_COMMANDS_H
#endif
