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
    RenderProgram_Overlay,

    RenderProgram_Count,
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

enum render_command_type
{
    RenderCommand_Clear,
    RenderCommand_Setup,
    RenderCommand_DrawModel,
    RenderCommand_DrawOverlay,
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

/*
  Everything shared by every draw this frame: where the camera is and what the
  lights are.  Pushed once, ahead of the draws.

  NOTE(yigit): Positions and directions are given in WORLD space, not view
  space, even though the shaders want view space.  The conversion needs the
  view matrix, which is a rendering detail - so the backend does it.  Handing
  the backend view-space values would bake this renderer's choice of lighting
  space into the command stream.
*/
struct render_command_setup
{
    render_command_header Header;

    mat4 View;
    mat4 Projection;

    vec3 DirLightDirectionWorld;
    vec3 DirAmbient;
    vec3 DirDiffuse;
    vec3 DirSpecular;

    render_point_light PointLights[4];
    uint32 PointLightCount;

    vec3 SpotAmbient;
    vec3 SpotDiffuse;
    vec3 SpotSpecular;
    real32 SpotConstant;
    real32 SpotLinear;
    real32 SpotQuadratic;
    real32 SpotCutOff;          // cosines, not angles
    real32 SpotOuterCutOff;
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

// Returns the command so the caller can fill in the lights, which are too many
// to pass as arguments without the call site becoming unreadable.
internal render_command_setup *
PushSetup(render_buffer *Buffer, mat4 View, mat4 Projection)
{
    render_command_setup *Command =
        PushRenderCommand(Buffer, render_command_setup, RenderCommand_Setup);

    if(Command)
    {
        Command->View = View;
        Command->Projection = Projection;
        Command->PointLightCount = 0;
    }

    return(Command);
}

internal void
PushModel(render_buffer *Buffer, render_model *Model, mat4 Transform,
          render_program Program)
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
