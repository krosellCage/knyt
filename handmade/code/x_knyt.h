/* ========================================================================
   X11/ALSA platform layer.

   ATTRIBUTION:

   The structure of this file follows the Win32 platform layer from Casey
   Muratori's Handmade Hero (handmadehero.org).  That code is not freely
   distributable; only its architecture is mirrored here, and the Linux
   implementation below is written against X11, ALSA and dlfcn.

   Parts were also adapted from xcb_handmade by Neil Blakey-Milner and
   contributors (github.com/nxsy/xcb_handmade), which is distributable under
   the BSD license.  That project targets XCB; this one targets Xlib, so the
   windowing code differs, but the approach was a reference.

   ======================================================================== */

#if !defined(X_KNYT_H)

/*
  NOTE(yigit): X11/ALSA platform layer state.  This is the Linux counterpart to
  win32_handmade.h - it holds only what the platform layer itself needs, never
  anything the game layer can see.

  There is no offscreen bitmap in here any more.  Rendering goes through
  OpenGL (see x_opengl.h), so the old XShm software backbuffer is gone - the
  window is owned by the GL context and blitting an XImage over it would just
  fight with the swap.
*/

#include <time.h>
#include <alsa/asoundlib.h>

#include "handmade_platform.h"

#define X_STATE_FILE_NAME_LENGTH 512

// NOTE(yigit): The game's sound mixer works eight samples at a time, so the
// sample count it is handed always has to be a multiple of eight.
#define Align8(Value) (((Value) + 7) & ~7)

struct x_window_dimensions
{
    int Width;
    int Height;
};

struct x_game_code
{
    void *LibraryHandle;
    time_t LibraryMTime;
    time_t MainExecutableMTime;

    // NOTE(yigit): Either of these may be 0 if the .so failed to load - check
    // IsValid, or the pointers themselves, before calling.
    game_update_and_render *UpdateAndRender;
    game_get_sound_samples *GetSoundSamples;

    bool32 IsValid;
};

struct x_replay_buffer
{
    int FileHandle;
    char Filename[X_STATE_FILE_NAME_LENGTH];
    void *MemoryBlock;
};

struct x_state
{
    uint64 TotalSize;
    void *GameMemoryBlock;
    x_replay_buffer ReplayBuffers[2];

    int RecordingFd;
    uint8 RecordingIndex;

    int PlaybackFd;
    uint8 PlaybackIndex;

    int JoystickFDs[4];
    int FramesUntilNextJoystickCheck;

    char BinaryName[X_STATE_FILE_NAME_LENGTH];
    char *OnePastBinaryFilenameSlash;
};

struct x_sound_output
{
    snd_pcm_t *AlsaHandle;
    snd_output_t *AlsaLog;

    uint32 SamplesPerSecond;
    uint32 Channels;
    uint32 BytesPerSample;
    uint32 BufferSizeInSamples;
    uint32 BufferSizeInFrames;
    uint32 BufferSizeInBytes;
    uint32 SafetySamples;

    snd_pcm_uframes_t PeriodSize;
};

#define X_KNYT_H
#endif
