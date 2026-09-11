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

// For a branch that should be unreachable.  A switch that silently ignores a
// case it does not know is indistinguishable from one that handled it and drew
// nothing, which is the worst kind of bug to chase.
#define InvalidCodePath Assert(!"InvalidCodePath")
#define InvalidDefaultCase default: {InvalidCodePath;} break

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

/*
  Opaque handles to things the backend owns.

  NOTE(yigit): The value is an index into a table inside the renderer, PLUS
  ONE - so a zeroed handle means "nothing".  PermanentStorage starts zeroed, so
  that falls out for free rather than needing an explicit invalid constant.

  They are structs rather than bare uint32s on purpose.  You cannot do
  arithmetic on one by accident, you cannot pass a texture where a mesh is
  wanted, and you cannot hand one to a graphics api by mistake - the compiler
  stops all three.

  A backend's own buffer objects are not the same things, or even the same
  NUMBER of things, from one api to the next.  Spelling them out in a header
  the game layer reads would shape the game layer around whichever one happens
  to be underneath, so they do not appear here at all.
*/
struct mesh_handle           { uint32 Value; };
struct texture_handle        { uint32 Value; };

// NOTE(yigit): Separate from mesh_handle on purpose.  A mesh is static
// geometry with an index buffer, uploaded once.  This names a DYNAMIC vertex
// buffer with no indices, rewritten every frame.  One type for both would mean
// the backend guessing which of the two it was holding.
struct overlay_buffer_handle { uint32 Value; };

inline bool32 IsValidHandle(mesh_handle Handle)           { return(Handle.Value != 0); }
inline bool32 IsValidHandle(texture_handle Handle)        { return(Handle.Value != 0); }
inline bool32 IsValidHandle(overlay_buffer_handle Handle) { return(Handle.Value != 0); }

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

    // Zeroed when the material named no texture, in which case the draw falls
    // back to the 1x1 white texture and the colour comes from Material.Diffuse
    // alone.  The same fallback covers the alpha mask, where white means fully
    // opaque.
    texture_handle DiffuseTexture;
    texture_handle AlphaTexture;
};

// One mesh as the GPU holds it.  The CPU-side vertex and index arrays are gone
// by the time this exists - the upload copies them, so nothing has to be kept
// around.
struct render_model
{
    // One opaque handle.  What the backend keeps behind it is its own
    // business, and it is not the same set of things on every api.
    mesh_handle Mesh;

    // NOT behind the handle.  The game layer reads this for the debug overlay,
    // and a triangle count is a fact about the model rather than about whatever
    // is holding it.
    uint32 IndexCount;

    render_submesh Submeshes[MAX_SUBMESHES_PER_MODEL];
    uint32 SubmeshCount;
};

// NOTE(yigit): Included here rather than from handmade.cpp because game_state
// holds a camera by value.  It needs Pi32 and the internal macro, both defined
// above.
#include "handmade_camera.h"
// NOTE(yigit): After the Push macros above - LoadWAV allocates with PushArray.
#include "handmade_sound.h"
// NOTE(yigit): game_state holds an overlay by value.  That header is backend
// code, and it reaches its api directly rather than through
// handmade_shader.h's uniform setters - that file includes this one, so using
// them would close the circle.
#include "handmade_overlay.h"
// NOTE(yigit): LAST of the four.  A draw-overlay command names an overlay and
// a loaded_font, and a draw-model command names a render_model, so everything
// it points at has to be declared before it.  No graphics api in that header
// by design.
#include "handmade_render_commands.h"

// NOTE(yigit): Declared, never defined here.  See the Renderer member below.
struct renderer;

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

    // NOTE(yigit): An INCOMPLETE type, and only ever a pointer.  The game knows
    // a renderer exists and can hand it around; it does not know what is in
    // one.  That is what keeps the backend's function table out of this header,
    // and what lets a different backend define the same name with entirely
    // different contents without the game layer changing a line.
    //
    // Shader programs and their file watches live inside it, not out here - a
    // program handle is a backend resource.
    renderer *Renderer;

    // NOTE(yigit): All geometry and all textures come off disk.  A material
    // names its own texture and the loader fetches it, so nothing here is a
    // fixed list of anything.
    render_model Model;         // the subject of the scene
    render_model MarkerModel;   // a small cube drawn at each point light

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
