#if !defined(HANDMADE_ASSETS_H)
/*
  NOTE(yigit): Everything that turns bytes on disk into something the GPU
  holds - textures, fonts and models.  Split out of handmade.cpp when that file
  passed 900 lines and asset loading was half of it.

  Nothing here is called per frame.  All of it runs once, from GameInitOpenGL.

  The two stb implementations live here rather than in handmade.cpp because
  this is the only code that uses them, and it keeps their 13000 lines out of
  the file you actually read.
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

internal uint32
GameLoadTexture(thread_context *Thread, game_memory *Memory, game_opengl_api *GL,
                const char *FileName, uint32 WrapMode)
{
    uint32 Result = 0;

    debug_read_file_result File = Memory->DEBUGPlatformReadEntireFile(Thread, FileName);
    if(!File.Contents)
    {
        Memory->DEBUGPlatformLog(Thread, "ERROR: Failed to read texture file: ");
        Memory->DEBUGPlatformLog(Thread, FileName);
        Memory->DEBUGPlatformLog(Thread, "\n");
        return(Result);
    }

    stbi_set_flip_vertically_on_load(1); // OpenGL's (0,0) is bottom-left

    int32 Width, Height, ChannelCount;
    uint8 *Pixels = stbi_load_from_memory((const uint8 *)File.Contents, (int32)File.ContentsSize,
                                          &Width, &Height, &ChannelCount, 0);
    Memory->DEBUGPlatformFreeFileMemory(Thread, File.Contents);

    if(Pixels)
    {
        // NOTE(yigit): One and two channel images are real - Sponza ships its
        // alpha masks as greyscale PNGs.  Assuming three channels for anything
        // that is not RGBA reads three bytes per pixel out of a buffer holding
        // one, which walks off the end of the allocation.
        //
        // GL_UNPACK_ALIGNMENT is 1 below, which matters here: a greyscale row
        // is rarely a multiple of four bytes, and the default alignment of 4
        // would shear the image.
        uint32 Format = GL_RGB;
        switch(ChannelCount)
        {
            case 1: { Format = GL_RED; } break;
            case 2: { Format = GL_RG; } break;
            case 3: { Format = GL_RGB; } break;
            case 4: { Format = GL_RGBA; } break;
        }

        GL->glGenTextures(1, &Result);
        GL->glBindTexture(GL_TEXTURE_2D, Result);

        GL->glPixelStorei(GL_UNPACK_ALIGNMENT, 1); // rows are packed, not padded to 4

        GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, WrapMode);
        GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, WrapMode);
        GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        GL->glTexImage2D(GL_TEXTURE_2D, 0, (int32)Format, Width, Height, 0,
                         Format, GL_UNSIGNED_BYTE, Pixels);
        GL->glGenerateMipmap(GL_TEXTURE_2D);

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

// Builds "data\Tyre.png" from "data\peugeot.obj" and "Tyre.png".
//
// NOTE(yigit): A .mtl names its textures relative to itself, not to the working
// directory, so the model's own folder has to be pasted back on.  Without this
// the loader looks for "Tyre.png" beside the executable and finds nothing.
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

// Turns "data\peugeot.obj" into "data\peugeot.mtl".
//
// NOTE(yigit): Derived from the model's own name rather than read from the
// OBJ's "mtllib" line, which for this model names a file that was never
// distributed with it.  The name beside the model is the one that exists.
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
GameLoadFont(thread_context *Thread, game_memory *Memory, game_opengl_api *GL,
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

        // The last argument is the fill rule: 1 for OpenGL's, which is what
        // puts the quad on integer pixel boundaries and keeps glyphs crisp.
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

    GL->glGenTextures(1, &Result.Texture);
    GL->glBindTexture(GL_TEXTURE_2D, Result.Texture);

    // NOTE(yigit): Alignment 1 is required, not optional.  The atlas is one
    // byte per pixel and 512 wide - the default alignment of 4 would be fine
    // here by luck, but any other width would shear the whole atlas.
    GL->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // CLAMP_TO_EDGE, not REPEAT: glyphs are packed edge to edge, and a
    // filtered sample at the border of one would otherwise bleed in a sliver
    // of the glyph on the far side of the atlas.
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // No mipmaps.  Text is drawn at 1:1 and never minified, so they would cost
    // memory to build and never be sampled.
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    GL->glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, (int32)AtlasDim, (int32)AtlasDim, 0,
                     GL_RED, GL_UNSIGNED_BYTE, Bitmap);

    return(Result);
}

// Loads a texture named by a material, reusing one already uploaded if an
// earlier submesh named the same file.
//
// NOTE(yigit): The submeshes built so far ARE the cache - a linear scan over a
// few dozen of them, cheaper than any structure built to avoid it.  It checks
// BOTH name fields, because one material's diffuse map can be another
// material's alpha mask, and uploading it twice would be silent waste.
internal uint32
GameLoadMaterialTexture(thread_context *Thread, game_memory *Memory, game_opengl_api *GL,
                        const char *ObjFileName, const char *MapName,
                        render_submesh *Submeshes, uint32 SubmeshCount)
{
    for(uint32 I = 0; I < SubmeshCount; ++I)
    {
        render_submesh *Other = Submeshes + I;

        if(Other->DiffuseTexture &&
           ObjNamesMatch2(Other->Material.DiffuseMapName, MapName))
        {
            return(Other->DiffuseTexture);
        }

        if(Other->AlphaTexture &&
           ObjNamesMatch2(Other->Material.AlphaMapName, MapName))
        {
            return(Other->AlphaTexture);
        }
    }

    char TextureFileName[256];
    ObjMakeSiblingFileName(TextureFileName, sizeof(TextureFileName), ObjFileName, MapName);

    return(GameLoadTexture(Thread, Memory, GL, TextureFileName, GL_REPEAT));
}

// Reads an OBJ, de-duplicates it into a vertex/index pair, and hands both to
// the GPU.  Chapters 18-20 of the book, without Assimp.
//
// NOTE(yigit): Arena is scratch and gets RESET on entry, so the caller must
// not keep anything in it.  A real model needs hundreds of megabytes to parse
// and none of it survives this call - glBufferData copies the vertices, so the
// CPU-side arrays are dead the moment they reach the GPU.
internal render_model
GameLoadModel(thread_context *Thread, game_memory *Memory, game_opengl_api *GL,
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

    GL->glGenVertexArrays(1, &Result.VAO);
    GL->glGenBuffers(1, &Result.VBO);
    GL->glGenBuffers(1, &Result.EBO);

    GL->glBindVertexArray(Result.VAO);

    GL->glBindBuffer(GL_ARRAY_BUFFER, Result.VBO);
    GL->glBufferData(GL_ARRAY_BUFFER, Model.VertexCount * sizeof(obj_vertex),
                     Model.Vertices, GL_STATIC_DRAW);

    // NOTE(yigit): Unlike GL_ARRAY_BUFFER, the ELEMENT buffer binding is stored
    // IN THE VAO.  So it has to be bound while the VAO is bound, and it must
    // not be unbound before the VAO is - unbind it first and the VAO forgets
    // its index buffer and the model draws nothing.  Same ordering trap as the
    // EBO in ch. 6.
    GL->glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, Result.EBO);
    GL->glBufferData(GL_ELEMENT_ARRAY_BUFFER, Model.IndexCount * sizeof(uint32),
                     Model.Indices, GL_STATIC_DRAW);

    // obj_vertex was laid out to match an 8-float stride exactly, so these are
    // the three usual calls with sizeof doing the arithmetic.
    GL->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(obj_vertex),
                              (void *)0);
    GL->glEnableVertexAttribArray(0);

    GL->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(obj_vertex),
                              (void *)(3 * sizeof(real32)));
    GL->glEnableVertexAttribArray(1);

    GL->glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(obj_vertex),
                              (void *)(6 * sizeof(real32)));
    GL->glEnableVertexAttribArray(2);

    GL->glBindVertexArray(0);

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
        Submesh->DiffuseTexture = 0;
        Submesh->AlphaTexture = 0;

        if(Submesh->Material.HasDiffuseMap)
        {
            Submesh->DiffuseTexture =
                GameLoadMaterialTexture(Thread, Memory, GL, FileName,
                                        Submesh->Material.DiffuseMapName,
                                        Result.Submeshes, Result.SubmeshCount - 1);
        }

        if(Submesh->Material.HasAlphaMap)
        {
            Submesh->AlphaTexture =
                GameLoadMaterialTexture(Thread, Memory, GL, FileName,
                                        Submesh->Material.AlphaMapName,
                                        Result.Submeshes, Result.SubmeshCount - 1);
        }
    }

    // Safe now - every name has been resolved into a material by value.
    Memory->DEBUGPlatformFreeFileMemory(Thread, File.Contents);

    return(Result);
}

// Makes the fallback texture: a single white pixel.
internal uint32
GameCreateWhiteTexture(game_opengl_api *GL)
{
    uint32 Result = 0;
    uint8 White[4] = {255, 255, 255, 255};

    GL->glGenTextures(1, &Result);
    GL->glBindTexture(GL_TEXTURE_2D, Result);
    GL->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    GL->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    GL->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, White);

    return(Result);
}

#define HANDMADE_ASSETS_H
#endif
