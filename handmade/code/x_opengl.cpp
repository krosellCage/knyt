/* ========================================================================
   X11/GLX OpenGL support.

   ATTRIBUTION:

   The structure of this file follows the Win32 platform layer from Casey
   Muratori's Handmade Hero (handmadehero.org).  That code is not freely
   distributable; only its architecture is mirrored here, and the Linux
   implementation below is written against X11 and GLX.

   Parts were also adapted from xcb_handmade by Neil Blakey-Milner and
   contributors (github.com/nxsy/xcb_handmade), which is distributable under
   the BSD license.  That project targets XCB; this one targets Xlib, so the
   windowing code differs, but the approach was a reference.

   ======================================================================== */

#include "x_opengl.h"

#include <stdio.h>
#include <string.h>

static void GLAPIENTRY
XGLDebugCallback(GLenum Source, GLenum Type, GLuint Id, GLenum Severity,
                 GLsizei Length, const GLchar *Message, const void *UserParam)
{
    fprintf(stderr, "GL DEBUG: %s\n", Message);
}

// -------------------------------------------------------------------------
// Resolve one GL entry point.
//
// glXGetProcAddressARB is the one that is guaranteed present by
// GLX_ARB_get_proc_address, and unlike wglGetProcAddress it resolves core
// functions too - so there is no "load it out of the DLL instead" fallback to
// write here.  It is also allowed to hand back an address for a function the
// driver does not actually implement, but every entry point we ask for is
// core 3.3, so a null return is the only failure we can detect and the only
// one that matters.  Anything we fail to find clears *AllLoaded so the caller
// can bail instead of leaving a null pointer in the dispatch table for the
// game to crash on.
// -------------------------------------------------------------------------
static void *
XGLGetProcAddress(const char *Name, bool32 *AllLoaded)
{
    void *Result = (void *)glXGetProcAddressARB((const GLubyte *)Name);

    if(!Result)
    {
        fprintf(stderr, "ERROR: Failed to load GL entry point: %s\n", Name);
        *AllLoaded = false;
    }

    return(Result);
}

// NOTE(yigit): Xlib's default error handler calls exit().  glXCreateContextAttribsARB
// reports "I cannot give you that version" as a protocol error rather than a
// null return, so we have to be holding the handler while we ask, or a driver
// without 3.3 support takes the whole process down instead of letting us
// print something useful.
static bool32 GlobalXErrorOccurred;

static int
XSwallowErrorHandler(Display *ClientDisplay, XErrorEvent *Event)
{
    GlobalXErrorOccurred = true;
    return(0);
}

bool32
XChooseGLVisual(x_opengl_context *Context, Display *ClientDisplay, int ScreenIndex)
{
    if(!Context || !ClientDisplay) { return(false); }

    // GLX 1.3 is what gives us FBConfigs, which is what
    // glXCreateContextAttribsARB needs.
    int GLXMajor = 0;
    int GLXMinor = 0;
    if(!glXQueryVersion(ClientDisplay, &GLXMajor, &GLXMinor) ||
       ((GLXMajor == 1) && (GLXMinor < 3)) || (GLXMajor < 1))
    {
        fprintf(stderr, "ERROR: GLX 1.3 or better is required (found %d.%d)\n", GLXMajor, GLXMinor);
        return(false);
    }

    int VisualAttribs[] = {
        GLX_X_RENDERABLE,  True,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        GLX_RENDER_TYPE,   GLX_RGBA_BIT,
        GLX_X_VISUAL_TYPE, GLX_TRUE_COLOR,
        GLX_RED_SIZE,      8,
        GLX_GREEN_SIZE,    8,
        GLX_BLUE_SIZE,     8,
        GLX_ALPHA_SIZE,    8,
        GLX_DEPTH_SIZE,    24,
        GLX_STENCIL_SIZE,  8,
        GLX_DOUBLEBUFFER,  True,
        None
    };

    int ConfigCount = 0;
    GLXFBConfig *Configs = glXChooseFBConfig(ClientDisplay, ScreenIndex, VisualAttribs, &ConfigCount);
    if(!Configs || (ConfigCount == 0))
    {
        fprintf(stderr, "ERROR: glXChooseFBConfig found no matching framebuffer config!\n");
        return(false);
    }

    // NOTE(yigit): glXChooseFBConfig returns its list already sorted by how
    // well each config matches what we asked for, so the first one is the best
    // one and there is nothing to score by hand.
    Context->FBConfig = Configs[0];
    Context->VisualInfo = glXGetVisualFromFBConfig(ClientDisplay, Context->FBConfig);
    XFree(Configs);

    if(!Context->VisualInfo)
    {
        fprintf(stderr, "ERROR: The chosen framebuffer config has no X visual!\n");
        return(false);
    }

    return(true);
}

bool32
XInitOpenGL(x_opengl_context *Context, Display *ClientDisplay, Window ClientWindow,
            game_memory *Memory)
{
    if(!Context || !Memory || !ClientDisplay) { return(false); }

    if(!Context->FBConfig)
    {
        fprintf(stderr, "ERROR: XInitOpenGL called before XChooseGLVisual!\n");
        return(false);
    }

    Context->ClientDisplay = ClientDisplay;
    Context->ClientWindow = ClientWindow;

    PFNGLXCREATECONTEXTATTRIBSARBPROC glXCreateContextAttribsARB =
        (PFNGLXCREATECONTEXTATTRIBSARBPROC)glXGetProcAddressARB(
            (const GLubyte *)"glXCreateContextAttribsARB");

    if(!glXCreateContextAttribsARB)
    {
        fprintf(stderr, "ERROR: This driver does not support GLX_ARB_create_context - "
                        "a 3.3 core context is not available.\n");
        return(false);
    }

#if HANDMADE_INTERNAL
    int ContextAttribs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
        GLX_CONTEXT_MINOR_VERSION_ARB, 3,
        GLX_CONTEXT_PROFILE_MASK_ARB,  GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        GLX_CONTEXT_FLAGS_ARB,         GLX_CONTEXT_DEBUG_BIT_ARB,
        None
    };
#else
    int ContextAttribs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
        GLX_CONTEXT_MINOR_VERSION_ARB, 3,
        GLX_CONTEXT_PROFILE_MASK_ARB,  GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        None
    };
#endif

    GlobalXErrorOccurred = false;
    int (*OldErrorHandler)(Display *, XErrorEvent *) = XSetErrorHandler(XSwallowErrorHandler);

    Context->GlContext = glXCreateContextAttribsARB(ClientDisplay, Context->FBConfig, 0,
                                                    True, ContextAttribs);
    // NOTE(yigit): X is asynchronous - without this the error for the call
    // above may not have arrived yet and we would read a stale flag.
    XSync(ClientDisplay, False);
    XSetErrorHandler(OldErrorHandler);

    if(!Context->GlContext || GlobalXErrorOccurred)
    {
        fprintf(stderr, "ERROR: glXCreateContextAttribsARB failed to create a 3.3 core context!\n");
        Context->GlContext = 0;
        return(false);
    }

    if(!glXMakeCurrent(ClientDisplay, ClientWindow, Context->GlContext))
    {
        fprintf(stderr, "ERROR: glXMakeCurrent failed for the real context!\n");
        XOpenGLShutdown(Context);
        return(false);
    }

    // -----------------------------------------------------------------------
    // Load every GL entry point the GAME needs into Memory->OpenGL.
    // The game calls these through the struct; it never calls
    // glXGetProcAddress itself.
    // -----------------------------------------------------------------------
    bool32 AllLoaded = true;
    game_opengl_api *GL = &Memory->OpenGL;

    // Shader compilation
    GL->glCreateShader            = (PFNGLCREATESHADERPROC)           XGLGetProcAddress("glCreateShader", &AllLoaded);
    GL->glShaderSource            = (PFNGLSHADERSOURCEPROC)           XGLGetProcAddress("glShaderSource", &AllLoaded);
    GL->glCompileShader           = (PFNGLCOMPILESHADERPROC)          XGLGetProcAddress("glCompileShader", &AllLoaded);
    GL->glGetShaderiv             = (PFNGLGETSHADERIVPROC)            XGLGetProcAddress("glGetShaderiv", &AllLoaded);
    GL->glGetShaderInfoLog        = (PFNGLGETSHADERINFOLOGPROC)       XGLGetProcAddress("glGetShaderInfoLog", &AllLoaded);
    GL->glDeleteShader            = (PFNGLDELETESHADERPROC)           XGLGetProcAddress("glDeleteShader", &AllLoaded);

    // Program linking
    GL->glCreateProgram           = (PFNGLCREATEPROGRAMPROC)          XGLGetProcAddress("glCreateProgram", &AllLoaded);
    GL->glAttachShader            = (PFNGLATTACHSHADERPROC)           XGLGetProcAddress("glAttachShader", &AllLoaded);
    GL->glLinkProgram             = (PFNGLLINKPROGRAMPROC)            XGLGetProcAddress("glLinkProgram", &AllLoaded);
    GL->glGetProgramiv            = (PFNGLGETPROGRAMIVPROC)           XGLGetProcAddress("glGetProgramiv", &AllLoaded);
    GL->glGetProgramInfoLog       = (PFNGLGETPROGRAMINFOLOGPROC)      XGLGetProcAddress("glGetProgramInfoLog", &AllLoaded);
    GL->glUseProgram              = (PFNGLUSEPROGRAMPROC)             XGLGetProcAddress("glUseProgram", &AllLoaded);
    GL->glDeleteProgram           = (PFNGLDELETEPROGRAMPROC)          XGLGetProcAddress("glDeleteProgram", &AllLoaded);

    // Buffers and vertex arrays
    GL->glGenVertexArrays         = (PFNGLGENVERTEXARRAYSPROC)        XGLGetProcAddress("glGenVertexArrays", &AllLoaded);
    GL->glBindVertexArray         = (PFNGLBINDVERTEXARRAYPROC)        XGLGetProcAddress("glBindVertexArray", &AllLoaded);
    GL->glDeleteVertexArrays      = (PFNGLDELETEVERTEXARRAYSPROC)     XGLGetProcAddress("glDeleteVertexArrays", &AllLoaded);
    GL->glGenBuffers              = (PFNGLGENBUFFERSPROC)             XGLGetProcAddress("glGenBuffers", &AllLoaded);
    GL->glBindBuffer              = (PFNGLBINDBUFFERPROC)             XGLGetProcAddress("glBindBuffer", &AllLoaded);
    GL->glBufferData              = (PFNGLBUFFERDATAPROC)             XGLGetProcAddress("glBufferData", &AllLoaded);
    GL->glBufferSubData           = (PFNGLBUFFERSUBDATAPROC)          XGLGetProcAddress("glBufferSubData", &AllLoaded);
    GL->glDeleteBuffers           = (PFNGLDELETEBUFFERSPROC)          XGLGetProcAddress("glDeleteBuffers", &AllLoaded);

    // Vertex attributes
    GL->glVertexAttribPointer     = (PFNGLVERTEXATTRIBPOINTERPROC)    XGLGetProcAddress("glVertexAttribPointer", &AllLoaded);
    GL->glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)XGLGetProcAddress("glEnableVertexAttribArray", &AllLoaded);

    // Uniforms
    GL->glGetUniformLocation      = (PFNGLGETUNIFORMLOCATIONPROC)     XGLGetProcAddress("glGetUniformLocation", &AllLoaded);
    GL->glUniform1i               = (PFNGLUNIFORM1IPROC)              XGLGetProcAddress("glUniform1i", &AllLoaded);
    GL->glUniform1f               = (PFNGLUNIFORM1FPROC)              XGLGetProcAddress("glUniform1f", &AllLoaded);
    GL->glUniform2f               = (PFNGLUNIFORM2FPROC)              XGLGetProcAddress("glUniform2f", &AllLoaded);
    GL->glUniform3f               = (PFNGLUNIFORM3FPROC)              XGLGetProcAddress("glUniform3f", &AllLoaded);
    GL->glUniform3fv              = (PFNGLUNIFORM3FVPROC)             XGLGetProcAddress("glUniform3fv", &AllLoaded);
    GL->glUniform4f               = (PFNGLUNIFORM4FPROC)              XGLGetProcAddress("glUniform4f", &AllLoaded);
    GL->glUniformMatrix4fv        = (PFNGLUNIFORMMATRIX4FVPROC)       XGLGetProcAddress("glUniformMatrix4fv", &AllLoaded);

    // Textures
    GL->glGenerateMipmap          = (PFNGLGENERATEMIPMAPPROC)         XGLGetProcAddress("glGenerateMipmap", &AllLoaded);
    GL->glActiveTexture           = (PFNGLACTIVETEXTUREPROC)          XGLGetProcAddress("glActiveTexture", &AllLoaded);
    GL->glGenTextures             = (PFNGLGENTEXTURESPROC)            XGLGetProcAddress("glGenTextures", &AllLoaded);
    GL->glBindTexture             = (PFNGLBINDTEXTUREPROC)            XGLGetProcAddress("glBindTexture", &AllLoaded);
    GL->glTexParameteri           = (PFNGLTEXPARAMETERIPROC)          XGLGetProcAddress("glTexParameteri", &AllLoaded);
    GL->glTexImage2D              = (PFNGLTEXIMAGE2DPROC)             XGLGetProcAddress("glTexImage2D", &AllLoaded);
    GL->glDeleteTextures          = (PFNGLDELETETEXTURESPROC)         XGLGetProcAddress("glDeleteTextures", &AllLoaded);
    GL->glPixelStorei             = (PFNGLPIXELSTOREIPROC)            XGLGetProcAddress("glPixelStorei", &AllLoaded);

    // Core 1.1
    GL->glEnable                  = (PFNGLENABLEPROC)                 XGLGetProcAddress("glEnable", &AllLoaded);
    GL->glClearColor              = (PFNGLCLEARCOLORPROC)             XGLGetProcAddress("glClearColor", &AllLoaded);
    GL->glClear                   = (PFNGLCLEARPROC)                  XGLGetProcAddress("glClear", &AllLoaded);
    GL->glDrawArrays              = (PFNGLDRAWARRAYSPROC)             XGLGetProcAddress("glDrawArrays", &AllLoaded);
    GL->glDrawElements            = (PFNGLDRAWELEMENTSPROC)           XGLGetProcAddress("glDrawElements", &AllLoaded);

    if(!AllLoaded)
    {
        fprintf(stderr, "ERROR: One or more required GL entry points are missing - aborting.\n");
        XOpenGLShutdown(Context);
        return(false);
    }

    // NOTE(yigit): Optional - we can run without it, so it does not clear AllLoaded.
    Context->glDebugMessageCallback =
        (PFNGLDEBUGMESSAGECALLBACKPROC)glXGetProcAddressARB((const GLubyte *)"glDebugMessageCallback");

#if HANDMADE_INTERNAL
    fprintf(stderr, "OpenGL %s | %s | %s\n",
            (const char *)glGetString(GL_VERSION),
            (const char *)glGetString(GL_VENDOR),
            (const char *)glGetString(GL_RENDERER));

    if(Context->glDebugMessageCallback)
    {
        GL->glEnable(GL_DEBUG_OUTPUT);
        // NOTE(yigit): Synchronous so the callback fires on our thread, at the
        // call site that caused it, instead of on a driver thread later.
        GL->glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        Context->glDebugMessageCallback(XGLDebugCallback, 0);
    }
#endif

    return(true);
}

void
XOpenGLRender(x_opengl_context *Context, int Width, int Height)
{
    if(!Context || !Context->GlContext) { return; }

    // The game has already cleared and issued all draw calls inside
    // GameUpdateAndRender.  All we do here is set the viewport and present.
    glViewport(0, 0, Width, Height);
    glXSwapBuffers(Context->ClientDisplay, Context->ClientWindow);
}

void
XOpenGLShutdown(x_opengl_context *Context)
{
    if(!Context) { return; }

    if(Context->ClientDisplay)
    {
        glXMakeCurrent(Context->ClientDisplay, None, 0);

        if(Context->GlContext)
        {
            glXDestroyContext(Context->ClientDisplay, Context->GlContext);
            Context->GlContext = 0;
        }
    }

    if(Context->VisualInfo)
    {
        XFree(Context->VisualInfo);
        Context->VisualInfo = 0;
    }
}
