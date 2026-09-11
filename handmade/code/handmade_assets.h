#if !defined(HANDMADE_ASSETS_H)
/*
  NOTE(yigit): Everything that turns bytes on disk into something the GPU
  holds - textures, fonts and models.

  Nothing here is called per frame.  All of it runs once, from GameInitScene.

  The two stb implementations live here because this is the only code that uses
  them, which keeps their 13000 lines out of every other file.
*/

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#pragma warning(push, 0)
#include "stb_image.h"
#pragma warning(pop)

// stb_truetype rasterizes glyph outlines from a .ttf - the part of text
// rendering that is genuinely hard and teaches nothing about rendering.
#define STB_TRUETYPE_IMPLEMENTATION
#pragma warning(push, 0)
#include "stb_truetype.h"
#pragma warning(pop)

/*
  Reads an image file and hands it to the GPU.

  NOTE(yigit): No OpenGL in here.  Decoding an image is CPU work that any
  backend would do identically; only the upload is api-specific, and that is
  one call at the bottom.  The same split the OBJ loader already had, where
  ObjLoadModel produces plain vertex and index arrays and knows nothing about
  how they reach the GPU.
*/
internal texture_handle
GameLoadTexture(thread_context *Thread, game_memory *Memory, renderer *Renderer,
                const char *FileName, texture_wrap Wrap)
{
    texture_handle Result = {};

    debug_read_file_result File = Memory->DEBUGPlatformReadEntireFile(Thread, FileName);
    if(!File.Contents)
    {
        Memory->DEBUGPlatformLog(Thread, "ERROR: Failed to read texture file: ");
        Memory->DEBUGPlatformLog(Thread, FileName);
        Memory->DEBUGPlatformLog(Thread, "\n");
        return(Result);
    }

    // NOTE(yigit): The one convention that is arguably still leaking.  OpenGL
    // puts a texture's (0,0) at the BOTTOM left and most image formats store
    // rows top-down, so the rows are reversed here.  Vulkan's origin is the
    // top left and would not want this.  It stays for now because the flip is
    // a property of the decoded data rather than of the upload, and there is
    // nothing to compare against until a second backend exists.
    stbi_set_flip_vertically_on_load(1);

    int32 Width, Height, ChannelCount;
    uint8 *Pixels = stbi_load_from_memory((const uint8 *)File.Contents, (int32)File.ContentsSize,
                                          &Width, &Height, &ChannelCount, 0);
    Memory->DEBUGPlatformFreeFileMemory(Thread, File.Contents);

    if(Pixels)
    {
        // Mipmaps because a surface texture IS seen at a distance - a wall at
        // the far end of the atrium covers fewer pixels than it has texels,
        // and without them that shimmers as the camera moves.
        Result = RendererUploadTexture(Renderer, Pixels,
                                       (uint32)Width, (uint32)Height, (uint32)ChannelCount,
                                       Wrap, TextureFilter_LinearMipmap);

        stbi_image_free(Pixels);
    }
    else
    {
        Memory->DEBUGPlatformLog(Thread, "ERROR: Failed to decode texture: ");
        Memory->DEBUGPlatformLog(Thread, FileName);
        Memory->DEBUGPlatformLog(Thread, "\n");
    }

    return(Result);
}

// Builds "data\textures\sponza_thorn_diff.png" out of "data\sponza.obj" and
// "textures\sponza_thorn_diff.png".
//
// NOTE(yigit): A .mtl names its textures relative to ITSELF, not to the working
// directory, so the model's own folder has to be pasted back on.  Without this
// the loader looks for the texture beside the executable and finds nothing.
//
// The whole directory prefix is kept rather than just the last component,
// which is what makes a map name with a subfolder in it work.
internal void
ObjMakeSiblingFileName(char *Dest, uint32 DestSize, const char *ObjName,
                       const char *SiblingName)
{
    uint32 DirLength = 0;
    for(uint32 I = 0; ObjName[I]; ++I)
    {
        if((ObjName[I] == '\\') || (ObjName[I] == '/'))
        {
            DirLength = I + 1;
        }
    }

    uint32 Length = 0;
    while((Length < DirLength) && (Length < (DestSize - 1)))
    {
        Dest[Length] = ObjName[Length];
        ++Length;
    }

    for(uint32 I = 0; SiblingName[I] && (Length < (DestSize - 1)); ++I)
    {
        Dest[Length++] = SiblingName[I];
    }

    Dest[Length] = 0;
}

// Turns "data\sponza.obj" into "data\sponza.mtl".
//
// NOTE(yigit): Derived from the model's own name rather than read from the
// OBJ's own "mtllib" line.  That line names whatever the exporter happened to
// call the file, which is not always what is actually sitting next to the
// model - and the name beside the model is the one that exists.
internal void
ObjMakeMaterialFileName(char *Dest, uint32 DestSize, const char *ObjName)
{
    uint32 Length = 0;
    while(ObjName[Length] && (Length < (DestSize - 1)))
    {
        Dest[Length] = ObjName[Length];
        ++Length;
    }
    Dest[Length] = 0;

    if((Length >= 4) &&
       (Dest[Length-4] == '.') && (Dest[Length-3] == 'o') &&
       (Dest[Length-2] == 'b') && (Dest[Length-1] == 'j'))
    {
        Dest[Length-3] = 'm';
        Dest[Length-2] = 't';
        Dest[Length-1] = 'l';
    }
}

/*
  Bakes a .ttf into a single-channel atlas and a table of glyph quads.

  NOTE(yigit): stbtt_BakeFontBitmap bakes ONE pixel size.  Scaling the result
  goes blurry or blocky, which is fine for a debug overlay drawn at 1:1 and is
  the reason scalable text needs signed distance fields instead.
*/
internal loaded_font
GameLoadFont(thread_context *Thread, game_memory *Memory, renderer *Renderer,
             memory_arena *Arena, const char *FileName, real32 PixelHeight)
{
    loaded_font Result = {};
    Result.LineHeight = PixelHeight;

    debug_read_file_result File = Memory->DEBUGPlatformReadEntireFile(Thread, FileName);
    if(!File.Contents)
    {
        Memory->DEBUGPlatformLog(Thread, "ERROR: Failed to read font: ");
        Memory->DEBUGPlatformLog(Thread, FileName);
        Memory->DEBUGPlatformLog(Thread, "\n");
        return(Result);
    }

    // Scratch, out of the transient arena - the bitmap is dead the moment it
    // reaches the GPU, and so is the stb-side glyph table once it has been
    // converted into font_glyphs below.
    uint32 AtlasDim = 512;
    uint8 *Bitmap = PushArray(Arena, AtlasDim*AtlasDim, uint8);
    stbtt_bakedchar *Baked = PushArray(Arena, FONT_CHAR_COUNT, stbtt_bakedchar);

    int32 BakeResult = stbtt_BakeFontBitmap((const uint8 *)File.Contents, 0, PixelHeight,
                                            Bitmap, (int32)AtlasDim, (int32)AtlasDim,
                                            FONT_FIRST_CHAR, FONT_CHAR_COUNT, Baked);

    Memory->DEBUGPlatformFreeFileMemory(Thread, File.Contents);

    // A negative return means the atlas was too small to hold every glyph.
    if(BakeResult <= 0)
    {
        Memory->DEBUGPlatformLog(Thread, "ERROR: Font atlas too small: ");
        Memory->DEBUGPlatformLog(Thread, FileName);
        Memory->DEBUGPlatformLog(Thread, "\n");
        return(Result);
    }

    // Ask stb where each glyph sits relative to a pen at the origin, once, and
    // keep the answer.  Doing this per character per frame would call into
    // stb_truetype for every letter drawn.
    for(uint32 I = 0; I < FONT_CHAR_COUNT; ++I)
    {
        real32 PenX = 0.0f;
        real32 PenY = 0.0f;
        stbtt_aligned_quad Quad;

        // The last argument is the fill rule.  1 is the one that puts the quad
        // on integer pixel boundaries, which is what keeps glyphs crisp.
        stbtt_GetBakedQuad(Baked, (int32)AtlasDim, (int32)AtlasDim, (int32)I,
                           &PenX, &PenY, &Quad, 1);

        font_glyph *Glyph = Result.Glyphs + I;
        Glyph->X0 = Quad.x0;  Glyph->Y0 = Quad.y0;
        Glyph->X1 = Quad.x1;  Glyph->Y1 = Quad.y1;
        Glyph->U0 = Quad.s0;  Glyph->V0 = Quad.t0;
        Glyph->U1 = Quad.s1;  Glyph->V1 = Quad.t1;

        // PenX was advanced by the call, which is exactly the advance width.
        Glyph->XAdvance = PenX;
    }

    // One channel: the atlas stores COVERAGE, not colour - how much of each
    // pixel the glyph covers.  The colour comes from a uniform, so storing it
    // per texel would waste three bytes on a value that never varies.
    //
    // ClampToEdge because glyphs are packed edge to edge: a filtered sample at
    // one glyph'''s border would otherwise bleed in a sliver of whatever sits on
    // the far side of the atlas.  And no mipmaps, because text is drawn at 1:1
    // and never minified, so they would cost memory and never be sampled.
    Result.Texture = RendererUploadTexture(Renderer, Bitmap, AtlasDim, AtlasDim, 1,
                                           TextureWrap_ClampToEdge,
                                           TextureFilter_Linear);

    return(Result);
}

// Loads a texture named by a material, reusing one already uploaded if an
// earlier submesh named the same file.
//
// NOTE(yigit): The submeshes built so far ARE the cache - a linear scan over a
// few dozen of them, cheaper than any structure built to avoid it.  It checks
// BOTH name fields, because one material's diffuse map can be another
// material's alpha mask, and uploading it twice would be silent waste.
internal texture_handle
GameLoadMaterialTexture(thread_context *Thread, game_memory *Memory, renderer *Renderer,
                        const char *ObjFileName, const char *MapName,
                        render_submesh *Submeshes, uint32 SubmeshCount)
{
    for(uint32 I = 0; I < SubmeshCount; ++I)
    {
        render_submesh *Other = Submeshes + I;

        if(IsValidHandle(Other->DiffuseTexture) &&
           ObjNamesMatch2(Other->Material.DiffuseMapName, MapName))
        {
            return(Other->DiffuseTexture);
        }

        if(IsValidHandle(Other->AlphaTexture) &&
           ObjNamesMatch2(Other->Material.AlphaMapName, MapName))
        {
            return(Other->AlphaTexture);
        }
    }

    char TextureFileName[256];
    ObjMakeSiblingFileName(TextureFileName, sizeof(TextureFileName), ObjFileName, MapName);

    return(GameLoadTexture(Thread, Memory, Renderer, TextureFileName,
                           TextureWrap_Repeat));
}

// Reads an OBJ, de-duplicates it into a vertex/index pair, and hands both to
// the GPU.  Chapters 18-20 of the book, without Assimp.
//
// NOTE(yigit): Arena is scratch and gets RESET on entry, so the caller must
// not keep anything in it.  A real model needs hundreds of megabytes to parse
// and none of it survives this call - glBufferData copies the vertices, so the
// CPU-side arrays are dead the moment they reach the GPU.
internal render_model
GameLoadModel(thread_context *Thread, game_memory *Memory, renderer *Renderer,
              memory_arena *Arena, const char *FileName)
{
    render_model Result = {};

    ResetArena(Arena);

    debug_read_file_result File = Memory->DEBUGPlatformReadEntireFile(Thread, FileName);
    if(!File.Contents)
    {
        Memory->DEBUGPlatformLog(Thread, "ERROR: Failed to read OBJ file: ");
        Memory->DEBUGPlatformLog(Thread, FileName);
        Memory->DEBUGPlatformLog(Thread, "\n");
        return(Result);
    }

    loaded_model Model = ObjLoadModel(Arena, (char *)File.Contents, File.ContentsSize);

    char Message[256];
    snprintf(Message, sizeof(Message),
             "%s: %u vertices, %u indices, %u triangles, %u submeshes, %u MB of scratch\n",
             FileName, Model.VertexCount, Model.IndexCount, Model.IndexCount / 3,
             Model.SubmeshCount, (uint32)(Arena->Used / (1024*1024)));
    Memory->DEBUGPlatformLog(Thread, Message);

    // NOTE(yigit): File.Contents is NOT freed here.  Model.Materials holds
    // names that point straight into this buffer, and they are still needed to
    // match against the .mtl below - freeing early is a use-after-free that
    // would read whatever the allocator handed out next.
    if(!Model.IndexCount)
    {
        Memory->DEBUGPlatformFreeFileMemory(Thread, File.Contents);
        return(Result);
    }

    Result.Mesh = RendererUploadMesh(Renderer,
                                     Model.Vertices, Model.VertexCount,
                                     Model.Indices, Model.IndexCount);
    Result.IndexCount = Model.IndexCount;

    // ---- Materials ------------------------------------------------------
    //
    // The .mtl is optional.  Without one every submesh keeps
    // ObjDefaultMaterial's grey, which is visibly placeholder rather than
    // invisible or black.
    char MaterialFileName[256];
    ObjMakeMaterialFileName(MaterialFileName, sizeof(MaterialFileName), FileName);

    obj_material *Materials = PushArray(Arena, Model.MaterialCount, obj_material);
    debug_read_file_result MaterialFile =
        Memory->DEBUGPlatformReadEntireFile(Thread, MaterialFileName);

    ObjParseMaterialLibrary((char *)MaterialFile.Contents, MaterialFile.ContentsSize,
                            Model.Materials, Model.MaterialCount, Materials);

    if(MaterialFile.Contents)
    {
        Memory->DEBUGPlatformFreeFileMemory(Thread, MaterialFile.Contents);
    }
    else
    {
        Memory->DEBUGPlatformLog(Thread, "  no material library at ");
        Memory->DEBUGPlatformLog(Thread, MaterialFileName);
        Memory->DEBUGPlatformLog(Thread, " - using defaults\n");
    }

    // The OBJ-side submesh list lives in scratch that the next load resets, so
    // it is copied into the render_model rather than pointed at.
    for(uint32 I = 0; I < Model.SubmeshCount; ++I)
    {
        if(Result.SubmeshCount >= MAX_SUBMESHES_PER_MODEL)
        {
            Memory->DEBUGPlatformLog(Thread, "WARNING: submesh limit hit, tail dropped\n");
            break;
        }

        render_submesh *Submesh = Result.Submeshes + Result.SubmeshCount++;
        Submesh->FirstIndex = Model.Submeshes[I].FirstIndex;
        Submesh->IndexCount = Model.Submeshes[I].IndexCount;
        Submesh->Material = Materials[Model.Submeshes[I].MaterialIndex];
        Submesh->DiffuseTexture = {};
        Submesh->AlphaTexture = {};

        if(Submesh->Material.HasDiffuseMap)
        {
            Submesh->DiffuseTexture =
                GameLoadMaterialTexture(Thread, Memory, Renderer, FileName,
                                        Submesh->Material.DiffuseMapName,
                                        Result.Submeshes, Result.SubmeshCount - 1);
        }

        if(Submesh->Material.HasAlphaMap)
        {
            Submesh->AlphaTexture =
                GameLoadMaterialTexture(Thread, Memory, Renderer, FileName,
                                        Submesh->Material.AlphaMapName,
                                        Result.Submeshes, Result.SubmeshCount - 1);
        }
    }

    // Safe now - every name has been resolved into a material by value.
    Memory->DEBUGPlatformFreeFileMemory(Thread, File.Contents);

    return(Result);
}

#define HANDMADE_ASSETS_H
#endif
