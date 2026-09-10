#if !defined(HANDMADE_OPENGL_H)
/*
  NOTE(yigit): Platform-independent OpenGL interface.

  This header holds ONLY what the game layer needs to talk to OpenGL: the
  base GL types, the constants we use, the entry point typedefs, and the
  game_opengl_api dispatch struct that the platform fills in.  It must not
  reference windows.h, WGL, or anything else platform-specific - that all
  lives in win32_opengl.h.
*/

#include <stddef.h> // NOTE(yigit): ptrdiff_t, for GLintptr/GLsizeiptr

// NOTE(yigit): OpenGL uses __stdcall on Windows and the default convention
// everywhere else.  APIENTRY is a windows.h macro, so we cannot use it here.
#if !defined(GLAPIENTRY)
#if defined(_WIN32)
#define GLAPIENTRY __stdcall
#else
#define GLAPIENTRY
#endif
#endif

// GL base types
typedef unsigned int   GLenum;
typedef unsigned int   GLuint;
typedef int            GLint;
typedef int            GLsizei;
typedef char           GLchar;
typedef float          GLfloat;
typedef unsigned char  GLboolean;
typedef signed char    GLbyte;
typedef ptrdiff_t      GLintptr;
typedef ptrdiff_t      GLsizeiptr;
typedef unsigned int   GLbitfield;

// GL constants used by the game layer
// (win32_opengl.cpp gets these from GL.h; the game layer never includes GL.h)
#define GL_FALSE                    0
#define GL_TRUE                     1
#define GL_TRIANGLES                0x0004
#define GL_DEPTH_TEST               0x0B71
#define GL_DEBUG_OUTPUT             0x92E0
#define GL_COLOR_BUFFER_BIT         0x00004000
#define GL_DEPTH_BUFFER_BIT         0x00000100
#define GL_ARRAY_BUFFER             0x8892
#define GL_STATIC_DRAW              0x88E4
#define GL_DYNAMIC_DRAW             0x88E8
#define GL_VERTEX_SHADER            0x8B31
#define GL_FRAGMENT_SHADER          0x8B30
#define GL_COMPILE_STATUS           0x8B81
#define GL_LINK_STATUS              0x8B82
#define GL_FLOAT                    0x1406
#define GL_UNSIGNED_INT             0x1405
#define GL_ELEMENT_ARRAY_BUFFER     0x8893
#define GL_DEBUG_OUTPUT_SYNCHRONOUS 0x8242

// Textures (book ch. 7)
#define GL_TEXTURE_2D               0x0DE1
// NOTE(yigit): Texture unit selectors for glActiveTexture.  They are
// consecutive, so GL_TEXTURE0 + n works for any unit and these are only here
// for readability.
#define GL_TEXTURE0                 0x84C0
#define GL_TEXTURE1                 0x84C1
#define GL_TEXTURE2                 0x84C2
#define GL_TEXTURE3                 0x84C3
#define GL_TEXTURE4                 0x84C4
#define GL_TEXTURE5                 0x84C5
#define GL_TEXTURE6                 0x84C6
#define GL_TEXTURE7                 0x84C7
// Wrapping - what happens outside the 0..1 coordinate range
#define GL_TEXTURE_WRAP_S           0x2802
#define GL_TEXTURE_WRAP_T           0x2803
#define GL_REPEAT                   0x2901
#define GL_MIRRORED_REPEAT          0x8370
#define GL_CLAMP_TO_EDGE            0x812F
#define GL_CLAMP_TO_BORDER          0x812D
// Filtering - how a texel is chosen when the sample lands between texels
#define GL_TEXTURE_MIN_FILTER       0x2801
#define GL_TEXTURE_MAG_FILTER       0x2800
#define GL_NEAREST                  0x2600
#define GL_LINEAR                   0x2601
#define GL_NEAREST_MIPMAP_NEAREST   0x2700
#define GL_LINEAR_MIPMAP_NEAREST    0x2701
#define GL_NEAREST_MIPMAP_LINEAR    0x2702
#define GL_LINEAR_MIPMAP_LINEAR     0x2703
// Pixel formats for glTexImage2D
#define GL_RED                      0x1903
#define GL_RG                       0x8227
#define GL_RGB                      0x1907
#define GL_RGBA                     0x1908
#define GL_UNSIGNED_BYTE            0x1401
// NOTE(yigit): Row alignment for pixel uploads.  The default is 4, which makes
// OpenGL assume every row starts on a 4-byte boundary and pad the stride to
// suit.  Decoders hand back tightly packed rows, so for any 3-channel image
// whose width is not a multiple of 4 the padded stride walks off the end of
// the buffer.  Set this to 1 before uploading.
#define GL_UNPACK_ALIGNMENT         0x0CF5

// -------------------------------------------------------------------------
// GL entry point typedefs
// Named to match glext.h exactly so adding glext.h later won't conflict.
// -------------------------------------------------------------------------

// Shaders
typedef GLuint (GLAPIENTRY *PFNGLCREATESHADERPROC)             (GLenum type);
typedef void   (GLAPIENTRY *PFNGLSHADERSOURCEPROC)             (GLuint shader, GLsizei count, const GLchar *const*string, const GLint *length);
typedef void   (GLAPIENTRY *PFNGLCOMPILESHADERPROC)            (GLuint shader);
typedef void   (GLAPIENTRY *PFNGLGETSHADERIVPROC)              (GLuint shader, GLenum pname, GLint *params);
typedef void   (GLAPIENTRY *PFNGLGETSHADERINFOLOGPROC)         (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void   (GLAPIENTRY *PFNGLDELETESHADERPROC)             (GLuint shader);

// Programs
typedef GLuint (GLAPIENTRY *PFNGLCREATEPROGRAMPROC)            (void);
typedef void   (GLAPIENTRY *PFNGLATTACHSHADERPROC)             (GLuint program, GLuint shader);
typedef void   (GLAPIENTRY *PFNGLLINKPROGRAMPROC)              (GLuint program);
typedef void   (GLAPIENTRY *PFNGLGETPROGRAMIVPROC)             (GLuint program, GLenum pname, GLint *params);
typedef void   (GLAPIENTRY *PFNGLGETPROGRAMINFOLOGPROC)        (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
typedef void   (GLAPIENTRY *PFNGLUSEPROGRAMPROC)               (GLuint program);
typedef void   (GLAPIENTRY *PFNGLDELETEPROGRAMPROC)            (GLuint program);

// Vertex arrays
typedef void   (GLAPIENTRY *PFNGLGENVERTEXARRAYSPROC)          (GLsizei n, GLuint *arrays);
typedef void   (GLAPIENTRY *PFNGLBINDVERTEXARRAYPROC)          (GLuint array);
typedef void   (GLAPIENTRY *PFNGLDELETEVERTEXARRAYSPROC)       (GLsizei n, const GLuint *arrays);

// Buffers
typedef void   (GLAPIENTRY *PFNGLGENBUFFERSPROC)               (GLsizei n, GLuint *buffers);
typedef void   (GLAPIENTRY *PFNGLBINDBUFFERPROC)               (GLenum target, GLuint buffer);
typedef void   (GLAPIENTRY *PFNGLBUFFERDATAPROC)               (GLenum target, GLsizeiptr size, const void *data, GLenum usage);
typedef void   (GLAPIENTRY *PFNGLBUFFERSUBDATAPROC)            (GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
typedef void   (GLAPIENTRY *PFNGLDELETEBUFFERSPROC)            (GLsizei n, const GLuint *buffers);

// Vertex attributes
typedef void   (GLAPIENTRY *PFNGLVERTEXATTRIBPOINTERPROC)      (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
typedef void   (GLAPIENTRY *PFNGLENABLEVERTEXATTRIBARRAYPROC)  (GLuint index);

// Uniforms
typedef GLint  (GLAPIENTRY *PFNGLGETUNIFORMLOCATIONPROC)       (GLuint program, const GLchar *name);
typedef void   (GLAPIENTRY *PFNGLUNIFORM1IPROC)                (GLint location, GLint v0);
typedef void   (GLAPIENTRY *PFNGLUNIFORM1FPROC)                (GLint location, GLfloat v0);
typedef void   (GLAPIENTRY *PFNGLUNIFORM2FPROC)                (GLint location, GLfloat v0, GLfloat v1);
typedef void   (GLAPIENTRY *PFNGLUNIFORM3FPROC)                (GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
typedef void   (GLAPIENTRY *PFNGLUNIFORM3FVPROC)               (GLint location, GLsizei count, const GLfloat *value);
typedef void   (GLAPIENTRY *PFNGLUNIFORM4FPROC)                (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
typedef void   (GLAPIENTRY *PFNGLUNIFORMMATRIX4FVPROC)         (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);

// Textures
typedef void   (GLAPIENTRY *PFNGLGENERATEMIPMAPPROC)           (GLenum target);
typedef void   (GLAPIENTRY *PFNGLACTIVETEXTUREPROC)            (GLenum texture);
// NOTE(yigit): These five are core 1.1, so they come out of opengl32.dll
// rather than the driver - Win32GLGetProcAddress handles that fallback.
typedef void   (GLAPIENTRY *PFNGLGENTEXTURESPROC)              (GLsizei n, GLuint *textures);
typedef void   (GLAPIENTRY *PFNGLBINDTEXTUREPROC)              (GLenum target, GLuint texture);
typedef void   (GLAPIENTRY *PFNGLTEXPARAMETERIPROC)            (GLenum target, GLenum pname, GLint param);
typedef void   (GLAPIENTRY *PFNGLTEXIMAGE2DPROC)               (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels);
typedef void   (GLAPIENTRY *PFNGLDELETETEXTURESPROC)           (GLsizei n, const GLuint *textures);
typedef void   (GLAPIENTRY *PFNGLPIXELSTOREIPROC)              (GLenum pname, GLint param);

// Core 1.1 entry points (these come out of opengl32.dll, not wglGetProcAddress)
typedef void   (GLAPIENTRY *PFNGLENABLEPROC)                   (GLenum cap);
typedef void   (GLAPIENTRY *PFNGLCLEARCOLORPROC)               (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
typedef void   (GLAPIENTRY *PFNGLCLEARPROC)                    (GLbitfield mask);
typedef void   (GLAPIENTRY *PFNGLDRAWARRAYSPROC)               (GLenum mode, GLint first, GLsizei count);
typedef void   (GLAPIENTRY *PFNGLDRAWELEMENTSPROC)             (GLenum mode, GLsizei count, GLenum type, const void *indices);

typedef struct game_opengl_api
{
    // Shader compilation
    PFNGLCREATESHADERPROC            glCreateShader;
    PFNGLSHADERSOURCEPROC            glShaderSource;
    PFNGLCOMPILESHADERPROC           glCompileShader;
    PFNGLGETSHADERIVPROC             glGetShaderiv;
    PFNGLGETSHADERINFOLOGPROC        glGetShaderInfoLog;
    PFNGLDELETESHADERPROC            glDeleteShader;

    // Program linking
    PFNGLCREATEPROGRAMPROC           glCreateProgram;
    PFNGLATTACHSHADERPROC            glAttachShader;
    PFNGLLINKPROGRAMPROC             glLinkProgram;
    PFNGLGETPROGRAMIVPROC            glGetProgramiv;
    PFNGLGETPROGRAMINFOLOGPROC       glGetProgramInfoLog;
    PFNGLUSEPROGRAMPROC              glUseProgram;
    PFNGLDELETEPROGRAMPROC           glDeleteProgram;

    // Buffers and vertex arrays
    PFNGLGENVERTEXARRAYSPROC         glGenVertexArrays;
    PFNGLBINDVERTEXARRAYPROC         glBindVertexArray;
    PFNGLDELETEVERTEXARRAYSPROC      glDeleteVertexArrays;
    PFNGLGENBUFFERSPROC              glGenBuffers;
    PFNGLBINDBUFFERPROC              glBindBuffer;
    PFNGLBUFFERDATAPROC              glBufferData;
    PFNGLBUFFERSUBDATAPROC           glBufferSubData;
    PFNGLDELETEBUFFERSPROC           glDeleteBuffers;

    // Vertex attributes
    PFNGLVERTEXATTRIBPOINTERPROC     glVertexAttribPointer;
    PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;

    // Uniforms
    PFNGLGETUNIFORMLOCATIONPROC      glGetUniformLocation;
    PFNGLUNIFORM1IPROC               glUniform1i;
    PFNGLUNIFORM1FPROC               glUniform1f;
    PFNGLUNIFORM2FPROC               glUniform2f;
    PFNGLUNIFORM3FPROC               glUniform3f;
    PFNGLUNIFORM3FVPROC              glUniform3fv;
    PFNGLUNIFORM4FPROC               glUniform4f;
    PFNGLUNIFORMMATRIX4FVPROC        glUniformMatrix4fv;

    // Textures
    PFNGLGENERATEMIPMAPPROC          glGenerateMipmap;
    PFNGLACTIVETEXTUREPROC           glActiveTexture;
    PFNGLGENTEXTURESPROC             glGenTextures;
    PFNGLBINDTEXTUREPROC             glBindTexture;
    PFNGLTEXPARAMETERIPROC           glTexParameteri;
    PFNGLTEXIMAGE2DPROC              glTexImage2D;
    PFNGLDELETETEXTURESPROC          glDeleteTextures;
    PFNGLPIXELSTOREIPROC             glPixelStorei;

    // Core 1.1
    PFNGLENABLEPROC                  glEnable;
    PFNGLCLEARCOLORPROC              glClearColor;
    PFNGLCLEARPROC                   glClear;
    PFNGLDRAWARRAYSPROC              glDrawArrays;
    PFNGLDRAWELEMENTSPROC            glDrawElements;
} game_opengl_api;

#define HANDMADE_OPENGL_H
#endif
