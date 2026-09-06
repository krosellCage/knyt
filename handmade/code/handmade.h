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

// NOTE(yigit): Included here rather than from handmade.cpp because game_state
// holds a camera by value.  It needs Pi32 and the internal macro, both defined
// above.
#include "handmade_camera.h"
// NOTE(yigit): After the Push macros above - LoadWAV allocates with PushArray.
#include "handmade_sound.h"

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
    uint32 ShaderProgram[2];
    shader_watch ShaderWatches[2];
    uint32 VAO[2];
    uint32 EBO;
    uint32 VBO[1];
    uint32 VertexCount;
    uint32 Texture[2];

    // NOTE(yigit): Everything in PermanentStorage that comes AFTER game_state
    // itself.  Set up once via InitializeArena - see the !IsInitialized block
    // in GameUpdateAndRender.
    memory_arena WorldArena;

    // Cached uniform locations for the FPS camera matrices
    int32 ViewUniformLocation;
    int32 ProjectionUniformLocation;

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
