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

/*
 * TODO(yigit): This is not a final platform layer.
 *  -Examine how alsa keyboard controller functions work.
 *  -Monitor refresh rate
 * */

// NOTE(yigit): X11 and the system GL headers come first, on purpose.  Both
// they and handmade_opengl.h declare the GL types and the GLAPIENTRY macro,
// and letting the system headers get there first keeps the two definitions
// from disagreeing.  x_opengl.h pulls in handmade_platform.h behind them.
#include "x_opengl.h"

#include "handmade.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <dlfcn.h>
#include <errno.h>

#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <linux/joystick.h>

#include <sys/mman.h> // NOTE: mmap.
#include <sys/stat.h>

#include <x86intrin.h> // NOTE: __rdtsc.

#include <alsa/asoundlib.h>

#include "x_knyt.h"

// NOTE(yigit): global for now.
global_variable bool32 GlobalRunning;
global_variable bool32 GlobalPause;
global_variable int64 GlobalPerfCountFrequency;
global_variable real32 GameUpdateHz = 144.0;

// NOTE(yigit): Directory the executable lives in, slash included.  Set once by
// XGetBinaryName - see XBuildAssetPath for why.
global_variable char GlobalBinaryDirectory[X_STATE_FILE_NAME_LENGTH];

/*
  NOTE(yigit): Turns an asset name as the GAME writes it into a path this
  process can actually open.  Two things happen here.

  The game asks for "data\\container.jpg" - Windows separators, because the
  asset names are identical in both platform layers and neither one carries an
  #ifdef for this.  Backslashes become slashes on the way in.

  More importantly the name is resolved against the EXECUTABLE's directory, not
  against the current working directory.  A relative "data/..." is only correct
  when the process happens to have been started from the build directory; run
  the binary from anywhere else - out of an editor, off a find -exec, by
  absolute path from $HOME - and every asset silently fails to open.  Missing
  textures are especially quiet about it: GameLoadTexture returns 0, the game
  binds texture object 0, and sampling an unbound texture in a core profile is
  defined to give opaque black.  No GL error, no warning, just a black quad.
  The replay files already key off the binary's directory (XBuildFullFilename);
  assets now do too, so where you launch from stops mattering.
*/
internal void
XBuildAssetPath(const char *Filename, char *Dest, size_t DestCount)
{
    size_t At = 0;

    if((Filename[0] != '/') && GlobalBinaryDirectory[0])
    {
        for(;
            GlobalBinaryDirectory[At] && (At < (DestCount - 1));
            ++At)
        {
            Dest[At] = GlobalBinaryDirectory[At];
        }
    }

    for(size_t Index = 0;
        Filename[Index] && (At < (DestCount - 1));
        ++Index, ++At)
    {
        Dest[At] = (Filename[Index] == '\\') ? '/' : Filename[Index];
    }

    Dest[At] = 0;
}

#if HANDMADE_INTERNAL
DEBUG_PLATFORM_LOG(DEBUGPlatformLog)
{
    fputs(Message, stderr);
}

DEBUG_PLATFORM_GET_FILE_WRITE_TIME(DEBUGPlatformGetFileWriteTime)
{
    uint64 Result = 0;

    char Path[X_STATE_FILE_NAME_LENGTH];
    XBuildAssetPath(Filename, Path, sizeof(Path));

    struct stat Stat;
    if(stat(Path, &Stat) == 0)
    {
        // NOTE(yigit): The game only ever compares these for equality, so any
        // packing will do as long as it changes when the file does.  Seconds
        // alone is too coarse to catch two saves inside the same second.
        Result = ((uint64)Stat.st_mtim.tv_sec * 1000000000ull) + (uint64)Stat.st_mtim.tv_nsec;
    }

    return(Result);
}

DEBUG_PLATFORM_WRITE_ENTIRE_FILE(DEBUGPlatformWriteEntireFile)
{
    bool32 Result = false;

    char Path[X_STATE_FILE_NAME_LENGTH];
    XBuildAssetPath(Filename, Path, sizeof(Path));

    FILE *FileHandle = fopen(Path, "wb");
    if(FileHandle)
    {
        size_t BytesWritten = fwrite(Memory, 1, MemorySize, FileHandle);
        if(BytesWritten == MemorySize)
        {
            Result = true;
        }
        else
        {
            // TODO: Logging
        }

        fclose(FileHandle);
    }
    else
    {
        // TODO: Logging
    }

    return(Result);
}

DEBUG_PLATFORM_FREE_FILE_MEMORY(DEBUGPlatformFreeFileMemory)
{
    // NOTE(yigit): The game frees without telling us how big the block was, so
    // the read below has to use an allocator that remembers - hence malloc
    // rather than mmap.
    if(Memory)
    {
        free(Memory);
    }
}

DEBUG_PLATFORM_READ_ENTIRE_FILE(DEBUGPlatformReadEntireFile)
{
    debug_read_file_result Result = {};

    char Path[X_STATE_FILE_NAME_LENGTH];
    XBuildAssetPath(Filename, Path, sizeof(Path));

    int FileDescriptor = open(Path, O_RDONLY);
    if(FileDescriptor != -1)
    {
        struct stat Stat;
        if(fstat(FileDescriptor, &Stat) == 0)
        {
            uint32 FileSize32 = SafeTruncateUInt64(Stat.st_size);

            void *Contents = malloc(FileSize32);
            if(Contents)
            {
                // NOTE(yigit): read() is allowed to come back short on a
                // signal or a big file, so keep going until the whole thing
                // is in or something actually fails.
                uint32 BytesRemaining = FileSize32;
                uint8 *At = (uint8 *)Contents;
                while(BytesRemaining)
                {
                    ssize_t BytesRead = read(FileDescriptor, At, BytesRemaining);
                    if(BytesRead <= 0)
                    {
                        break;
                    }

                    At += BytesRead;
                    BytesRemaining -= (uint32)BytesRead;
                }

                if(BytesRemaining == 0)
                {
                    Result.Contents = Contents;
                    Result.ContentsSize = FileSize32;
                }
                else
                {
                    free(Contents);
                }
            }
        }

        close(FileDescriptor);
    }
    else
    {
        // TODO: Logging (File not found)
    }

    return(Result);
}
#endif

internal void
XGetBinaryName(x_state *State)
{
    // NOTE(nbm): There are probably pathological cases where this won't work - for
    // example if the underlying file has been removed/moved.
    ssize_t Length = readlink("/proc/self/exe", State->BinaryName, sizeof(State->BinaryName) - 1);
    if(Length < 0)
    {
        Length = 0;
    }
    State->BinaryName[Length] = 0;

    State->OnePastBinaryFilenameSlash = State->BinaryName;
    for (char *Scan = State->BinaryName; *Scan; ++Scan)
    {
        if (*Scan == '/')
        {
            State->OnePastBinaryFilenameSlash = Scan + 1;
        }
    }

    // NOTE(yigit): The debug file calls reach for this and they do not get an
    // x_state, so it is kept apart from State.  Trailing slash included, so
    // XBuildAssetPath can just concatenate.
    size_t DirectoryLength = (size_t)(State->OnePastBinaryFilenameSlash - State->BinaryName);
    if(DirectoryLength >= sizeof(GlobalBinaryDirectory))
    {
        DirectoryLength = sizeof(GlobalBinaryDirectory) - 1;
    }
    memcpy(GlobalBinaryDirectory, State->BinaryName, DirectoryLength);
    GlobalBinaryDirectory[DirectoryLength] = 0;
}

internal void
XCatStrings(
    size_t SrcACount, char *SrcA,
    size_t SrcBCount, char *SrcB,
    size_t DestCount, char *Dest
    )
{
    size_t Counter = 0;
    for (
            size_t i = 0;
            i < SrcACount && Counter++ < DestCount;
            ++i)
    {
        *Dest++ = *SrcA++;
    }
    for (
            size_t i = 0;
            i < SrcBCount && Counter++ < DestCount;
            ++i)
    {
        *Dest++ = *SrcB++;
    }

    *Dest++ = 0;
}

internal void
XBuildFullFilename(x_state *State, char *Filename, int DestCount, char *Dest)
{
    XCatStrings(State->OnePastBinaryFilenameSlash -
            State->BinaryName, State->BinaryName,
            strlen(Filename), Filename,
            DestCount, Dest);
}

internal void
XGetInputFileLocation(x_state *State, bool32 InputStream, uint Index, int DestSize, char *Dest)
{
    char Temp[64];
    sprintf(Temp, "loop_%d_%s.hmi", Index, InputStream ? "input" : "state");
    XBuildFullFilename(State,
            Temp,
            DestSize,
            Dest);
}

internal x_replay_buffer *
XGetReplayBuffer(x_state *State, uint8 Index)
{
    Assert(Index < ArrayCount(State->ReplayBuffers));
    x_replay_buffer *Result = &State->ReplayBuffers[Index];
    return Result;
}

internal void
XInitReplays(x_state *State)
{
    for (uint8 Index = 0;
            Index < ArrayCount(State->ReplayBuffers);
            ++Index)
    {
        x_replay_buffer *ReplayBuffer = &State->ReplayBuffers[Index];

        XGetInputFileLocation(State, false, Index,
                sizeof(ReplayBuffer->Filename), ReplayBuffer->Filename);

        ReplayBuffer->FileHandle = open(ReplayBuffer->Filename,
                O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);

        int TruncateSucceeded = ftruncate(ReplayBuffer->FileHandle, State->TotalSize);
        if (TruncateSucceeded == -1)
        {
            perror("ftruncate");
        }

        ReplayBuffer->MemoryBlock = mmap(0, State->TotalSize,
                PROT_READ | PROT_WRITE,
                MAP_SHARED, // | MAP_POPULATE,
                ReplayBuffer->FileHandle, 0);

        if (ReplayBuffer->MemoryBlock == MAP_FAILED)
        {
            perror("mmap");
            ReplayBuffer->MemoryBlock = 0;
        }
    }
}

internal void
XStartRecording(x_state *State, uint8 Index)
{
    x_replay_buffer *ReplayBuffer = XGetReplayBuffer(State, Index);
    if (ReplayBuffer->MemoryBlock)
    {
        State->RecordingIndex = Index;
        char Filename[X_STATE_FILE_NAME_LENGTH];
        XGetInputFileLocation(State, true, Index, sizeof(Filename), Filename);
        State->RecordingFd = open(Filename, O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);

        memcpy(ReplayBuffer->MemoryBlock, State->GameMemoryBlock, State->TotalSize);
    }
}

internal void
XStopRecording(x_state *State)
{
    close(State->RecordingFd);
    State->RecordingIndex = 0;
    State->RecordingFd = 0;
}

internal void
XStartPlayback(x_state *State, uint8 Index)
{
    x_replay_buffer *ReplayBuffer = XGetReplayBuffer(State, Index);
    if (ReplayBuffer->MemoryBlock)
    {
        State->PlaybackIndex = Index;
        char Filename[X_STATE_FILE_NAME_LENGTH];
        XGetInputFileLocation(State, true, Index, sizeof(Filename), Filename);
        State->PlaybackFd = open(Filename, O_RDONLY);
        memcpy(State->GameMemoryBlock, ReplayBuffer->MemoryBlock, State->TotalSize);
    }
}

internal void
XStopPlayback(x_state *State)
{
    close(State->PlaybackFd);
    State->PlaybackIndex = 0;
    State->PlaybackFd = 0;
}

internal void
XRecordInput(x_state *State, game_input *NewInput)
{
    write(State->RecordingFd, NewInput, sizeof(*NewInput));
}

internal void
XPlaybackInput(x_state *State, game_input *NewInput)
{
    int BytesRead = read(State->PlaybackFd, NewInput, sizeof(*NewInput));
    if (BytesRead == 0)
    {
        uint8 Index = State->PlaybackIndex;
        XStopPlayback(State);
        XStartPlayback(State, Index);
        read(State->PlaybackFd, NewInput, sizeof(*NewInput));
    }
}

internal void
XLoadGame(x_game_code *GameCode, char *Path)
{
    struct stat StatBuf = {};
    uint32_t StatResult = stat(Path, &StatBuf);
    if (StatResult != 0)
    {
        printf("Failed to stat game code at %s", Path);
        return;
    }
    GameCode->LibraryMTime = StatBuf.st_mtime;

    GameCode->IsValid = false;
    GameCode->LibraryHandle = dlopen(Path, RTLD_LAZY);
    if (GameCode->LibraryHandle == 0)
    {
        char *Error = dlerror();
        printf("Unable to load library at path %s: %s\n", Path, Error);
        return;
    }

    GameCode->UpdateAndRender =
        (game_update_and_render *)dlsym(GameCode->LibraryHandle, "GameUpdateAndRender");
    if (GameCode->UpdateAndRender == 0)
    {
        char *Error = dlerror();
        printf("Unable to load symbol GameUpdateAndRender: %s\n", Error);
        return;
    }

    GameCode->GetSoundSamples =
        (game_get_sound_samples *)dlsym(GameCode->LibraryHandle, "GameGetSoundSamples");
    if (GameCode->GetSoundSamples == 0)
    {
        char *Error = dlerror();
        printf("Unable to load symbol GameGetSoundSamples: %s\n", Error);
        return;
    }

    GameCode->IsValid = (GameCode->LibraryHandle &&
                         GameCode->UpdateAndRender &&
                         GameCode->GetSoundSamples);
}

internal void
XUnloadGame(x_game_code *GameCode)
{
    if (GameCode->LibraryHandle)
    {
        dlclose(GameCode->LibraryHandle);
        GameCode->LibraryHandle = 0;
    }
    GameCode->IsValid = false;
    GameCode->UpdateAndRender = 0;
    GameCode->GetSoundSamples = 0;
}
internal real32
XProcessControllerAxis(int16 Value, uint16 DeadZone, bool Inverted)
{
    if (Inverted)
    {
        Value = -Value;
    }

    if (abs(Value) < DeadZone)
    {
        return 0;
    }
    else if (Value < -DeadZone)
    {
        return (real32)((Value + DeadZone) / (32767.0f - DeadZone));
    }

    return (real32)((Value - DeadZone) / (32767.0f - DeadZone));
}

internal void
XProcessDigitalButton(bool Down, game_button_state *OldState, game_button_state *NewState)
{
    NewState->EndedDown = Down;
    NewState->HalfTransitionCount = (OldState->EndedDown != NewState->EndedDown) ? 1 : 0;
}

internal void
XProcessKeyboardMessage(game_button_state *NewState, bool32 IsDown)
{
    if (NewState->EndedDown != IsDown)
    {
        NewState->EndedDown = IsDown;
        ++NewState->HalfTransitionCount;
    }
}

internal void
XCheckControllerPlug(x_state *State)
{
    if (State->FramesUntilNextJoystickCheck-- <= 0)
    {
        // Try to fill any empty slots
        for (int i = 0; i < 4; ++i)
        {
            if (State->JoystickFDs[i] == -1)
            {
                char DevicePath[32];
                snprintf(DevicePath, sizeof(DevicePath), "/dev/input/js%d", i);

                // Attempt to open without blocking the game
                int FD = open(DevicePath, O_RDONLY | O_NONBLOCK);
                if (FD != -1)
                {
                    State->JoystickFDs[i] = FD;
                    printf("Hot-plugged Joystick %d\n", i);
                }
            }
        }
        // Reset timer (e.g., check once every 120 frames / ~2 seconds)
        State->FramesUntilNextJoystickCheck = 120;
    }
}

internal void
XInitJoysticks(x_state *State)
{
    for (int ControllerIndex = 0;
         ControllerIndex < ArrayCount(State->JoystickFDs);
         ++ControllerIndex)
    {
        State->JoystickFDs[ControllerIndex] = -1;

        char DevicePath[64];
        // Linux joystick device nodes are typically /dev/input/jsX
        snprintf(DevicePath, sizeof(DevicePath), "/dev/input/js%d", ControllerIndex);

        int FD = open(DevicePath, O_RDONLY | O_NONBLOCK);
        if (FD != -1)
        {
            State->JoystickFDs[ControllerIndex] = FD;
            // Useful for verifying which pads are picked up
            printf("Joystick %d connected: %s\n", ControllerIndex, DevicePath);
        }
    }
}

internal void
XCloseJoysticks(x_state *State)
{
    for (int i = 0; i < ArrayCount(State->JoystickFDs); ++i)
    {
        if (State->JoystickFDs[i] != -1)
        {
            close(State->JoystickFDs[i]);
            State->JoystickFDs[i] = -1;
        }
    }
}

inline timespec
XGetWallClock(void)
{
    timespec Result = {};
    clock_gettime(CLOCK_MONOTONIC, &Result);
    return Result;
}

inline real32
XGetSecondsElapsed(timespec Start, timespec End)
{
    uint32 WholeSeconds = End.tv_sec - Start.tv_sec;
    real32 PartialSeconds = (End.tv_nsec - Start.tv_nsec) / 1000000000.0f;
    real32 Result = (real32)WholeSeconds + PartialSeconds;

    return Result;
}

inline bool32
XIsSleepGranular(void)
{
    timespec Result;
    clock_getres(CLOCK_MONOTONIC, &Result);

    bool32 SleepIsGranular = (Result.tv_nsec <= 1000000);
    return SleepIsGranular;
}

internal int16*
XAllocateSoundBuffer(x_sound_output *SoundOutput)
{
    uint32 MaxPossibleOverrun = 2 * 8 * sizeof(uint16);
    uint32 TotalSize = SoundOutput->BufferSizeInBytes + MaxPossibleOverrun;

    void* Result = mmap(0, TotalSize,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS,
                        -1, 0);

    if(Result == MAP_FAILED)
    {
        fprintf(stderr, "ERROR: Sound buffer mmap failed: %s\n", strerror(errno));
        return 0;
    }

    return (int16 *)Result;
}

internal void
XInitAlsa(x_sound_output *SoundOutput)
{
    // NOTE: "hw:0,0" doesn't seem to work with alsa running on top of pulseaudio
    char *Device = (char *)"default";
    // char *Device = (char *)"hw:0,0";
    snd_pcm_uframes_t AlsaBufferSizeFrames = (snd_pcm_uframes_t)SoundOutput->BufferSizeInSamples;
    int Error;
    snd_pcm_sframes_t Frames;
    Error = snd_output_stdio_attach(&SoundOutput->AlsaLog, stderr, 0);

    if ((Error = snd_pcm_open(&SoundOutput->AlsaHandle, Device, SND_PCM_STREAM_PLAYBACK, 0)) < 0)
    {
        printf("Playback open error: %s\n", snd_strerror(Error));
        exit(EXIT_FAILURE);
    }

    snd_pcm_hw_params_t *HwParams;
    snd_pcm_hw_params_alloca(&HwParams);
    printf("\nSETTING HWPARAMS\n");
    snd_pcm_hw_params_any(SoundOutput->AlsaHandle, HwParams);

    Error = snd_pcm_hw_params_set_access(SoundOutput->AlsaHandle, HwParams, SND_PCM_ACCESS_RW_INTERLEAVED);
    if(!Error)
        printf("set access: SND_PCM_ACCESS_RW_INTERLEAVED\n");

    Error = snd_pcm_hw_params_set_format(SoundOutput->AlsaHandle, HwParams, SND_PCM_FORMAT_S16_LE);
    if(!Error)
        printf("set format: SND_PCM_FORMAT_S16_LE\n");

    Error = snd_pcm_hw_params_set_channels(SoundOutput->AlsaHandle, HwParams, SoundOutput->Channels);
    if(!Error)
        printf("set channels: %d\n", SoundOutput->Channels);

    Error = snd_pcm_hw_params_set_rate(SoundOutput->AlsaHandle, HwParams, SoundOutput->SamplesPerSecond, 0);
    if(!Error)
        printf("set rate: %d\n", SoundOutput->SamplesPerSecond);

    // NOTE: "snd_pcm_hw_params_set_buffer_size" takes a buffer size
    // parameter in frames, what alsa calls a multi channel sample)
    Error = snd_pcm_hw_params_set_buffer_size_near(SoundOutput->AlsaHandle, HwParams, &AlsaBufferSizeFrames);
    if(!Error)
        printf("set buffer size: %d\n", SoundOutput->BufferSizeInSamples);

    Error = snd_pcm_hw_params(SoundOutput->AlsaHandle, HwParams);
    if(!Error)
        printf("SET HWPARAMS and now in SND_PCM_STATE_PREPARED\n\n");

    snd_pcm_hw_params_get_period_size(HwParams, &SoundOutput->PeriodSize, 0);
    // NOTE: The interrupt update frequency is given in this
    // log dump, as the period size/time.
    snd_pcm_dump(SoundOutput->AlsaHandle, SoundOutput->AlsaLog);

    SoundOutput->BufferSizeInSamples = (uint32)AlsaBufferSizeFrames;
    SoundOutput->BufferSizeInFrames  = (uint32)AlsaBufferSizeFrames;
    SoundOutput->BufferSizeInBytes   = SoundOutput->BufferSizeInFrames * SoundOutput->BytesPerSample;
}

internal int32
GetALSASampleCountToFill(x_sound_output *Output)
{
    snd_pcm_sframes_t Avail = 0;
    snd_pcm_sframes_t Delay = 0;
    // Query the ring buffer state
    snd_pcm_avail_delay(Output->AlsaHandle, &Avail, &Delay);

    int32 SamplesToFill = 0;
    int32 ExpectedSamplesPerFrame = (Output->SamplesPerSecond / GameUpdateHz);
    int32 SamplesInBuffer = Output->BufferSizeInSamples - (int32)Avail;

    Assert(SamplesInBuffer >= 0);

    /* If Avail == BufferSize, the buffer is empty (startup or underrun).
       We need a lead-in to prevent immediate stuttering.
    */
    if(Avail == Output->BufferSizeInSamples)
    {
        SamplesToFill = (ExpectedSamplesPerFrame * 2) + Output->SafetySamples;
    }
    else
    {
        /* Feedback loop:
           We want to stay around 4 periods of latency.
           If we have more, we write less. If we have less, we write more.
        */
        int32 TargetLatency = (int32)Output->PeriodSize * 4;
        int32 SampleAdjustment = (SamplesInBuffer - TargetLatency) / 2;

        SamplesToFill = ExpectedSamplesPerFrame - SampleAdjustment;
    }

    // Clamp to ensure we never return a negative write size
    if(SamplesToFill < 0)
    {
        SamplesToFill = 0;
    }

    return SamplesToFill;
}

internal void
XCopyBufferToAlsa(x_sound_output *SoundOutput, void *SamplesToDevice, uint32 FrameCount)
{
    // 1. Absolute Pointer Safety
    if (!SoundOutput || !SoundOutput->AlsaHandle || !SamplesToDevice) return;

    // 2. Try to write
    snd_pcm_sframes_t WrittenFrames = snd_pcm_writei(SoundOutput->AlsaHandle, SamplesToDevice, FrameCount);

    // 3. Handle Errors and Stalls
    if (WrittenFrames < 0)
    {
        fprintf(stderr, "ALSA Error: %s\n", snd_strerror(WrittenFrames));

        // Recover handles Underruns (-EPIPE) and Suspensions (-ESTRPIPE)
        snd_pcm_recover(SoundOutput->AlsaHandle, WrittenFrames, 1);
    }
    else if (WrittenFrames == 0)
    {
        // If we wrote 0, the device is likely "stuck" or in a transition state.
        // We call prepare to kick it back into gear.
        snd_pcm_state_t state = snd_pcm_state(SoundOutput->AlsaHandle);
        if (state == SND_PCM_STATE_XRUN) {
            snd_pcm_prepare(SoundOutput->AlsaHandle);
        }
    }
    else if (WrittenFrames < (snd_pcm_sframes_t)FrameCount)
    {
        // Optional: Handle partial writes by offsetting the pointer and trying again
        // but for Handmade dev, a log is usually enough for now.
        fprintf(stderr, "ALSA Short Write: %ld/%u\n", WrittenFrames, FrameCount);
    }
}

internal Status
XToggleFullscreen(Display* ClientDisplay, Window ClientWindow)
{
    XClientMessageEvent Event = {};
    Atom WmState    = XInternAtom(ClientDisplay, "_NET_WM_STATE", False);
    Atom Fullscreen = XInternAtom(ClientDisplay, "_NET_WM_STATE_FULLSCREEN", False);

    if(WmState == None || Fullscreen == None) return 0;

    Event.type = ClientMessage;
    Event.format = 32;
    Event.window = ClientWindow;
    Event.message_type = WmState;

    // Action: 2 is _NET_WM_STATE_TOGGLE
    // Action: 1 is _NET_WM_STATE_ADD (Force fullscreen)
    // Action: 0 is _NET_WM_STATE_REMOVE (Exit fullscreen)
    Event.data.l[0] = 2;

    Event.data.l[1] = Fullscreen;
    Event.data.l[2] = 0;
    Event.data.l[3] = 1;

    return XSendEvent(ClientDisplay, DefaultRootWindow(ClientDisplay),
            False, SubstructureRedirectMask | SubstructureNotifyMask, (XEvent *)&Event);
}

internal Status
XToggleMaximize(Display* ClientDisplay, Window ClientWindow)
{
    XClientMessageEvent Event = {};
    Atom WmState = XInternAtom(ClientDisplay, "_NET_WM_STATE", False);
    Atom MaxH  =  XInternAtom(ClientDisplay, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    Atom MaxV  =  XInternAtom(ClientDisplay, "_NET_WM_STATE_MAXIMIZED_VERT", False);

    if(WmState == None) return 0;

    Event.type = ClientMessage;
    Event.format = 32;
    Event.window = ClientWindow;
    Event.message_type = WmState;
    Event.data.l[0] = 2;
    Event.data.l[1] = MaxH;
    Event.data.l[2] = MaxV;
    Event.data.l[3] = 1;

    return XSendEvent(ClientDisplay, DefaultRootWindow(ClientDisplay),
            False, SubstructureNotifyMask, (XEvent *)&Event);
}

internal void
XSetSizeHint(Display* ClientDisplay, Window ClientWindow,
            int MinWidth, int MinHeight,
            int MaxWidth, int MaxHeight)
{
  XSizeHints Hints = {};
  if(MinWidth > 0 && MinHeight > 0) Hints.flags |= PMinSize;
  if(MaxWidth > 0 && MaxHeight > 0) Hints.flags |= PMaxSize;

  Hints.min_width = MinWidth;
  Hints.min_height = MinHeight;
  Hints.max_width = MaxWidth;
  Hints.max_height = MaxHeight;

  XSetWMNormalHints(ClientDisplay, ClientWindow, &Hints);
}

internal void
XProcessJoystickEvents(x_state *State, game_input *OldInput, game_input *NewInput)
{
    for (uint32 ControllerIndex = 0; ControllerIndex < 4; ++ControllerIndex)
    {
        uint32 OurControllerIndex = ControllerIndex + 1;
        game_controller_input *OldController = &OldInput->Controllers[OurControllerIndex];
        game_controller_input *NewController = &NewInput->Controllers[OurControllerIndex];

        *NewController = *OldController;

        NewController->IsConnected = false;

        for(int ButtonIndex = 0; ButtonIndex < ArrayCount(NewController->Buttons); ++ButtonIndex)
        {
            NewController->Buttons[ButtonIndex].HalfTransitionCount = 0;
        }

        int FD = State->JoystickFDs[ControllerIndex];
        if (FD >= 0)
        {
            NewController->IsConnected = true;
            struct js_event Event;

            ssize_t BytesRead;
            while ((BytesRead = read(FD, &Event, sizeof(Event))) > 0)
            {
                uint8_t Type = Event.type & ~JS_EVENT_INIT;

                if (Type == JS_EVENT_BUTTON)
                {
                    bool32 Down = (Event.value != 0);
                    switch (Event.number)
                    {
                        case 0: XProcessDigitalButton(Down, &OldController->ActionDown, &NewController->ActionDown); break;
                        case 1: XProcessDigitalButton(Down, &OldController->ActionRight, &NewController->ActionRight); break;
                        case 2: XProcessDigitalButton(Down, &OldController->ActionLeft, &NewController->ActionLeft); break;
                        case 3: XProcessDigitalButton(Down, &OldController->ActionUp, &NewController->ActionUp); break;
                        case 4: XProcessDigitalButton(Down, &OldController->LeftShoulder, &NewController->LeftShoulder); break;
                        case 5: XProcessDigitalButton(Down, &OldController->RightShoulder, &NewController->RightShoulder); break;
                        case 6: XProcessDigitalButton(Down, &OldController->Back, &NewController->Back); break;
                        case 7: XProcessDigitalButton(Down, &OldController->Start, &NewController->Start); break;
                    }
                }
                else if (Type == JS_EVENT_AXIS)
                {
                    uint16 DeadZone = 7849;
                    if (Event.number == 0)      NewController->StickAverageX = XProcessControllerAxis(Event.value, DeadZone, false);
                    else if (Event.number == 1) NewController->StickAverageY = XProcessControllerAxis(Event.value, DeadZone, true);

                    if((NewController->StickAverageX != 0.0f) || (NewController->StickAverageY != 0.0f))
                    {
                        NewController->IsAnalog = true;
                    }

                    else if (Event.number == 6) // Hat X
                    {
                        XProcessDigitalButton((Event.value < -16000), &OldController->MoveLeft, &NewController->MoveLeft);
                        XProcessDigitalButton((Event.value > 16000), &OldController->MoveRight, &NewController->MoveRight);
                    }
                    else if (Event.number == 7) // Hat Y
                    {
                        XProcessDigitalButton((Event.value < -16000), &OldController->MoveUp, &NewController->MoveUp);
                        XProcessDigitalButton((Event.value > 16000), &OldController->MoveDown, &NewController->MoveDown);
                    }
                }
            }

            if (BytesRead == -1 && errno != EAGAIN)
            {
                // The device node is likely gone (unplugged)
                close(State->JoystickFDs[ControllerIndex]);
                State->JoystickFDs[ControllerIndex] = -1;

                // Clear the state so the game doesn't keep using "Old" values
                *NewController = {};
                NewController->IsConnected = false;

                printf("Joystick %d disconnected.\n", ControllerIndex);
            }
        }
        else
        {
            // If the FD was -1 to begin with, ensure the controller
            // state is zeroed out for the game.
            *NewController = {};
            NewController->IsConnected = false;
        }
    }
}

internal void
XProcessEvents(x_state *State, Display* ClientDisplay, Window ClientWindow,
               x_window_dimensions* WindowDimensions,
               game_controller_input* OldKeyboardController,
               game_controller_input* NewKeyboardController,
               game_input *NewInput, game_input *OldInput)
{

    NewInput->MouseX = OldInput->MouseX;
    NewInput->MouseY = OldInput->MouseY;
    NewInput->MouseZ = OldInput->MouseZ;

    for(uint32 ButtonIndex = 0;
            ButtonIndex < 5; // FIXME(yigit): This is hard coded for now.
            ++ButtonIndex)
    {
        NewInput->MouseButtons[ButtonIndex] = OldInput->MouseButtons[ButtonIndex];
        NewInput->MouseButtons[ButtonIndex].HalfTransitionCount = 0;
    }
    while(XPending(ClientDisplay))
    {
        XEvent Event;
        XNextEvent(ClientDisplay, &Event);

        switch(Event.type)
        {
            case DestroyNotify:
            {
                XDestroyWindowEvent *e = (XDestroyWindowEvent *)&Event;
                if(e->window == ClientWindow) GlobalRunning = false;
            } break;

            case ClientMessage:
            {
                // NOTE(yigit): The window manager telling us the user hit the
                // close button.  Without this the X server just kills the
                // connection and Xlib exits the process out from under us.
                Atom WmDeleteWindow = XInternAtom(ClientDisplay, "WM_DELETE_WINDOW", False);
                if((Atom)Event.xclient.data.l[0] == WmDeleteWindow)
                {
                    GlobalRunning = false;
                }
            } break;

            case ConfigureNotify:
            {
                XConfigureEvent ConfigureEvent = Event.xconfigure;
                // NOTE(yigit): Nothing to reallocate any more - the GL
                // framebuffer resizes with the window on its own, and the
                // viewport is set from these numbers at present time.
                WindowDimensions->Width = ConfigureEvent.width;
                WindowDimensions->Height = ConfigureEvent.height;
            } break;

            case KeyPress:
            case KeyRelease:
            {
                bool32 IsDown = (Event.type == KeyPress);
                KeySym KeySymbol = XLookupKeysym(&Event.xkey, 0);

                if (Event.type == KeyRelease && XPending(ClientDisplay))
                {
                    XEvent NextEvent;
                    XPeekEvent(ClientDisplay, &NextEvent);
                    if (NextEvent.type == KeyPress &&
                        NextEvent.xkey.time == Event.xkey.time &&
                        NextEvent.xkey.keycode == Event.xkey.keycode)
                    {
                        XNextEvent(ClientDisplay, &NextEvent);
                        continue;
                    }
                }
                if (KeySymbol == XK_w || KeySymbol == XK_Up) XProcessDigitalButton(IsDown,
                        &OldKeyboardController->MoveUp, &NewKeyboardController->MoveUp);
                else if (KeySymbol == XK_a || KeySymbol == XK_Left) XProcessDigitalButton(IsDown,
                        &OldKeyboardController->MoveLeft, &NewKeyboardController->MoveLeft);
                else if (KeySymbol == XK_s || KeySymbol == XK_Down) XProcessDigitalButton(IsDown,
                        &OldKeyboardController->MoveDown, &NewKeyboardController->MoveDown);
                else if (KeySymbol == XK_d || KeySymbol == XK_Right) XProcessDigitalButton(IsDown,
                        &OldKeyboardController->MoveRight, &NewKeyboardController->MoveRight);
                else if (KeySymbol == XK_q) XProcessDigitalButton(IsDown, &OldKeyboardController->LeftShoulder,
                        &NewKeyboardController->LeftShoulder);
                else if (KeySymbol == XK_e) XProcessDigitalButton(IsDown, &OldKeyboardController->RightShoulder,
                        &NewKeyboardController->RightShoulder);
                else if (KeySymbol == XK_Return) XProcessDigitalButton(IsDown, &OldKeyboardController->Start,
                        &NewKeyboardController->Start);
                else if (KeySymbol == XK_BackSpace) XProcessDigitalButton(IsDown, &OldKeyboardController->Back,
                        &NewKeyboardController->Back);
                else if (KeySymbol == XK_space) XProcessDigitalButton(IsDown, &OldKeyboardController->ActionDown,
                        &NewKeyboardController->ActionDown);
                else if (KeySymbol == XK_Escape) GlobalRunning = false;

                else if (KeySymbol == XK_p)
                {
                    if(IsDown)
                    {
                        GlobalPause = !GlobalPause;
                    }
                }
                else if (KeySymbol == XK_F11)
                {
                    // NOTE(yigit): Guarded on IsDown so autorepeat cannot
                    // toggle this once per repeat while the key is held.
                    //
                    // Unlike the Win32 side, we do not resize the window
                    // ourselves - we ask the window manager to do it, via the
                    // EWMH _NET_WM_STATE_FULLSCREEN hint.  The WM owns window
                    // geometry on X11, so it also remembers where the window
                    // was and puts it back when toggled off.  Nothing here has
                    // to save the old placement.
                    if(IsDown)
                    {
                        XToggleFullscreen(ClientDisplay, ClientWindow);
                    }
                }
                else if (KeySymbol == XK_l) // FIXME(yigit): Segfault only in release mode after this line.
                {
                    if(IsDown)
                    {
                        if (State->PlaybackIndex == 0)
                        {
                            if (State->RecordingIndex == 0)
                            {
                                XStartRecording(State, 1);
                            }
                            else
                            {
                                XStopRecording(State);
                                XStartPlayback(State, 1);
                            }
                        }
                        else
                        {
                            XStopPlayback(State);
                        }
                    }
                }
            } break;

            case ButtonPress:
            case ButtonRelease:
            {

                XButtonEvent *E = (XButtonEvent *)&Event;
                bool32 IsDown = (Event.type == ButtonPress);
                if ((E->button >= 1) && (E->button <= 5))
                {
                    uint32 MouseButtonIndex = (E->button - 1);
                    XProcessKeyboardMessage(&NewInput->MouseButtons[MouseButtonIndex], IsDown);
                }
                // Button 2 is middle click, Button 4/5 are typically scroll wheel
            } break;

            case MotionNotify:
            {
                // These are relative to the top-left of the ClientWindow
                NewInput->MouseX = Event.xmotion.x;
                NewInput->MouseY = Event.xmotion.y;
            } break;
        }
    }
}

int main()
{
    x_state State = {};
    XGetBinaryName(&State);

    char MainExecutablePath[X_STATE_FILE_NAME_LENGTH];
    char *MainExecutableFilename = (char *)"x_knyt";

    XBuildFullFilename(&State, MainExecutableFilename,
            sizeof(MainExecutablePath),MainExecutablePath);

    char SourceGameCodeLibraryPath[X_STATE_FILE_NAME_LENGTH];
    char *GameCodeFilename = (char *)"debug/libknyt.so";

    XBuildFullFilename(&State,GameCodeFilename,
            sizeof(SourceGameCodeLibraryPath),
            SourceGameCodeLibraryPath);

    x_game_code GameCode = {};

    struct stat MainExecutableStatBuf = {};

    if(stat(MainExecutablePath, &MainExecutableStatBuf) == 0)
    {
        GameCode.MainExecutableMTime = MainExecutableStatBuf.st_mtime;
    }
    else
    {
        // Note: Using %s requires including <stdio.h>
        printf("Couldn't stat main executable: %s\n", MainExecutablePath);
    }

    XLoadGame(&GameCode, SourceGameCodeLibraryPath);

    GlobalRunning = true;

    x_window_dimensions WindowDimensions = {};
    WindowDimensions.Width = 1020;
    WindowDimensions.Height = 540;

    Display* ClientDisplay = XOpenDisplay(0);

    if(!ClientDisplay)
    {
        printf("No display available\n");
        exit(1);
    }

    ::Window RootWindow = DefaultRootWindow(ClientDisplay);
    int DefaultScreen = DefaultScreen(ClientDisplay);

    // NOTE(yigit): The visual is not ours to pick any more - GLX hands us the
    // one that belongs to the framebuffer config we are going to render into,
    // and the window has to be created with exactly that visual or
    // glXMakeCurrent fails with BadMatch.
    x_opengl_context OpenGLContext = {};
    if(!XChooseGLVisual(&OpenGLContext, ClientDisplay, DefaultScreen))
    {
        fprintf(stderr, "FATAL: Could not find a usable OpenGL visual - aborting.\n");
        exit(1);
    }
    XVisualInfo *VisualInfo = OpenGLContext.VisualInfo;

    XSetWindowAttributes WindowAttributes = {};
    WindowAttributes.background_pixel = 0;
    WindowAttributes.border_pixel = 0;
    WindowAttributes.event_mask = StructureNotifyMask;
    WindowAttributes.colormap = XCreateColormap(ClientDisplay, RootWindow,
            VisualInfo->visual, AllocNone);
    uint64 AttributeMask = CWBackPixel | CWColormap | CWBorderPixel | CWEventMask;


    //NOTE(yigit): Window is an unsigned int.
    ::Window ClientWindow = XCreateWindow(ClientDisplay, RootWindow,
            0, 0,
            WindowDimensions.Width, WindowDimensions.Height, 0,
            VisualInfo->depth, InputOutput,
            VisualInfo->visual, AttributeMask, &WindowAttributes);

    if(!ClientWindow)
    {
        printf("Window wasn't created properly\n");
        exit(1);
    }

    XSelectInput(ClientDisplay, ClientWindow,
            KeyPressMask | KeyReleaseMask |
            StructureNotifyMask | ExposureMask |
            ButtonPressMask | ButtonReleaseMask | PointerMotionMask);

    // NOTE(yigit): Ask the window manager to send us a close request instead
    // of tearing the connection down behind our back.
    Atom WmDeleteWindow = XInternAtom(ClientDisplay, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(ClientDisplay, ClientWindow, &WmDeleteWindow, 1);

    XStoreName(ClientDisplay, ClientWindow, "Knyt");

    real32 TargetSecondsPerFrame = 1.0f / (real32)GameUpdateHz;

    x_sound_output SoundOutput = {};
    SoundOutput.SamplesPerSecond = 48000;
    SoundOutput.Channels = 2;
    SoundOutput.BytesPerSample = sizeof(int16)* SoundOutput.Channels;
    SoundOutput.BufferSizeInSamples = SoundOutput.SamplesPerSecond;
    SoundOutput.BufferSizeInBytes = SoundOutput.SamplesPerSecond*SoundOutput.BytesPerSample;
    SoundOutput.BufferSizeInFrames = SoundOutput.SamplesPerSecond/2;
	SoundOutput.SafetySamples = (uint32)(((real32)SoundOutput.SamplesPerSecond / GameUpdateHz)/3.0f);


    XInitAlsa(&SoundOutput);

    game_sound_output_buffer SoundBuffer = {};
    SoundBuffer.SamplesPerSecond = SoundOutput.SamplesPerSecond;
    SoundBuffer.Samples = XAllocateSoundBuffer(&SoundOutput);

    if(!SoundBuffer.Samples) { exit(1); }

    // NOTE(yigit): Zero means unset.
    XSetSizeHint(ClientDisplay, ClientWindow, WindowDimensions.Width,
            WindowDimensions.Height, 0, 0);
    XMapWindow(ClientDisplay, ClientWindow);
    //XToggleMaximize(ClientDisplay, ClientWindow);
    //XToggleFullscreen(ClientDisplay, ClientWindow);

    XFlush(ClientDisplay);


#if HANDMADE_INTERNAL
    // FIXME(yigit): Find these adrresses limits it may break.
    void* BaseAddress = (void*)Gigabytes(1);
#else
    void* BaseAddress = 0;
#endif

    game_memory GameMemory = {};
    GameMemory.PermanentStorageSize = Megabytes(64);
    GameMemory.TransientStorageSize = Gigabytes(1);
    State.TotalSize = GameMemory.PermanentStorageSize +
        GameMemory.TransientStorageSize;

    GameMemory.PermanentStorage = mmap(BaseAddress,
            (size_t)State.TotalSize,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1, 0);

    if (GameMemory.PermanentStorage == MAP_FAILED) {
        GameMemory.PermanentStorage = nullptr;
    }
    State.GameMemoryBlock = GameMemory.PermanentStorage;

    GameMemory.TransientStorage = ((uint8 *)GameMemory.PermanentStorage +
            GameMemory.PermanentStorageSize);

#if HANDMADE_INTERNAL
    GameMemory.DEBUGPlatformReadEntireFile = DEBUGPlatformReadEntireFile;
    GameMemory.DEBUGPlatformFreeFileMemory = DEBUGPlatformFreeFileMemory;
    GameMemory.DEBUGPlatformWriteEntireFile = DEBUGPlatformWriteEntireFile;
    GameMemory.DEBUGPlatformLog = DEBUGPlatformLog;
    GameMemory.DEBUGPlatformGetFileWriteTime = DEBUGPlatformGetFileWriteTime;
#endif

    // NOTE(yigit): If this fails, every entry point in GameMemory.OpenGL is
    // null and the game would crash on its first draw call, so we stop here
    // instead - which also skips allocating the (very large) replay buffers.
    if(!XInitOpenGL(&OpenGLContext, ClientDisplay, ClientWindow, &GameMemory))
    {
        fprintf(stderr, "FATAL: OpenGL initialization failed - aborting.\n");
        GlobalRunning = false;
    }

    if(GlobalRunning)
    {
        XInitReplays(&State);
    }

    // TODO(yigit): Add samples to if section.
    if(GlobalRunning && SoundBuffer.Samples &&
       GameMemory.PermanentStorage && GameMemory.TransientStorage)
    {

        game_input Input[2] = {};
        game_input *NewInput = &Input[0];
        game_input *OldInput = &Input[1];

        bool32 SleepIsGranular = XIsSleepGranular();
        timespec LastCounter = XGetWallClock();
        uint64 LastCycleCount = __rdtsc();

        XInitJoysticks(&State);

        while(GlobalRunning)
        {
            NewInput->dtForFrame= TargetSecondsPerFrame;
            NewInput->WindowWidth = WindowDimensions.Width;
            NewInput->WindowHeight = WindowDimensions.Height;

            struct stat NewStat = {};
            if(stat(SourceGameCodeLibraryPath, &NewStat) == 0)
            {
                // If the file on disk is newer than the one we have in memory
                if(NewStat.st_mtime != GameCode.LibraryMTime)
                {
                    XUnloadGame(&GameCode);
                    XLoadGame(&GameCode, SourceGameCodeLibraryPath);
                }
            }

            game_controller_input *OldKeyboardController =
                GetController(OldInput, 0);
            game_controller_input *NewKeyboardController =
                GetController(NewInput, 0);
            *NewKeyboardController = {};
            NewKeyboardController->IsConnected = true;
            for(int ButtonIndex = 0;
                    ButtonIndex < ArrayCount(NewKeyboardController->Buttons);
                    ++ButtonIndex)
            {
                NewKeyboardController->Buttons[ButtonIndex].EndedDown =
                    OldKeyboardController->Buttons[ButtonIndex].EndedDown;
            }

            XProcessEvents(&State,ClientDisplay, ClientWindow,
                    &WindowDimensions,
                    OldKeyboardController,
                    NewKeyboardController,NewInput,OldInput);

            if(!GlobalPause)
            {
#if HANDMADE_INTERNAL
                if (State.RecordingIndex)
                {
                    XRecordInput(&State, NewInput);
                }

                if (State.PlaybackIndex)
                {
                    game_input Temp = *NewInput;
                    XPlaybackInput(&State, NewInput);
                    for(uint32 MouseButtonIndex = 0;
                            MouseButtonIndex < 5; // FIXME(yigit): This is hard coded for now.
                            ++MouseButtonIndex)
                    {
                        NewInput->MouseButtons[MouseButtonIndex] = Temp.MouseButtons[MouseButtonIndex];
                    }
                    NewInput->MouseX = Temp.MouseX;
                    NewInput->MouseY = Temp.MouseY;
                    NewInput->MouseZ = Temp.MouseZ;
                }
#endif
                XProcessJoystickEvents(&State, OldInput, NewInput);
                XCheckControllerPlug(&State);

                thread_context Thread = {};

                int32 SamplesToFill=GetALSASampleCountToFill(&SoundOutput);
                SoundBuffer.SamplesPerSecond = SoundOutput.SamplesPerSecond;
                SoundBuffer.SampleCount = Align8(SamplesToFill);

                Assert(SoundBuffer.SampleCount >= 0);
                if(GameCode.UpdateAndRender)
                {
                    GameCode.UpdateAndRender(&Thread,&GameMemory, NewInput);
                }
                if(GameCode.GetSoundSamples)
                {
                    GameCode.GetSoundSamples(&Thread,&GameMemory,&SoundBuffer);
                }

                XCopyBufferToAlsa(&SoundOutput, SoundBuffer.Samples, SoundBuffer.SampleCount);

                timespec WorkCounter = XGetWallClock();
                real32 WorkSecondsElapsed = XGetSecondsElapsed(LastCounter, WorkCounter);

                real32 SecondsElapsedForFrame = WorkSecondsElapsed;
                if(SecondsElapsedForFrame < TargetSecondsPerFrame)
                {
                    if(SleepIsGranular)
                    {
                        real32 SecondsToWait = TargetSecondsPerFrame -
                            SecondsElapsedForFrame;
                        if (SecondsToWait > 0.001f)
                        {
                            struct timespec SleepTime;
                            SleepTime.tv_sec = (time_t)SecondsToWait;
                            SleepTime.tv_nsec = (long)((SecondsToWait -
                                        (real32)SleepTime.tv_sec) * 1e9f);

                            nanosleep(&SleepTime, NULL);
                        }
                    }

                    real32 TestSecondsElapsedForFrame = XGetSecondsElapsed(LastCounter,
                            XGetWallClock());
                    if(TestSecondsElapsedForFrame < TargetSecondsPerFrame)
                    {
                        // TODO(yigit): LOG MISSED SLEEP HERE
                    }

                    while(SecondsElapsedForFrame < TargetSecondsPerFrame)
                    {
                        SecondsElapsedForFrame = XGetSecondsElapsed(LastCounter,
                                XGetWallClock());
                    }
                }
                else
                {
                    // TODO(yigit): MISSED FRAME RATE!
                    // TODO(yigit): Logging
                }

                timespec EndCounter = XGetWallClock();
                real32 MSPerFrame = 1000.0f*XGetSecondsElapsed(LastCounter, EndCounter);

                LastCounter = EndCounter;

                // NOTE(yigit): Present.  The game has already issued every
                // draw call into the current context; all this does is set the
                // viewport for the window's present size and swap.
                XOpenGLRender(&OpenGLContext, WindowDimensions.Width, WindowDimensions.Height);

                game_input *Temp = NewInput;
                NewInput = OldInput;
                OldInput = Temp;

                uint64 EndCycleCount = __rdtsc();
                uint64 CyclesElapsed = EndCycleCount - LastCycleCount;

                LastCycleCount = EndCycleCount;

                real64 FPS = 0.0f;
                real64 MCPF = ((real64)CyclesElapsed / (1000.0f * 1000.0f));
                if (MSPerFrame > 0) {
                    FPS = 1000.0 / MSPerFrame;
                } else {
                    FPS = 0.0;
                }

                char FPSBuffer[256];
                //snprintf(FPSBuffer, sizeof(FPSBuffer),
               //         "%.02fms/f,  %.02ff/s,  %.02fmc/f\n", MSPerFrame, FPS, MCPF);
               // printf("%s", FPSBuffer);
                fflush(stdout);
            }
        }
    }
    XCloseJoysticks(&State);
    XOpenGLShutdown(&OpenGLContext);
    XCloseDisplay(ClientDisplay);
    return 0;
}
