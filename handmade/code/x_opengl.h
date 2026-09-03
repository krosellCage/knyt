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

#if !defined(X_OPENGL_H)

/*
  NOTE(yigit): X11/GLX-specific OpenGL support.  This is the Linux counterpart
  to win32_opengl.h - same job, same two-stage shape (pick a format, then make
  a 3.3 core context and fill in the game's dispatch table), different API
  underneath.  The platform-independent half (GL types, constants, entry
  points, game_opengl_api) lives in handmade_opengl.h, which the game layer
  includes via handmade_platform.h.  Nothing in the game layer should ever
  include this file.
*/

#include <X11/Xlib.h>
#include <GL/glx.h>

#include "handmade_platform.h"

struct x_opengl_context
{
    // NOTE(yigit): Not named Display/Window - a member whose name matches the
    // Xlib typedef it is declared with changes what that name means for the
    // rest of the struct, which C++ rejects outright.
    Display *ClientDisplay;
    Window ClientWindow;
    GLXContext GlContext;
    GLXFBConfig FBConfig;
    XVisualInfo *VisualInfo;

    // NOTE(yigit): Debug - platform side only, the game never registers a callback.
    PFNGLDEBUGMESSAGECALLBACKPROC glDebugMessageCallback;
};

/*
  NOTE(yigit): Unlike WGL, GLX will not let us pick a pixel format after the
  window exists - the window has to be created with the visual that belongs to
  the framebuffer config we intend to render into.  So context creation splits
  in two:

    XChooseGLVisual  - before XCreateWindow, fills Context->FBConfig and
                       Context->VisualInfo (use its visual/depth/colormap for
                       the window).
    XInitOpenGL      - after XCreateWindow, makes the 3.3 core context current
                       and loads every entry point the game needs.

  Both return false on failure.  If XInitOpenGL fails the caller MUST NOT run
  the game loop, because every entry point in Memory->OpenGL would be null and
  the first draw call would go through a null pointer.
*/
bool32 XChooseGLVisual(x_opengl_context *Context, Display *ClientDisplay, int ScreenIndex);
bool32 XInitOpenGL(x_opengl_context *Context, Display *ClientDisplay, Window ClientWindow,
                   game_memory *Memory);
void XOpenGLRender(x_opengl_context *Context, int Width, int Height);
void XOpenGLShutdown(x_opengl_context *Context);

#define X_OPENGL_H
#endif
