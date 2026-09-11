#if !defined(HANDMADE_H)

/*
  NOTE(yigit):

  HANDMADE_INTERNAL:
    0 - Build for public release
    1 - Build for developer only

  HANDMADE_SLOW:
    0 - Not slow code allowed!
    1 - Slow code welcome.
*/

// TODO(yigit): Implement sine ourselves
#include <math.h>
#include "handmade_platform.h"
// NOTE(yigit): game_state holds vec3 camera fields, so the maths types have to
// be visible here.  handmade_math.h only needs real32, which the line above
// provides, and it is include-guarded so handmade.cpp including it again is a
// no-op.
#include "handmade_math.h"

#define internal static
#define local_persist static 
#define global_variable static

#define Pi32 3.14159265359f

#if HANDMADE_SLOW
// TODO(yigit): Complete assertion macro - don't worry everyone!
#define Assert(Expression) if(!(Expression)) {*(int *)0 = 0;}
#else
#define Assert(Expression)
#endif

#define Kilobytes(Value) ((Value)*1024LL)
#define Megabytes(Value) (Kilobytes(Value)*1024LL)
#define Gigabytes(Value) (Megabytes(Value)*1024LL)
#define Terabytes(Value) (Gigabytes(Value)*1024LL)

#define ArrayCount(Array) (sizeof(Array) / sizeof((Array)[0]))
// TODO(yigit): swap, min, max ... macros???

inline uint32
SafeTruncateUInt64(uint64 Value)
{
    // TODO(yigit): Defines for maximum values
    Assert(Value <= 0xFFFFFFFF);
    uint32 Result = (uint32)Value;
    return(Result);
}

inline game_controller_input *GetController(game_input *Input, int unsigned ControllerIndex)
{
    Assert(ControllerIndex < ArrayCount(Input->Controllers));
    
    game_controller_input *Result = &Input->Controllers[ControllerIndex];
    return(Result);
}

struct tile_map
{
    int32 CountX;
    int32 CountY;

    real32 UpperLeftX;
    real32 UpperLeftY;
    real32 TileWidth;
    real32 TileHeight;

    uint32 *Tiles;
};

// NOTE(yigit): Last-seen write times for one program's source files, so we can
// notice an edit on disk.  Only timestamps live here - never the file names.
// String literals are part of the DLL image and move every time it reloads, so
// a pointer parked in PermanentStorage would be left dangling into the copy
// that just got unloaded.
struct shader_watch
{
    uint64 VertWriteTime;
    uint64 FragWriteTime;
};

#define PushStruct(Arena, type) (type *)PushSize_(Arena, sizeof(type))
#define PushArray(Arena, Count, type) (type *)PushSize_(Arena, (Count)*sizeof(type))
internal void *
PushSize_(memory_arena *Arena, memory_index Size)
{
    Assert((Arena->Used + Size) <= Arena->Size);
    void *Result = Arena->Base + Arena->Used;
    Arena->Used += Size;

    return(Result);
}

// NOTE(yigit): An arena is zeroed at startup along with the rest of
// PermanentStorage, which leaves Size at 0 - so the first PushSize_ would
// assert.  Every arena has to be handed its block explicitly before use.
internal void
InitializeArena(memory_arena *Arena, memory_index Size, uint8 *Base)
{
    Arena->Size = Size;
    Arena->Used = 0;
    Arena->Base = Base;
}

// Hands the whole block back at once.  Only safe when nothing still points
// into the arena - which is the point of keeping model loading in an arena of
// its own: once the vertices are on the GPU, every byte of it is free again.
internal void
ResetArena(memory_arena *Arena)
{
    Arena->Used = 0;
}

// NOTE(yigit): Needed here, not from handmade.cpp, because render_submesh now
// holds an obj_material by value.  Everything it uses - the Push macros,
// Assert, vec2/vec3 - is already in scope by this point.
#include "handmade_obj.h"

// A run of the index buffer sharing one material - a copy of obj_submesh that
// survives the load, since the OBJ side of it lives in scratch memory.
#define MAX_SUBMESHES_PER_MODEL 64

struct render_submesh
{
    uint32 FirstIndex;
    uint32 IndexCount;

    // By value, not by index.  Submeshes are one-per-material by construction,
    // so a separate material table would only ever be read one-to-one.
    obj_material Material;

    // 0 when the material named no texture, in which case the draw falls back
    // to the 1x1 white texture and the colour comes from Material.Diffuse
    // alone.  The same fallback covers the alpha mask, where white means
    // fully opaque.
    uint32 DiffuseTexture;
    uint32 AlphaTexture;
};

// One mesh as the GPU holds it.  The CPU-side vertex and index arrays are gone
// by the time this exists - glBufferData copies them, so nothing has to be
// kept around.
struct render_model
{
    uint32 VAO;
    uint32 VBO;
    uint32 EBO;
    uint32 IndexCount;

    render_submesh Submeshes[MAX_SUBMESHES_PER_MODEL];
    uint32 SubmeshCount;
};

// NOTE(yigit): Included here rather than from handmade.cpp because game_state
// holds a camera by value.  It needs Pi32 and the internal macro, both defined
// above.
#include "handmade_camera.h"
// NOTE(yigit): game_state holds a render_buffer, and the renderer needs
// render_model which is declared above.  No OpenGL in that header by design.
#include "handmade_renderer.h"
// NOTE(yigit): After the Push macros above - LoadWAV allocates with PushArray.
#include "handmade_sound.h"
// NOTE(yigit): game_state holds an overlay by value.  This header deliberately
// calls OpenGL directly rather than using handmade_shader.h's uniform setters -
// that file includes this one, so reaching for them would close the circle.
#include "handmade_overlay.h"

struct game_state
{
    real32 PlayerX;
    real32 PlayerY;

    real32 tSine;

    // NOTE(yigit): Seconds since startup, accumulated from Input->dtForFrame
    // rather than read off a wall clock.  dtForFrame travels inside game_input,
    // which the recording system writes to disk every frame, so a clock built
    // this way replays identically during loop-edit playback.  A platform
    // wall clock would drift and break that determinism.
    real32 TimeSeconds;

    // NOTE(yigit): 3D rendering handles — created once in GameUpdateAndRender when
    // !Memory->IsInitialized, then reused every frame
    uint32 ShaderProgram[3];    // lit, lamp, overlay
    shader_watch ShaderWatches[3];

    // NOTE(yigit): All geometry AND all textures are loaded from disk now.  The
    // hand-written cube vertex table that used to live in GameInitOpenGL is
    // gone, so are the ten hardcoded container positions, and so is the fixed
    // container texture pair - a material names its own texture and the loader
    // fetches it.
    render_model Model;         // the subject of the scene
    render_model MarkerModel;   // a small cube drawn at each point light

    // NOTE(yigit): One white pixel.  Materials with no map_Kd bind this, and
    // white times Kd is Kd - so the shader never has to ask whether a texture
    // exists.  Cheaper than a branch, and it keeps one code path.
    uint32 WhiteTexture;

    // 2D screen-space pass - debug text and anything else that sits on top of
    // the scene.  Refilled from empty every frame.
    overlay Overlay;

    // NOTE(yigit): By value, and it holds only plain floats and a texture
    // handle - no pointers into the arena the atlas was baked in, so it
    // survives a DLL reload like everything else in here.
    loaded_font DebugFont;

    // What the game says it wants drawn.  Filled every frame, executed by the
    // backend, reset at the top of the next one.
    render_buffer RenderBuffer;

    // NOTE(yigit): Everything in PermanentStorage that comes AFTER game_state
    // itself.  Set up once via InitializeArena - see the !IsInitialized block
    // in GameUpdateAndRender.
    memory_arena WorldArena;

    // NOTE(yigit): Carved out of TransientStorage, and reset before every load.
    // OBJ parsing needs hundreds of megabytes of scratch for a real model -
    // far more than WorldArena holds - and none of it outlives the
    // glBufferData call that copies the result to the GPU.
    memory_arena TransientArena;

    // NOTE(yigit): Two cached uniform locations used to live here.  They are
    // gone because a location only survives as long as the linked program, and
    // shader hot reload relinks on every save - so anything parked in
    // PermanentStorage goes stale silently.  GameDrawModel caches per FRAME.

    // NOTE(yigit): By value, not a pointer - it lives in PermanentStorage, so
    // it survives DLL reloads and is captured by the input recording.
    camera Camera;

    // NOTE(yigit): Samples point into WorldArena, which is also inside
    // PermanentStorage, so this stays valid across a DLL reload.
    loaded_sound Music;
    uint32 MusicPlayCursor;

};

#define HANDMADE_H
#endif
