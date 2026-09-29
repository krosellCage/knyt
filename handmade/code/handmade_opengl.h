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

/*
  The depth comparison, the test glDepthFunc picks.  The fragment
  is kept when "incoming depth COMPARISON stored depth" is true.

  NOTE(yigit): GL_LESS is the default, and is the one that draws a normal scene
  - a nearer fragment replaces a farther one.  The others are worth trying
  once: GL_ALWAYS keeps every fragment and so behaves as
  though there were no depth buffer at all, which is a good way to SEE what the
  depth test was doing for you.
*/
#define GL_NEVER                    0x0200
#define GL_LESS                     0x0201
#define GL_EQUAL                    0x0202
#define GL_LEQUAL                   0x0203
#define GL_GREATER                  0x0204
#define GL_NOTEQUAL                 0x0205
#define GL_GEQUAL                   0x0206
#define GL_ALWAYS                   0x0207

/*
  Stencil testing.  The stencil buffer is one extra byte per
  pixel that fragments can be tested against and can write into, which is what
  makes "draw only where I have NOT already drawn" expressible - outlines,
  portals, mirrors.

  The comparison functions are the same eight as above; these are the ACTIONS,
  what happens to the stored value in each of three outcomes (stencil fails,
  stencil passes but depth fails, both pass).

  NOTE(yigit): The mask is easy to lose an afternoon to.  glStencilMask(0x00)
  makes every write a no-op while leaving the TEST working, so a pass that
  seems to write nothing is usually a mask left at zero by the pass before it.
*/
#define GL_STENCIL_TEST             0x0B90
#define GL_STENCIL_BUFFER_BIT       0x00000400

/*
  How many bits the framebuffer actually GOT, which is not necessarily what the
  pixel format asked for.  Queried at startup so a context without a stencil
  buffer is reported rather than discovered through a technique that silently
  does nothing.

  NOTE(yigit): NOT glGetIntegerv(GL_STENCIL_BITS).  That is OpenGL 1.x state
  and 3.2 core REMOVED it - on a core context the call raises GL_INVALID_ENUM,
  leaves the output variable untouched, and so reports zero stencil bits on a
  framebuffer that has eight.  A check that fails on a working setup is worse
  than no check at all.

  The replacement asks the framebuffer about one of its attachments.  For the
  DEFAULT framebuffer the attachment names are GL_STENCIL and GL_DEPTH, not the
  GL_STENCIL_ATTACHMENT spelling that a framebuffer object would use.
*/
#define GL_FRAMEBUFFER                          0x8D40
#define GL_DEPTH                                0x1801
#define GL_STENCIL                              0x1802
#define GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE    0x8216
#define GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE  0x8217

/*
  Framebuffer objects - drawing into a texture instead of the window.

  A framebuffer is an empty container.  It becomes drawable once things are
  ATTACHED to it: a colour attachment (usually a texture, so a later pass can
  sample what was drawn) and a depth/stencil attachment (usually a
  renderbuffer, which is faster but cannot be sampled).  Binding framebuffer 0
  goes back to drawing into the window.

  GL_FRAMEBUFFER binds for both reading and drawing; the READ/DRAW pair split
  those, which glBlitFramebuffer needs.
*/
#define GL_READ_FRAMEBUFFER                     0x8CA8
#define GL_DRAW_FRAMEBUFFER                     0x8CA9
#define GL_RENDERBUFFER                         0x8D41

#define GL_COLOR_ATTACHMENT0                    0x8CE0
#define GL_DEPTH_ATTACHMENT                     0x8D00
#define GL_STENCIL_ATTACHMENT                   0x8D20
#define GL_DEPTH_STENCIL_ATTACHMENT             0x821A

// Depth and stencil packed into one 32-bit value: 24 bits depth, 8 stencil.
#define GL_DEPTH24_STENCIL8                     0x88F0

// Depth-only textures: one number per pixel, the distance to the nearest
// surface.  The shadow map is one of these.
#define GL_DEPTH_COMPONENT                      0x1902
#define GL_DEPTH_COMPONENT24                    0x81A6

// "No buffer" - for glDrawBuffer/glReadBuffer on a framebuffer that has no
// colour attachment at all, like the shadow map's.
#define GL_NONE                                 0
#define GL_DEPTH_STENCIL                        0x84F9
#define GL_UNSIGNED_INT_24_8                    0x84FA

/*
  What glCheckFramebufferStatus returns.  Anything but COMPLETE means draws
  into this framebuffer do nothing - no error, just a black result.

  NOTE(yigit): The failure codes are here so a debugger shows which one came
  back instead of a bare number.  The usual beginner one is
  INCOMPLETE_MISSING_ATTACHMENT: the framebuffer was checked before anything
  was attached to it.
*/
#define GL_FRAMEBUFFER_COMPLETE                        0x8CD5
#define GL_FRAMEBUFFER_UNDEFINED                       0x8219
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT           0x8CD6
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT   0x8CD7
#define GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER          0x8CDB
#define GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER          0x8CDC
#define GL_FRAMEBUFFER_UNSUPPORTED                     0x8CDD
#define GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE          0x8D56

#define GL_KEEP                     0x1E00
#define GL_ZERO                     0
#define GL_REPLACE                  0x1E01
#define GL_INCR                     0x1E02
#define GL_DECR                     0x1E03
#define GL_INVERT                   0x150A
#define GL_INCR_WRAP                0x8507
#define GL_DECR_WRAP                0x8508

/*
  Face culling.  A triangle's front is the side from which its corners go
  round counter-clockwise on screen (GL_CCW, the default).  With GL_CULL_FACE
  on, the faces glCullFace names are thrown away before any fragment is shaded
  - GL_BACK by default, which for a closed mesh is the half you could never see
  anyway.

  NOTE(yigit): A model wound the other way round does not look darker or
  inside-out when culling is on - it disappears, or shows only its far side.
  cube.obj is wound counter-clockwise from outside, so it is safe.
*/
#define GL_CULL_FACE                0x0B44
#define GL_FRONT                    0x0404
#define GL_BACK                     0x0405
#define GL_FRONT_AND_BACK           0x0408
#define GL_CW                       0x0900
#define GL_CCW                      0x0901

// Blending.  SRC_ALPHA / ONE_MINUS_SRC_ALPHA is the standard
// "over" operator: the incoming fragment contributes its own alpha, and what
// is already in the framebuffer contributes the rest.
#define GL_BLEND                    0x0BE2
#define GL_SRC_ALPHA                0x0302
#define GL_ONE_MINUS_SRC_ALPHA      0x0303
// Blend factor 1: "take this colour as it is".  ONE, ONE blending is plain
// addition - new colour + colour already there - which is how the bloom
// levels are stacked on top of each other.
#define GL_ONE                      1
#define GL_VERTEX_SHADER            0x8B31
#define GL_FRAGMENT_SHADER          0x8B30
#define GL_COMPILE_STATUS           0x8B81
#define GL_LINK_STATUS              0x8B82
#define GL_FLOAT                    0x1406
#define GL_UNSIGNED_INT             0x1405
#define GL_ELEMENT_ARRAY_BUFFER     0x8893
#define GL_DEBUG_OUTPUT_SYNCHRONOUS 0x8242

// Textures
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
/*
  Cube maps - six square textures used as the faces of a cube, sampled with a
  DIRECTION instead of a UV.  The GPU picks the face the direction points at,
  and the spot on that face.  What a sky is: a picture of everything that is
  infinitely far away, in every direction.

  NOTE(yigit): The six face targets are consecutive, in the order +X -X +Y -Y
  +Z -Z, so POSITIVE_X + n is face n.

  SEAMLESS makes the filter blend across the edge between two faces instead of
  clamping at it.  Core since 3.2 but OFF by default, and without it every
  cube edge shows as a faint line in the sky.
*/
#define GL_TEXTURE_CUBE_MAP                 0x8513
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X      0x8515
#define GL_TEXTURE_WRAP_R                   0x8072
#define GL_TEXTURE_CUBE_MAP_SEAMLESS        0x884F

// A float format: 16 bits per channel, so values can go above 1.0.
// This is what makes a texture "HDR".
#define GL_RGBA16F                  0x881A

// Float formats with fewer channels, for image-based lighting: the light
// maps need no alpha, and the BRDF table holds just two numbers per texel.
#define GL_RGB16F                   0x881B
#define GL_RG16F                    0x822F
#define GL_SRGB8                    0x8C41
#define GL_SRGB8_ALPHA8             0x8C43
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
typedef void   (GLAPIENTRY *PFNGLDISABLEPROC)                  (GLenum cap);
typedef void   (GLAPIENTRY *PFNGLBLENDFUNCPROC)                (GLenum sfactor, GLenum dfactor);
typedef void   (GLAPIENTRY *PFNGLDEPTHMASKPROC)                (GLboolean flag);
typedef void   (GLAPIENTRY *PFNGLDEPTHFUNCPROC)                (GLenum func);
typedef void   (GLAPIENTRY *PFNGLCULLFACEPROC)                 (GLenum mode);
typedef void   (GLAPIENTRY *PFNGLFRONTFACEPROC)                (GLenum mode);
typedef void   (GLAPIENTRY *PFNGLSTENCILFUNCPROC)              (GLenum func, GLint ref, GLuint mask);
typedef void   (GLAPIENTRY *PFNGLSTENCILOPPROC)                (GLenum sfail, GLenum dpfail, GLenum dppass);
typedef void   (GLAPIENTRY *PFNGLSTENCILMASKPROC)              (GLuint mask);
typedef void   (GLAPIENTRY *PFNGLGETINTEGERVPROC)              (GLenum pname, GLint *data);
typedef void   (GLAPIENTRY *PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC)(GLenum target, GLenum attachment, GLenum pname, GLint *params);

// Framebuffer objects
typedef void   (GLAPIENTRY *PFNGLGENFRAMEBUFFERSPROC)          (GLsizei n, GLuint *framebuffers);
typedef void   (GLAPIENTRY *PFNGLBINDFRAMEBUFFERPROC)          (GLenum target, GLuint framebuffer);
typedef void   (GLAPIENTRY *PFNGLDELETEFRAMEBUFFERSPROC)       (GLsizei n, const GLuint *framebuffers);
typedef GLenum (GLAPIENTRY *PFNGLCHECKFRAMEBUFFERSTATUSPROC)   (GLenum target);
typedef void   (GLAPIENTRY *PFNGLFRAMEBUFFERTEXTURE2DPROC)     (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef void   (GLAPIENTRY *PFNGLGENRENDERBUFFERSPROC)         (GLsizei n, GLuint *renderbuffers);
typedef void   (GLAPIENTRY *PFNGLBINDRENDERBUFFERPROC)         (GLenum target, GLuint renderbuffer);
typedef void   (GLAPIENTRY *PFNGLDELETERENDERBUFFERSPROC)      (GLsizei n, const GLuint *renderbuffers);
typedef void   (GLAPIENTRY *PFNGLRENDERBUFFERSTORAGEPROC)      (GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
typedef void   (GLAPIENTRY *PFNGLFRAMEBUFFERRENDERBUFFERPROC)  (GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
typedef void   (GLAPIENTRY *PFNGLBLITFRAMEBUFFERPROC)          (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);
typedef void   (GLAPIENTRY *PFNGLVIEWPORTPROC)                 (GLint x, GLint y, GLsizei width, GLsizei height);
typedef void   (GLAPIENTRY *PFNGLCLEARCOLORPROC)               (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
typedef void   (GLAPIENTRY *PFNGLCLEARPROC)                    (GLbitfield mask);
// NOTE(yigit): Which colour attachment draws and reads go to.  A framebuffer
// with NO colour attachment has to be told GL_NONE for both - in 3.3 the
// default, COLOR_ATTACHMENT0, pointing at nothing makes it incomplete.
typedef void   (GLAPIENTRY *PFNGLDRAWBUFFERPROC)               (GLenum buf);
typedef void   (GLAPIENTRY *PFNGLREADBUFFERPROC)               (GLenum src);
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
    PFNGLDISABLEPROC                 glDisable;
    PFNGLBLENDFUNCPROC               glBlendFunc;
    PFNGLDEPTHMASKPROC               glDepthMask;
    PFNGLDEPTHFUNCPROC               glDepthFunc;
    PFNGLCULLFACEPROC                glCullFace;
    PFNGLFRONTFACEPROC               glFrontFace;
    PFNGLSTENCILFUNCPROC             glStencilFunc;
    PFNGLSTENCILOPPROC               glStencilOp;
    PFNGLSTENCILMASKPROC             glStencilMask;
    PFNGLGETINTEGERVPROC             glGetIntegerv;
    PFNGLGETFRAMEBUFFERATTACHMENTPARAMETERIVPROC glGetFramebufferAttachmentParameteriv;

    // Framebuffer objects.  glViewport is here because a framebuffer that is
    // not the window's size needs the viewport changed to match while drawing
    // into it, and changed back afterwards.
    PFNGLGENFRAMEBUFFERSPROC         glGenFramebuffers;
    PFNGLBINDFRAMEBUFFERPROC         glBindFramebuffer;
    PFNGLDELETEFRAMEBUFFERSPROC      glDeleteFramebuffers;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC  glCheckFramebufferStatus;
    PFNGLFRAMEBUFFERTEXTURE2DPROC    glFramebufferTexture2D;
    PFNGLGENRENDERBUFFERSPROC        glGenRenderbuffers;
    PFNGLBINDRENDERBUFFERPROC        glBindRenderbuffer;
    PFNGLDELETERENDERBUFFERSPROC     glDeleteRenderbuffers;
    PFNGLRENDERBUFFERSTORAGEPROC     glRenderbufferStorage;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer;
    PFNGLBLITFRAMEBUFFERPROC         glBlitFramebuffer;
    PFNGLVIEWPORTPROC                glViewport;

    PFNGLCLEARCOLORPROC              glClearColor;
    PFNGLCLEARPROC                   glClear;
    PFNGLDRAWBUFFERPROC              glDrawBuffer;
    PFNGLREADBUFFERPROC              glReadBuffer;
    PFNGLDRAWARRAYSPROC              glDrawArrays;
    PFNGLDRAWELEMENTSPROC            glDrawElements;
} game_opengl_api;

#define HANDMADE_OPENGL_H
#endif
