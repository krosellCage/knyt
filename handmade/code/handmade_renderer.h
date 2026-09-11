#if !defined(HANDMADE_RENDERER_H)
/*
  NOTE(yigit): What the game says it wants drawn, as DATA rather than as calls.

  There is no OpenGL in this file and there must never be.  That is the whole
  point: the game fills a buffer with commands, and something else - a GL
  backend today, a Vulkan one later - walks it and decides how to execute them.
  A direct call happens NOW, in THIS api, on THIS thread.  A pushed command
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

    RenderProgram_Count,
};

enum render_command_type
{
    RenderCommand_Clear,
    RenderCommand_Setup,
    RenderCommand_DrawModel,
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

struct render_buffer
{
    uint8 *Base;
    memory_index Size;
    memory_index Used;
};

internal void
RenderBufferInitialize(render_buffer *Buffer, memory_arena *Arena, memory_index Size)
{
    Buffer->Base = (uint8 *)PushSize_(Arena, Size);
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
    // Dropping the command is the right failure.  Growing mid-frame would mean
    // moving a buffer that already holds commands pointing at their own sizes.
    if((Buffer->Used + Size) > Buffer->Size)
    {
        return(0);
    }

    render_command_header *Header = (render_command_header *)(Buffer->Base + Buffer->Used);
    Buffer->Used += Size;

    Header->Type = Type;
    Header->Size = Size;

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

#define HANDMADE_RENDERER_H
#endif
