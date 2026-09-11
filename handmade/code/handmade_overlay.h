#if !defined(HANDMADE_OVERLAY_H)
/*
  NOTE(yigit): 2D screen-space drawing - the thing debug text and a HUD get
  drawn with.  Everything the 3D path does is wrong for this:

    - Geometry is STATIC there and uploaded once.  Here it changes every frame,
      because the text changes every frame.
    - Fragments are opaque there, and cut-outs use discard.  Glyph edges are
      ANTIALIASED, which is a partial coverage that discard cannot express -
      that needs real blending.
    - Depth matters there.  An overlay sits on top of everything by definition.

  So it gets its own VAO, its own buffer, its own shader and its own state.
  Quads accumulate into a CPU-side array during the frame and go to the GPU in
  one upload at the end.
*/

// 6 vertices per quad - two triangles, no index buffer.  An EBO would save two
// vertices in four, which at 16 bytes each is not worth a second buffer to
// upload and keep in sync every frame.
#define OVERLAY_MAX_QUADS 4096
#define OVERLAY_MAX_VERTICES (6 * OVERLAY_MAX_QUADS)

// 16 bytes.  Position is in PIXELS, not clip space - Mat4Ortho does that
// conversion, so callers think in screen coordinates throughout.
struct overlay_vertex
{
    vec2 Position;
    vec2 TexCoord;
};

// ASCII 32 (space) through 126 (~).  Everything a debug overlay needs, and it
// keeps the atlas small enough to bake in one go.
#define FONT_FIRST_CHAR  32
#define FONT_CHAR_COUNT  95

/*
  One glyph, already resolved into what a quad needs.

  NOTE(yigit): Deliberately NOT stbtt_bakedchar.  stb_truetype is only needed
  where the font is baked, and keeping its types out of here means this header
  depends on nothing but the maths types - and that game_state can hold a font
  without dragging a 5000-line header into everything that includes handmade.h.

  The offsets are relative to the PEN, which sits on the baseline.  Y0 is
  therefore negative for most glyphs, because they rise above it, and positive
  at the bottom of a descender like 'y'.
*/
struct font_glyph
{
    real32 X0, Y0, X1, Y1;      // pixel offsets from the pen
    real32 U0, V0, U1, V1;      // where it lives in the atlas
    real32 XAdvance;            // how far the pen moves after drawing it
};

struct loaded_font
{
    texture_handle Texture;     // single channel: coverage, not colour
    font_glyph Glyphs[FONT_CHAR_COUNT];
    real32 LineHeight;
};

struct overlay
{
    uint32 VAO;
    uint32 VBO;

    // NOTE(yigit): Lives in WorldArena, not on the stack and not re-allocated.
    // It is refilled from zero every frame, so it is scratch in use but
    // permanent in lifetime.
    overlay_vertex *Vertices;
    uint32 VertexCount;
};

internal void
OverlayInitialize(overlay *Overlay, game_opengl_api *GL, memory_arena *Arena)
{
    Overlay->Vertices = PushArray(Arena, OVERLAY_MAX_VERTICES, overlay_vertex);
    Overlay->VertexCount = 0;

    GL->glGenVertexArrays(1, &Overlay->VAO);
    GL->glGenBuffers(1, &Overlay->VBO);

    GL->glBindVertexArray(Overlay->VAO);
    GL->glBindBuffer(GL_ARRAY_BUFFER, Overlay->VBO);

    // NOTE(yigit): Allocated at full size ONCE with a null pointer, so the
    // driver reserves the storage now and glBufferSubData below only ever
    // writes into it.  GL_DYNAMIC_DRAW is the hint that this will be rewritten
    // often - calling glBufferData every frame instead would ask the driver to
    // reallocate every frame, which is the classic way to make a dynamic
    // buffer slow.
    GL->glBufferData(GL_ARRAY_BUFFER, OVERLAY_MAX_VERTICES * sizeof(overlay_vertex),
                     0, GL_DYNAMIC_DRAW);

    GL->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(overlay_vertex),
                              (void *)0);
    GL->glEnableVertexAttribArray(0);

    GL->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(overlay_vertex),
                              (void *)(2 * sizeof(real32)));
    GL->glEnableVertexAttribArray(1);

    GL->glBindVertexArray(0);
}

// Call once at the top of each frame, before anything pushes.
inline void
OverlayReset(overlay *Overlay)
{
    Overlay->VertexCount = 0;
}

// One axis-aligned rectangle at (X, Y) with size (W, H), in pixels, sampling
// the sub-rectangle (U0,V0)..(U1,V1) of whatever texture is bound at flush.
//
// Wound counter-clockwise, matching the rest of the renderer, so this still
// works if face culling is ever turned on.
internal void
OverlayPushQuad(overlay *Overlay, real32 X, real32 Y, real32 W, real32 H,
                real32 U0, real32 V0, real32 U1, real32 V1)
{
    // Dropping the quad is the right failure here.  Growing would mean a
    // reallocation mid-frame and a buffer the GPU is already looking at.
    if((Overlay->VertexCount + 6) > OVERLAY_MAX_VERTICES)
    {
        return;
    }

    overlay_vertex *V = Overlay->Vertices + Overlay->VertexCount;
    Overlay->VertexCount += 6;

    real32 X1 = X + W;
    real32 Y1 = Y + H;

    V[0].Position = Vec2(X,  Y ); V[0].TexCoord = Vec2(U0, V0);
    V[1].Position = Vec2(X1, Y ); V[1].TexCoord = Vec2(U1, V0);
    V[2].Position = Vec2(X1, Y1); V[2].TexCoord = Vec2(U1, V1);

    V[3].Position = Vec2(X,  Y ); V[3].TexCoord = Vec2(U0, V0);
    V[4].Position = Vec2(X1, Y1); V[4].TexCoord = Vec2(U1, V1);
    V[5].Position = Vec2(X,  Y1); V[5].TexCoord = Vec2(U0, V1);
}

/*
  Walks a string, pushing one quad per glyph.  X and Y name the left end of the
  BASELINE, not the top-left corner - that is the convention every text system
  uses, because it is what keeps letters lined up when they have different
  heights.

  NOTE(yigit): This assumes the overlay projection has its origin at the TOP
  left with Y growing downward, which is how Mat4Ortho is called for the
  overlay pass.  Glyph offsets come out of stb_truetype in that convention, so
  matching it here means no per-quad flipping.

  Returns where the pen ended up, so callers can chain without re-measuring.
*/
internal real32
OverlayPushText(overlay *Overlay, loaded_font *Font, real32 X, real32 Y,
                const char *Text)
{
    real32 PenX = X;

    for(const char *C = Text; *C; ++C)
    {
        // Anything outside the baked range is skipped rather than drawn as
        // garbage - a stray byte should not index off the end of the array.
        if((*C < FONT_FIRST_CHAR) || (*C >= (FONT_FIRST_CHAR + FONT_CHAR_COUNT)))
        {
            continue;
        }

        font_glyph *Glyph = Font->Glyphs + (*C - FONT_FIRST_CHAR);

        OverlayPushQuad(Overlay,
                        PenX + Glyph->X0, Y + Glyph->Y0,
                        Glyph->X1 - Glyph->X0, Glyph->Y1 - Glyph->Y0,
                        Glyph->U0, Glyph->V0, Glyph->U1, Glyph->V1);

        // Advance even for a space, which has a zero-size quad but a real
        // width.  Pushing the empty quad costs six vertices and no pixels, so
        // it is not worth a branch.
        PenX += Glyph->XAdvance;
    }

    return(PenX);
}

// Uploads everything pushed this frame and draws it in one call.
//
// NOTE(yigit): State is saved and restored around the draw by hand, because
// there is no render state system yet - the 3D pass that runs next frame would
// otherwise inherit blending and a disabled depth test and quietly break.
// This is exactly the kind of bookkeeping a push buffer exists to remove.
internal void
OverlayFlush(overlay *Overlay, game_opengl_api *GL, uint32 Program,
             mat4 Projection, uint32 Texture, vec3 Color)
{
    if(!Overlay->VertexCount)
    {
        return;
    }

    GL->glBindBuffer(GL_ARRAY_BUFFER, Overlay->VBO);
    GL->glBufferSubData(GL_ARRAY_BUFFER, 0,
                        Overlay->VertexCount * sizeof(overlay_vertex),
                        Overlay->Vertices);

    GL->glUseProgram(Program);

    // NOTE(yigit): Straight to GL rather than through handmade_shader.h's
    // setters.  That header includes handmade.h, and handmade.h includes this
    // one so game_state can hold an overlay by value - using the helpers would
    // close the circle.  These are one-liners anyway.
    GL->glUniformMatrix4fv(GL->glGetUniformLocation(Program, "projection"),
                           1, GL_FALSE, Projection.E);
    GL->glUniform3f(GL->glGetUniformLocation(Program, "color"),
                    Color.X, Color.Y, Color.Z);
    GL->glUniform1i(GL->glGetUniformLocation(Program, "atlas"), 0);

    GL->glActiveTexture(GL_TEXTURE0);
    GL->glBindTexture(GL_TEXTURE_2D, Texture);

    // Standard "over": the incoming fragment contributes its own alpha, and
    // what is already on screen contributes the rest.
    GL->glEnable(GL_BLEND);
    GL->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // NOTE(yigit): The depth TEST is off so the overlay always wins, and the
    // depth WRITE is off so it does not leave a mark that occludes the scene
    // on the next frame.  Turning off only the test would still write.
    GL->glDisable(GL_DEPTH_TEST);
    GL->glDepthMask(GL_FALSE);

    GL->glBindVertexArray(Overlay->VAO);
    GL->glDrawArrays(GL_TRIANGLES, 0, (int32)Overlay->VertexCount);
    GL->glBindVertexArray(0);

    GL->glDepthMask(GL_TRUE);
    GL->glEnable(GL_DEPTH_TEST);
    GL->glDisable(GL_BLEND);
}

#define HANDMADE_OVERLAY_H
#endif
