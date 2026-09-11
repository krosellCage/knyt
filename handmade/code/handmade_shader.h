#if !defined(HANDMADE_SHADER_H)
/*
  NOTE(yigit): Everything to do with shaders - loading them off disk,
  compiling, linking, hot reloading, and setting uniforms.

  Contents:
    - loading, compiling, linking, hot reloading
    - uniform setters
*/

#include "handmade.h"
#include "handmade_math.h"

// Loading, compiling, linking, and hot reloading

internal uint32
GameCompileShader(thread_context *Thread, game_memory *Memory, game_opengl_api *GL,
                  uint32 Type, const char *Source)
{
    uint32 Shader = GL->glCreateShader(Type);
    GL->glShaderSource(Shader, 1, &Source, 0);
    GL->glCompileShader(Shader);

#if HANDMADE_INTERNAL
    int32 Success;
    GL->glGetShaderiv(Shader, GL_COMPILE_STATUS, &Success);
    if(!Success)
    {
        char InfoLog[512];
        GL->glGetShaderInfoLog(Shader, sizeof(InfoLog), 0, InfoLog);
        Memory->DEBUGPlatformLog(Thread, "SHADER COMPILE ERROR:\n");
        Memory->DEBUGPlatformLog(Thread, InfoLog);
        Memory->DEBUGPlatformLog(Thread, "\n");
    }
#endif

    return(Shader);
}

// Helper: link a program from two already-compiled shaders.
// Returns 0 if linking failed, so callers can keep whatever they already had.

internal uint32
GameLinkProgram(thread_context *Thread, game_memory *Memory, game_opengl_api *GL,
                uint32 VertShader, uint32 FragShader)
{
    uint32 Program = GL->glCreateProgram();
    GL->glAttachShader(Program, VertShader);
    GL->glAttachShader(Program, FragShader);
    GL->glLinkProgram(Program);

    int32 Success;
    GL->glGetProgramiv(Program, GL_LINK_STATUS, &Success);
    if(!Success)
    {
        char InfoLog[512];
        GL->glGetProgramInfoLog(Program, sizeof(InfoLog), 0, InfoLog);
        Memory->DEBUGPlatformLog(Thread, "PROGRAM LINK ERROR:\n");
        Memory->DEBUGPlatformLog(Thread, InfoLog);
        Memory->DEBUGPlatformLog(Thread, "\n");

        GL->glDeleteProgram(Program);
        Program = 0;
    }

    return(Program);
}

internal uint32
GameCompileShaderFromFile(thread_context *Thread, game_memory *Memory, game_opengl_api *GL,
                          uint32 Type, const char *FileName)
{
    uint32 Shader = 0;
    debug_read_file_result File = Memory->DEBUGPlatformReadEntireFile(Thread, FileName);

    if(File.Contents)
    {
        Shader = GL->glCreateShader(Type);

        // Pass the explicit size so OpenGL doesn't look for a null-terminator!
        const char *Source = (const char *)File.Contents;
        int32 Size = (int32)File.ContentsSize;
        GL->glShaderSource(Shader, 1, &Source, &Size);

        GL->glCompileShader(Shader);

        int32 Success;
        GL->glGetShaderiv(Shader, GL_COMPILE_STATUS, &Success);
        if(!Success)
        {
            char InfoLog[512];
            GL->glGetShaderInfoLog(Shader, sizeof(InfoLog), 0, InfoLog);
            Memory->DEBUGPlatformLog(Thread, "SHADER COMPILE ERROR in file: ");
            Memory->DEBUGPlatformLog(Thread, FileName);
            Memory->DEBUGPlatformLog(Thread, "\n");
            Memory->DEBUGPlatformLog(Thread, InfoLog);
            Memory->DEBUGPlatformLog(Thread, "\n");

            GL->glDeleteShader(Shader);
            Shader = 0;
        }

        // Safely free the memory immediately after compiling
        Memory->DEBUGPlatformFreeFileMemory(Thread, File.Contents);
    }
    else
    {
        Memory->DEBUGPlatformLog(Thread, "ERROR: Failed to load shader file: ");
        Memory->DEBUGPlatformLog(Thread, FileName);
        Memory->DEBUGPlatformLog(Thread, "\n");
    }

    return(Shader);
}

// Shader hot reloading
//
// NOTE(yigit): This table lives in the DLL image on purpose.  game_state sits
// in PermanentStorage and survives a DLL reload, but string literals do not -
// they belong to the module that just got unloaded.  Storing file names in
// game_state would leave dangling pointers after the first reload, so the
// names are looked up here fresh every frame and only timestamps are kept.

struct shader_source_files
{
    const char *VertFileName;
    const char *FragFileName;
};

global_variable const shader_source_files GlobalShaderFiles[] =
{
    {"data\\vertexShader.vert", "data\\fragmentShader.frag"},
    {"data\\vertexShader.vert", "data\\lightCube.frag"},
    {"data\\overlay.vert",      "data\\overlay.frag"},
};

// Builds a complete program, or returns 0 if any stage failed.  Nothing the
// caller owns is touched, so a typo in a shader can never take down the
// program that is currently drawing.
internal uint32
GameBuildShaderProgram(thread_context *Thread, game_memory *Memory, game_opengl_api *GL,
                       const char *VertFileName, const char *FragFileName)
{
    uint32 Result = 0;

    uint32 Vert = GameCompileShaderFromFile(Thread, Memory, GL, GL_VERTEX_SHADER, VertFileName);
    uint32 Frag = GameCompileShaderFromFile(Thread, Memory, GL, GL_FRAGMENT_SHADER, FragFileName);

    if(Vert && Frag)
    {
        Result = GameLinkProgram(Thread, Memory, GL, Vert, Frag);
    }

    // NOTE(yigit): glDeleteShader silently ignores 0, so this is safe even
    // when one or both compiles failed.
    GL->glDeleteShader(Vert);
    GL->glDeleteShader(Frag);

    return(Result);
}

// NOTE(yigit): The hot-reload loop used to live here.  It moved to
// handmade_render.h when the renderer became an object that owns its own
// programs - polling files and swapping program handles is backend work, and
// this file is now only about compiling, linking and setting uniforms.

// Uniform setters

/*
  Three things worth knowing about every function below.

  1. Locations are looked up fresh on each call and never cached.  That is
     deliberate.  GameUpdateShaderPrograms relinks a program whenever its
     .vert or .frag changes on disk, and uniform locations belong to one
     specific program object - a location cached before a reload would point
     into the program that has since been deleted, and the write would land
     somewhere meaningless with no error reported.  The lookup is a short
     string hash inside the driver.  Cache it only when a profiler says to,
     and invalidate from the reload path when you do.

  2. glUniform* always writes into the program that is CURRENTLY BOUND, not
     into whichever handle the location was looked up on.  Those can differ,
     and nothing warns you when they do.  Every setter here binds Program
     itself so the two can never disagree.  The cost is one redundant
     glUseProgram per call, which drivers fold away.  GL 4.1 added
     glProgramUniform*, which sets without binding, but that is not available
     in a 3.3 core context.

  3. If a uniform is declared in GLSL but never actually read, the compiler
     strips it and glGetUniformLocation returns -1.  OpenGL then silently
     ignores the write (this is specified behaviour, not an error).  That is
     why a uniform can appear to do nothing with no complaint from anywhere -
     see the book's warning on p. 45.
*/

/*
  NOTE(yigit): The setters below take a NAME, which costs a glGetUniformLocation
  - a string comparison inside the driver - on every single write.  Fine when a
  uniform is set once a frame; wasteful when the same three are set once per
  submesh, twenty-four times over.

  These take a LOCATION instead, so the lookup can be hoisted out of a loop.
  Two rules come with them:

    1. They write to whatever program is currently bound.  The name-based
       setters bind for you; these do not, so the caller must have called
       glUseProgram already.

    2. A location is only valid for one linked program.  Relinking - which
       shader hot reload does every time a .frag is saved - invalidates it.
       So look up per FRAME and reuse within the frame; never park a location
       in PermanentStorage.
*/

inline int32
GetUniformLocation(game_opengl_api *GL, uint32 Program, const char *Name)
{
    return(GL->glGetUniformLocation(Program, Name));
}

inline void
SetUniformIntAt(game_opengl_api *GL, int32 Location, int32 Value)
{
    GL->glUniform1i(Location, Value);
}

inline void
SetUniformFloatAt(game_opengl_api *GL, int32 Location, real32 Value)
{
    GL->glUniform1f(Location, Value);
}

inline void
SetUniformVec3At(game_opengl_api *GL, int32 Location, vec3 V)
{
    GL->glUniform3f(Location, V.X, V.Y, V.Z);
}

inline void
SetUniformMat4At(game_opengl_api *GL, int32 Location, mat4 Value)
{
    GL->glUniformMatrix4fv(Location, 1, GL_FALSE, Value.E);
}

internal void
SetUniformBool(game_opengl_api *GL, uint32 Program, const char *Name, bool32 Value)
{
    // NOTE(yigit): There is no bool upload in OpenGL - GLSL bools are set as
    // ints.  This is what the book's setBool does behind the cast.
    GL->glUseProgram(Program);
    GL->glUniform1i(GL->glGetUniformLocation(Program, Name), Value ? 1 : 0);
}

internal void
SetUniformInt(game_opengl_api *GL, uint32 Program, const char *Name, int32 Value)
{
    GL->glUseProgram(Program);
    GL->glUniform1i(GL->glGetUniformLocation(Program, Name), Value);
}

internal void
SetUniformFloat(game_opengl_api *GL, uint32 Program, const char *Name, real32 Value)
{
    GL->glUseProgram(Program);
    GL->glUniform1f(GL->glGetUniformLocation(Program, Name), Value);
}

internal void
SetUniformVec2(game_opengl_api *GL, uint32 Program, const char *Name,
               real32 X, real32 Y)
{
    GL->glUseProgram(Program);
    GL->glUniform2f(GL->glGetUniformLocation(Program, Name), X, Y);
}

internal void
SetUniformVec3(game_opengl_api *GL, uint32 Program, const char *Name,
               real32 X, real32 Y, real32 Z)
{
    GL->glUseProgram(Program);
    GL->glUniform3f(GL->glGetUniformLocation(Program, Name), X, Y, Z);
}

internal void
SetUniformVec4(game_opengl_api *GL, uint32 Program, const char *Name,
               real32 X, real32 Y, real32 Z, real32 W)
{
    GL->glUseProgram(Program);
    GL->glUniform4f(GL->glGetUniformLocation(Program, Name), X, Y, Z, W);
}

// NOTE(yigit): Overloads that take the vector whole, so call sites can pass a
// vec3 instead of unpacking it into three fields.  C++ picks between these and
// the component versions above by argument type - same name, no ambiguity,
// because the counts differ.
internal void
SetUniformVec3(game_opengl_api *GL, uint32 Program, const char *Name, vec3 V)
{
    SetUniformVec3(GL, Program, Name, V.X, V.Y, V.Z);
}

internal void
SetUniformVec4(game_opengl_api *GL, uint32 Program, const char *Name, vec4 V)
{
    SetUniformVec4(GL, Program, Name, V.X, V.Y, V.Z, V.W);
}

// A vec3 plus an explicit w - mirrors GLSL's vec4(someVec3, 1.0).
internal void
SetUniformVec4(game_opengl_api *GL, uint32 Program, const char *Name, vec3 XYZ, real32 W)
{
    SetUniformVec4(GL, Program, Name, XYZ.X, XYZ.Y, XYZ.Z, W);
}

// NOTE(yigit): For chapter 8 onwards.  Transpose is GL_FALSE because mat4.E is
// already stored column-major (see handmade_math.h), which is the layout
// OpenGL expects - so the data goes across untouched.
internal void
SetUniformMat4(game_opengl_api *GL, uint32 Program, const char *Name, mat4 Value)
{
    GL->glUseProgram(Program);
    GL->glUniformMatrix4fv(GL->glGetUniformLocation(Program, Name), 1, GL_FALSE, Value.E);
}

#define HANDMADE_SHADER_H
#endif
