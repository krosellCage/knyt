#if !defined(HANDMADE_PLATFORM_H)

// NOTE(yigit): Includes go ABOVE the extern "C" block - pulling a system
// header in with C linkage forced on it is asking for trouble, and nothing
// in here needs C linkage anyway (it is all typedefs and structs).
#include <stdint.h>
#include <stddef.h>
#include "handmade_opengl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef int32 bool32;

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

typedef float real32;
typedef double real64;

typedef int8 i8;
typedef int16 i16;
typedef int32 i32;
typedef int64 i64;

typedef int8 u8;
typedef int16 u16;
typedef int32 u32;
typedef int64 u64;

typedef real32 r32;
typedef real64 r64;

typedef size_t memory_index;

typedef struct thread_context
{
    int Placeholder;
} thread_context;

/*
  NOTE(yigit): Services that the platform layer provides to the game
*/
#if HANDMADE_INTERNAL
/* IMPORTANT(yigit):

   These are NOT for doing anything in the shipping game - they are
   blocking and the write doesn't protect against lost data!
*/
typedef struct debug_read_file_result
{
    uint32 ContentsSize;
    void *Contents;
} debug_read_file_result;

#define DEBUG_PLATFORM_FREE_FILE_MEMORY(name) void name(thread_context *Thread, void *Memory)
typedef DEBUG_PLATFORM_FREE_FILE_MEMORY(debug_platform_free_file_memory);

#define DEBUG_PLATFORM_READ_ENTIRE_FILE(name) debug_read_file_result name(thread_context *Thread, const char *Filename)
typedef DEBUG_PLATFORM_READ_ENTIRE_FILE(debug_platform_read_entire_file);

#define DEBUG_PLATFORM_WRITE_ENTIRE_FILE(name) bool32 name(thread_context *Thread, const char *Filename, uint32 MemorySize, void *Memory)
typedef DEBUG_PLATFORM_WRITE_ENTIRE_FILE(debug_platform_write_entire_file);

// NOTE(yigit): So the game layer can report errors without knowing what an
// OutputDebugStringA is.  Message must be null-terminated.
#define DEBUG_PLATFORM_LOG(name) void name(thread_context *Thread, const char *Message)
typedef DEBUG_PLATFORM_LOG(debug_platform_log);

// NOTE(yigit): Opaque timestamp - the game only ever compares it for equality,
// so the platform is free to pack whatever it likes in here (Win32 stuffs a
// FILETIME into the 64 bits).  Returns 0 if the file could not be stat'd.
#define DEBUG_PLATFORM_GET_FILE_WRITE_TIME(name) uint64 name(thread_context *Thread, const char *Filename)
typedef DEBUG_PLATFORM_GET_FILE_WRITE_TIME(debug_platform_get_file_write_time);

#endif

/*
  NOTE(yigit): Services that the game provides to the platform layer.
  (this may expand in the future - sound on separate thread, etc.)
*/

// FOUR THINGS - timing, controller/keyboard input, bitmap buffer to use, sound buffer to use

// TODO(yigit): In the future, rendering _specifically_ will become a three-tiered abstraction!!!
typedef struct game_offscreen_buffer
{
    // NOTE(yigit): Pixels are alwasy 32-bits wide, Memory Order BB GG RR XX
    void *Memory;
    int Width;
    int Height;
    int Pitch;
    int BytesPerPixel;
} game_offscreen_buffer;

typedef struct game_sound_output_buffer
{
    int SamplesPerSecond;
    int SampleCount;
    int16 *Samples;
} game_sound_output_buffer;

typedef struct game_button_state
{
    int HalfTransitionCount;
    bool32 EndedDown;
} game_button_state;

typedef struct game_controller_input
{
    bool32 IsConnected;
    bool32 IsAnalog;    
    real32 StickAverageX;
    real32 StickAverageY;
    
    union
    {
        game_button_state Buttons[12];
        struct
        {
            game_button_state MoveUp;
            game_button_state MoveDown;
            game_button_state MoveLeft;
            game_button_state MoveRight;
            
            game_button_state ActionUp;
            game_button_state ActionDown;
            game_button_state ActionLeft;
            game_button_state ActionRight;
            
            game_button_state LeftShoulder;
            game_button_state RightShoulder;

            game_button_state Back;
            game_button_state Start;

            // NOTE(yigit): All buttons must be added above this line
            
            game_button_state Terminator;
        };
    };
} game_controller_input;

#define MAX_CONTROLLERS 5

typedef struct game_input
{
    game_button_state MouseButtons[MAX_CONTROLLERS];
    int32 MouseX, MouseY, MouseZ;
    int32 MouseDeltaX, MouseDeltaY;

    real32 dtForFrame;

    // NOTE(yigit): Needed so the game can build a correct perspective
    // projection matrix (aspect ratio) for the FPS camera.
    int32 WindowWidth;
    int32 WindowHeight;

    game_controller_input Controllers[5];
} game_input;

struct memory_arena
{
    memory_index Size;
    uint8 *Base;
    memory_index Used;
};

typedef struct game_memory
{
    bool32 IsInitialized;

    uint64 PermanentStorageSize;
    void *PermanentStorage; // NOTE(yigit): REQUIRED to be cleared to zero at startup

    uint64 TransientStorageSize;
    void *TransientStorage; // NOTE(yigit): REQUIRED to be cleared to zero at startup

    game_opengl_api OpenGL;

#if HANDMADE_INTERNAL
    debug_platform_free_file_memory *DEBUGPlatformFreeFileMemory;
    debug_platform_read_entire_file *DEBUGPlatformReadEntireFile;
    debug_platform_write_entire_file *DEBUGPlatformWriteEntireFile;
    debug_platform_log *DEBUGPlatformLog;
    debug_platform_get_file_write_time *DEBUGPlatformGetFileWriteTime;
#endif
} game_memory;

#define GAME_UPDATE_AND_RENDER(name) void name(thread_context *Thread, game_memory *Memory, game_input *Input)
typedef GAME_UPDATE_AND_RENDER(game_update_and_render);

// NOTE(yigit): At the moment, this has to be a very fast function, it cannot be
// more than a millisecond or so.
// TODO(yigit): Reduce the pressure on this function's performance by measuring it
// or asking about it, etc.
#define GAME_GET_SOUND_SAMPLES(name) void name(thread_context *Thread, game_memory *Memory, game_sound_output_buffer *SoundBuffer)
typedef GAME_GET_SOUND_SAMPLES(game_get_sound_samples);

#ifdef __cplusplus
}
#endif

#define HANDMADE_PLATFORM_H
#endif
