#if !defined(HANDMADE_OBJ_H)
/*
  NOTE(yigit): Wavefront OBJ loading, replacing the book's use of Assimp
  (ch. 18-20).  Assimp reads forty formats; this reads one, which is why it
  fits in a header instead of a library.

  OBJ is line-based plain text.  Four line types carry the geometry:

    v  x y z          a position
    vt u v            a texture coordinate
    vn x y z          a normal
    f  a/b/c d/e/f .. a face, indexing the three lists SEPARATELY

  Everything else - comments, mtllib, usemtl, o, g, s - is skipped for now.

  Bytes arrive from the platform layer the same way shader source and image
  data do; this file never opens anything itself.
*/

// What one pass over the file found.  Counting first means the second pass can
// allocate exactly once out of the arena instead of guessing or growing.
struct obj_counts
{
    uint32 PositionCount;
    uint32 TexCoordCount;
    uint32 NormalCount;

    // Faces can be triangles, quads or larger.  A face with N corners becomes
    // N-2 triangles, so this is the count AFTER that split - the number of
    // triangles the GPU will actually be asked to draw.
    uint32 TriangleCount;

    // Every v/vt/vn triple mentioned by every face, before duplicates are
    // merged.  The upper bound on how many unique vertices can exist, and so
    // the size of the vertex buffer to allocate.
    uint32 FaceVertexCount;

    // How many "usemtl" lines the file has.  Not the number of DISTINCT
    // materials - a file switches back and forth - but an upper bound on it,
    // which is all an arena needs.
    uint32 UsemtlCount;
};

// ---------------------------------------------------------------------------
// Tokenizer
//
// NOTE(yigit): The file is walked with a single moving char pointer.  Nothing
// is copied and nothing is allocated - these just advance it and hand back
// what they passed over.
// ---------------------------------------------------------------------------

inline bool32
ObjIsEndOfLine(char C)
{
    return((C == '\n') || (C == '\r'));
}

inline bool32
ObjIsWhitespace(char C)
{
    // Deliberately does NOT treat a newline as whitespace.  Line type is
    // decided by the first token on a line, so a parser that skipped past line
    // ends while eating spaces would run the file together.
    return((C == ' ') || (C == '\t'));
}

inline void
ObjSkipWhitespace(char **At)
{
    while(**At && ObjIsWhitespace(**At))
    {
        ++(*At);
    }
}

// Moves past the end of the current line, leaving At on the first character of
// the next one.  Handles both LF and CRLF, since an OBJ authored on Windows
// will have the latter.
internal void
ObjSkipLine(char **At)
{
    while(**At && !ObjIsEndOfLine(**At))
    {
        ++(*At);
    }

    while(**At && ObjIsEndOfLine(**At))
    {
        ++(*At);
    }
}

// True when the token starting at At is exactly Keyword followed by a space or
// tab.  The trailing check is what keeps "v" from matching a "vt" or "vn"
// line, which is the first thing that goes wrong if you compare prefixes.
internal bool32
ObjLineStartsWith(char *At, const char *Keyword)
{
    while(*Keyword)
    {
        if(*At != *Keyword)
        {
            return(false);
        }
        ++At;
        ++Keyword;
    }

    return(ObjIsWhitespace(*At));
}

// ---------------------------------------------------------------------------
// Numbers
//
// NOTE(yigit): Written out rather than calling strtof/atoi, for the same
// reason there is no GLM or GLFW here.  OBJ numbers are plain decimals, with
// the occasional exponent from an exporter, so the general case a CRT parser
// handles is not needed.
// ---------------------------------------------------------------------------

inline bool32
ObjIsDigit(char C)
{
    return((C >= '0') && (C <= '9'));
}

internal real32
ObjParseReal32(char **At)
{
    char *C = *At;

    real32 Sign = 1.0f;
    if(*C == '-')      { Sign = -1.0f; ++C; }
    else if(*C == '+') { ++C; }

    real32 Result = 0.0f;
    while(ObjIsDigit(*C))
    {
        Result = Result*10.0f + (real32)(*C - '0');
        ++C;
    }

    if(*C == '.')
    {
        ++C;

        // Each digit past the point is worth a tenth of the one before it.
        real32 Place = 0.1f;
        while(ObjIsDigit(*C))
        {
            Result += (real32)(*C - '0') * Place;
            Place *= 0.1f;
            ++C;
        }
    }

    // Scientific notation - Blender and Maya both emit it for small values.
    if((*C == 'e') || (*C == 'E'))
    {
        ++C;

        bool32 NegativeExponent = false;
        if(*C == '-')      { NegativeExponent = true; ++C; }
        else if(*C == '+') { ++C; }

        int32 Exponent = 0;
        while(ObjIsDigit(*C))
        {
            Exponent = Exponent*10 + (*C - '0');
            ++C;
        }

        while(Exponent--)
        {
            Result = NegativeExponent ? (Result * 0.1f) : (Result * 10.0f);
        }
    }

    *At = C;
    return(Sign * Result);
}

// Face indices only.  Signed because OBJ allows NEGATIVE indices, which count
// backwards from the end of the list so far - -1 is the most recent vertex.
internal int32
ObjParseInt32(char **At)
{
    char *C = *At;

    int32 Sign = 1;
    if(*C == '-')      { Sign = -1; ++C; }
    else if(*C == '+') { ++C; }

    int32 Result = 0;
    while(ObjIsDigit(*C))
    {
        Result = Result*10 + (*C - '0');
        ++C;
    }

    *At = C;
    return(Sign * Result);
}

// ---------------------------------------------------------------------------
// Counting pass
// ---------------------------------------------------------------------------

internal obj_counts
ObjCountElements(char *Contents, uint32 ContentsSize)
{
    obj_counts Result = {};

    char *At = Contents;
    char *End = Contents + ContentsSize;

    while(At < End && *At)
    {
        ObjSkipWhitespace(&At);

        // NOTE(yigit): "vt" and "vn" are tested before "v".  Order alone would
        // not be enough - ObjLineStartsWith also demands whitespace after the
        // keyword, which is what stops "v" matching a "vt" line - but testing
        // the longer keywords first makes the intent obvious.
        if(ObjLineStartsWith(At, "vt"))
        {
            ++Result.TexCoordCount;
        }
        else if(ObjLineStartsWith(At, "vn"))
        {
            ++Result.NormalCount;
        }
        else if(ObjLineStartsWith(At, "v"))
        {
            ++Result.PositionCount;
        }
        else if(ObjLineStartsWith(At, "f"))
        {
            // Count the corners on this face by counting the runs of
            // non-whitespace after the "f".  A quad gives 4, which is 2
            // triangles; a triangle gives 3, which is 1.
            char *Scan = At + 1;
            uint32 CornerCount = 0;

            while(*Scan && !ObjIsEndOfLine(*Scan))
            {
                ObjSkipWhitespace(&Scan);

                if(*Scan && !ObjIsEndOfLine(*Scan))
                {
                    ++CornerCount;
                    while(*Scan && !ObjIsWhitespace(*Scan) && !ObjIsEndOfLine(*Scan))
                    {
                        ++Scan;
                    }
                }
            }

            if(CornerCount >= 3)
            {
                Result.TriangleCount += (CornerCount - 2);
                Result.FaceVertexCount += CornerCount;
            }
        }
        else if(ObjLineStartsWith(At, "usemtl"))
        {
            ++Result.UsemtlCount;
        }

        ObjSkipLine(&At);
    }

    return(Result);
}

// ---------------------------------------------------------------------------
// Fill pass - the three source lists, exactly as the file states them
//
// NOTE(yigit): These are NOT what the GPU is handed.  OBJ indexes positions,
// texcoords and normals separately, so a face corner like 5/12/3 picks one
// entry from each of these three lists.  Turning that into the single index
// per vertex a GPU index buffer needs comes later.
// ---------------------------------------------------------------------------

// One corner of one triangle, exactly as the file states it: three indices
// into three different lists.  Already converted to 0-based, with negative
// indices resolved.  A slot is -1 when the face did not supply one, which is
// what "f 1//3" means - position and normal, no texture coordinate.
struct obj_face_vertex
{
    int32 PositionIndex;
    int32 TexCoordIndex;
    int32 NormalIndex;
};

// A material NAME, as the "usemtl" line spells it.
//
// NOTE(yigit): Name points straight into the file buffer rather than copying -
// nothing is allocated, and comparing two of these is a length check and a
// byte loop.  The catch is that it dies when the caller frees the file, so
// anything that has to outlive the load must copy it first.
struct obj_material_ref
{
    char *Name;
    uint32 NameLength;
};

internal bool32
ObjNamesMatch(obj_material_ref A, char *Name, uint32 NameLength)
{
    if(A.NameLength != NameLength)
    {
        return(false);
    }

    for(uint32 I = 0; I < NameLength; ++I)
    {
        if(A.Name[I] != Name[I])
        {
            return(false);
        }
    }

    return(true);
}

struct obj_source_data
{
    vec3 *Positions;
    vec2 *TexCoords;
    vec3 *Normals;

    // Distinct materials, in order of first appearance.  A file that switches
    // back to an earlier material reuses its index rather than adding a
    // second entry, which is what makes the grouping in ObjLoadModel possible.
    obj_material_ref *Materials;
    uint32 MaterialCount;

    // Which material each triangle belongs to.  TriangleCount entries.
    uint32 *TriangleMaterials;

    // TriangleCount*3 entries, already fanned out of whatever polygons the
    // file had.  Note this is NOT Counts.FaceVertexCount: six quads give 24
    // corners but 36 triangle corners, because the fan repeats two of them
    // per extra triangle.
    obj_face_vertex *FaceVertices;
    uint32 FaceVertexCount;

    obj_counts Counts;
};

// OBJ indices are 1-BASED, and a NEGATIVE index counts backwards from the end
// of what has been read SO FAR: -1 is the most recently listed entry.
//
// NOTE(yigit): "so far" is why faces are parsed in the same pass as the
// vertices rather than in a pass of their own.  Count here is the number read
// up to this line, not the file's total - and for a file that interleaves v
// and f lines those are different numbers.
inline int32
ObjResolveIndex(int32 Index, uint32 CountSoFar)
{
    int32 Result = -1;

    if(Index > 0)
    {
        Result = Index - 1;
    }
    else if(Index < 0)
    {
        Result = (int32)CountSoFar + Index;
    }

    // A malformed file should render wrong, not read out of bounds.
    if((Result < 0) || (Result >= (int32)CountSoFar))
    {
        Result = -1;
    }

    return(Result);
}

// Parses one corner - "5", "5/12", "5//3" or "5/12/3" - and leaves At just
// past it.
internal obj_face_vertex
ObjParseFaceVertex(char **At, uint32 PositionsSoFar, uint32 TexCoordsSoFar,
                   uint32 NormalsSoFar)
{
    obj_face_vertex Result;
    Result.PositionIndex = ObjResolveIndex(ObjParseInt32(At), PositionsSoFar);
    Result.TexCoordIndex = -1;
    Result.NormalIndex = -1;

    if(**At == '/')
    {
        ++(*At);

        // An immediate second slash is the "no texture coordinate" form.
        if(**At != '/')
        {
            Result.TexCoordIndex = ObjResolveIndex(ObjParseInt32(At), TexCoordsSoFar);
        }

        if(**At == '/')
        {
            ++(*At);
            Result.NormalIndex = ObjResolveIndex(ObjParseInt32(At), NormalsSoFar);
        }
    }

    return(Result);
}

internal obj_source_data
ObjParseSourceData(memory_arena *Arena, char *Contents, uint32 ContentsSize)
{
    obj_source_data Result = {};
    Result.Counts = ObjCountElements(Contents, ContentsSize);

    // Counted first, so these are allocated once at exactly the right size.
    // A file with no texcoords or no normals allocates nothing for them.
    if(Result.Counts.PositionCount)
    {
        Result.Positions = PushArray(Arena, Result.Counts.PositionCount, vec3);
    }
    if(Result.Counts.TexCoordCount)
    {
        Result.TexCoords = PushArray(Arena, Result.Counts.TexCoordCount, vec2);
    }
    if(Result.Counts.NormalCount)
    {
        Result.Normals = PushArray(Arena, Result.Counts.NormalCount, vec3);
    }
    if(Result.Counts.TriangleCount)
    {
        Result.TriangleMaterials = PushArray(Arena, Result.Counts.TriangleCount, uint32);
        Result.FaceVertices = PushArray(Arena, 3*Result.Counts.TriangleCount,
                                        obj_face_vertex);
    }

    uint32 PositionIndex = 0;
    uint32 TexCoordIndex = 0;
    uint32 NormalIndex = 0;
    uint32 FaceVertexIndex = 0;
    uint32 TriangleIndex = 0;

    // NOTE(yigit): One slot spare on top of the usemtl count, for a file whose
    // first faces appear BEFORE any usemtl line.  Those get material 0, an
    // unnamed default, rather than being dropped.
    Result.Materials = PushArray(Arena, Result.Counts.UsemtlCount + 1, obj_material_ref);
    Result.Materials[0].Name = 0;
    Result.Materials[0].NameLength = 0;
    Result.MaterialCount = 1;

    uint32 CurrentMaterial = 0;

    char *At = Contents;
    char *End = Contents + ContentsSize;

    while(At < End && *At)
    {
        ObjSkipWhitespace(&At);

        // "vt" and "vn" are tested BEFORE "v", even though ObjLineStartsWith
        // guards against the prefix problem.  Cheaper than relying on it, and
        // it makes the intent obvious to anyone reading.
        if(ObjLineStartsWith(At, "vt"))
        {
            char *C = At + 2;
            ObjSkipWhitespace(&C);
            real32 U = ObjParseReal32(&C);
            ObjSkipWhitespace(&C);
            real32 V = ObjParseReal32(&C);

            // A third value (w) is legal here and is ignored - it is only used
            // for volumetric textures, which this renderer has none of.
            Result.TexCoords[TexCoordIndex++] = Vec2(U, V);
        }
        else if(ObjLineStartsWith(At, "vn"))
        {
            char *C = At + 2;
            ObjSkipWhitespace(&C);
            real32 X = ObjParseReal32(&C);
            ObjSkipWhitespace(&C);
            real32 Y = ObjParseReal32(&C);
            ObjSkipWhitespace(&C);
            real32 Z = ObjParseReal32(&C);

            Result.Normals[NormalIndex++] = Vec3(X, Y, Z);
        }
        else if(ObjLineStartsWith(At, "v"))
        {
            char *C = At + 1;
            ObjSkipWhitespace(&C);
            real32 X = ObjParseReal32(&C);
            ObjSkipWhitespace(&C);
            real32 Y = ObjParseReal32(&C);
            ObjSkipWhitespace(&C);
            real32 Z = ObjParseReal32(&C);

            // A fourth value (w) is legal and ignored, same as above.
            Result.Positions[PositionIndex++] = Vec3(X, Y, Z);
        }
        else if(ObjLineStartsWith(At, "f"))
        {
            char *C = At + 1;

            // Triangulated as a FAN, on the fly.  Every corner past the second
            // closes a triangle with the first corner and the one before it,
            // so a quad gives 2 triangles and a hexagon gives 4 - with no
            // buffer of corners and no upper limit on how many a face may have.
            //
            // NOTE(yigit): A fan is only correct for CONVEX faces.  Exporters
            // emit convex polygons in practice, and anything concave would need
            // real triangulation - out of scope while the book only ever loads
            // triangles and quads.
            obj_face_vertex First = {};
            obj_face_vertex Previous = {};
            uint32 CornerIndex = 0;

            for(;;)
            {
                ObjSkipWhitespace(&C);
                if(!*C || ObjIsEndOfLine(*C))
                {
                    break;
                }

                // The counts SO FAR, not the file totals - see ObjResolveIndex.
                obj_face_vertex Corner = ObjParseFaceVertex(&C, PositionIndex,
                                                            TexCoordIndex, NormalIndex);

                if(CornerIndex == 0)
                {
                    First = Corner;
                }
                else if(CornerIndex == 1)
                {
                    Previous = Corner;
                }
                else
                {
                    Result.FaceVertices[FaceVertexIndex++] = First;
                    Result.FaceVertices[FaceVertexIndex++] = Previous;
                    Result.FaceVertices[FaceVertexIndex++] = Corner;

                    // Whichever usemtl was most recently in effect owns this
                    // triangle.  A quad records the same material twice.
                    Result.TriangleMaterials[TriangleIndex++] = CurrentMaterial;

                    Previous = Corner;
                }

                ++CornerIndex;
            }
        }
        else if(ObjLineStartsWith(At, "usemtl"))
        {
            char *C = At + 6;
            ObjSkipWhitespace(&C);

            char *Name = C;
            while(*C && !ObjIsWhitespace(*C) && !ObjIsEndOfLine(*C))
            {
                ++C;
            }
            uint32 NameLength = (uint32)(C - Name);

            // Linear search, on purpose.  A model has a few dozen materials at
            // most, and it is walked once per usemtl line - a hash table here
            // would be more code than the whole loop.
            CurrentMaterial = 0;
            for(uint32 I = 1; I < Result.MaterialCount; ++I)
            {
                if(ObjNamesMatch(Result.Materials[I], Name, NameLength))
                {
                    CurrentMaterial = I;
                    break;
                }
            }

            if((CurrentMaterial == 0) && NameLength)
            {
                Assert(Result.MaterialCount <= Result.Counts.UsemtlCount);

                CurrentMaterial = Result.MaterialCount++;
                Result.Materials[CurrentMaterial].Name = Name;
                Result.Materials[CurrentMaterial].NameLength = NameLength;
            }
        }

        ObjSkipLine(&At);
    }

    // If these disagree, the counting pass and the fill pass are reading the
    // file differently - which would silently leave uninitialised vertices.
    Assert(PositionIndex == Result.Counts.PositionCount);
    Assert(TexCoordIndex == Result.Counts.TexCoordCount);
    Assert(NormalIndex == Result.Counts.NormalCount);
    Assert(FaceVertexIndex == 3*Result.Counts.TriangleCount);

    Result.FaceVertexCount = FaceVertexIndex;

    return(Result);
}

// ---------------------------------------------------------------------------
// De-duplication - the actual job
//
// OBJ says "position 5, texcoord 12, normal 3".  A GPU says "vertex 17", and
// that one index selects from one buffer.  So every distinct v/vt/vn TRIPLE
// has to become one vertex, and repeated triples have to collapse onto the
// same index or the buffer is three times bigger than it needs to be.
//
// This is the step Assimp does invisibly, and the reason the book never makes
// you think about it.
// ---------------------------------------------------------------------------

// 32 bytes: 3 position, 3 normal, 2 texcoord, an 8-float stride.  The backend
// knows this layout and describes it to the GPU when a mesh is uploaded, so
// nothing here has to say how it gets there.
struct obj_vertex
{
    vec3 Position;
    vec3 Normal;
    vec2 TexCoord;
};

// ---------------------------------------------------------------------------
// Material libraries (.mtl)
//
// A second text file, simpler than the OBJ itself.  "newmtl Name" opens a
// block and the lines under it describe that material until the next newmtl:
//
//   Kd r g b        diffuse colour       - what the surface reflects
//   Ks r g b        specular colour      - the colour of the highlight
//   Ns n            specular exponent    - bigger is a tighter highlight
//   map_Kd file     diffuse texture      - needs texture coordinates to matter
//
// NOTE(yigit): Names here are COPIED, not pointed at.  A material has to
// outlive the file it was read from, unlike obj_material_ref which dies with
// the buffer.
// ---------------------------------------------------------------------------

#define OBJ_MAX_MAP_NAME 64

struct obj_material
{
    vec3 Diffuse;
    vec3 Specular;
    real32 Shininess;

    char DiffuseMapName[OBJ_MAX_MAP_NAME];
    bool32 HasDiffuseMap;

    // Book ch. 22 - map_d, an opacity mask.  Sponza uses it for foliage and
    // chains: the geometry is a flat card and the mask is what cuts the leaf
    // shape out of it.
    //
    // NOTE(yigit): Not always present even when a material IS cut out.  This
    // distribution of Sponza has the mask merged into the alpha channel of
    // some diffuse textures and left as a separate file for others, so the
    // shader has to honour both.
    char AlphaMapName[OBJ_MAX_MAP_NAME];
    bool32 HasAlphaMap;
};

// What a material looks like when the .mtl is missing or names nothing useful.
// Mid grey with a weak highlight - obviously placeholder, never invisible.
internal obj_material
ObjDefaultMaterial(void)
{
    obj_material Result = {};
    Result.Diffuse = Vec3(0.6f, 0.6f, 0.6f);
    Result.Specular = Vec3(0.3f, 0.3f, 0.3f);
    Result.Shininess = 32.0f;
    return(Result);
}

// Two zero-terminated names, for comparing map filenames after they have been
// copied out of the file buffer.
internal bool32
ObjNamesMatch2(const char *A, const char *B)
{
    while(*A && (*A == *B))
    {
        ++A;
        ++B;
    }

    return(*A == *B);
}

internal void
ObjCopyToken(char *Dest, uint32 DestSize, char *Source, uint32 SourceLength)
{
    uint32 CopyCount = SourceLength;
    if(CopyCount > (DestSize - 1))
    {
        CopyCount = DestSize - 1;
    }

    for(uint32 I = 0; I < CopyCount; ++I)
    {
        Dest[I] = Source[I];
    }
    Dest[CopyCount] = 0;
}

// Reads a .mtl and fills OutMaterials to match Names, entry for entry.  Matching
// by name rather than by order is what lets the .mtl list its materials in any
// sequence, and list ones the model never uses.
internal void
ObjParseMaterialLibrary(char *Contents, uint32 ContentsSize,
                        obj_material_ref *Names, uint32 NameCount,
                        obj_material *OutMaterials)
{
    for(uint32 I = 0; I < NameCount; ++I)
    {
        OutMaterials[I] = ObjDefaultMaterial();
    }

    if(!Contents)
    {
        return;
    }

    char *At = Contents;
    char *End = Contents + ContentsSize;

    // Which entry of OutMaterials the current newmtl block writes to, or -1
    // when the block names a material this model never uses.
    int32 Target = -1;

    while(At < End && *At)
    {
        ObjSkipWhitespace(&At);

        if(ObjLineStartsWith(At, "newmtl"))
        {
            char *C = At + 6;
            ObjSkipWhitespace(&C);

            char *Name = C;
            while(*C && !ObjIsWhitespace(*C) && !ObjIsEndOfLine(*C))
            {
                ++C;
            }
            uint32 NameLength = (uint32)(C - Name);

            Target = -1;
            for(uint32 I = 0; I < NameCount; ++I)
            {
                if(ObjNamesMatch(Names[I], Name, NameLength))
                {
                    Target = (int32)I;
                    break;
                }
            }
        }
        else if(Target >= 0)
        {
            obj_material *Material = OutMaterials + Target;

            if(ObjLineStartsWith(At, "Kd"))
            {
                char *C = At + 2;
                ObjSkipWhitespace(&C);
                real32 R = ObjParseReal32(&C);
                ObjSkipWhitespace(&C);
                real32 G = ObjParseReal32(&C);
                ObjSkipWhitespace(&C);
                real32 B = ObjParseReal32(&C);
                Material->Diffuse = Vec3(R, G, B);
            }
            else if(ObjLineStartsWith(At, "Ks"))
            {
                char *C = At + 2;
                ObjSkipWhitespace(&C);
                real32 R = ObjParseReal32(&C);
                ObjSkipWhitespace(&C);
                real32 G = ObjParseReal32(&C);
                ObjSkipWhitespace(&C);
                real32 B = ObjParseReal32(&C);
                Material->Specular = Vec3(R, G, B);
            }
            else if(ObjLineStartsWith(At, "Ns"))
            {
                char *C = At + 2;
                ObjSkipWhitespace(&C);
                Material->Shininess = ObjParseReal32(&C);

                // A zero exponent makes pow() return 1 everywhere, which lights
                // the whole surface as if it were a mirror facing you.
                if(Material->Shininess < 1.0f)
                {
                    Material->Shininess = 1.0f;
                }
            }
            else if(ObjLineStartsWith(At, "map_d"))
            {
                char *C = At + 5;
                ObjSkipWhitespace(&C);

                char *Name = C;
                while(*C && !ObjIsEndOfLine(*C)) { ++C; }
                while((C > Name) && ObjIsWhitespace(C[-1])) { --C; }

                ObjCopyToken(Material->AlphaMapName, OBJ_MAX_MAP_NAME,
                             Name, (uint32)(C - Name));
                Material->HasAlphaMap = true;
            }
            else if(ObjLineStartsWith(At, "map_Kd"))
            {
                char *C = At + 6;
                ObjSkipWhitespace(&C);

                // Everything to the end of the line - a texture path may
                // contain spaces, and trailing whitespace is trimmed after.
                char *Name = C;
                while(*C && !ObjIsEndOfLine(*C))
                {
                    ++C;
                }
                while((C > Name) && ObjIsWhitespace(C[-1]))
                {
                    --C;
                }

                ObjCopyToken(Material->DiffuseMapName, OBJ_MAX_MAP_NAME,
                             Name, (uint32)(C - Name));
                Material->HasDiffuseMap = true;
            }
        }

        ObjSkipLine(&At);
    }
}

// A contiguous run of the index buffer that shares one material, so it can be
// drawn with one glDrawElements call after its material's uniforms are set.
//
// NOTE(yigit): This is why the indices are REORDERED rather than left in file
// order.  A file interleaves materials freely - body, glass, body again - and
// each switch would otherwise cost its own draw call.  Grouping first means
// one call per material, however scattered the faces were.
struct obj_submesh
{
    uint32 FirstIndex;
    uint32 IndexCount;
    uint32 MaterialIndex;
};

struct loaded_model
{
    obj_vertex *Vertices;
    uint32 VertexCount;

    uint32 *Indices;
    uint32 IndexCount;

    obj_submesh *Submeshes;
    uint32 SubmeshCount;

    // Names only, still pointing into the file buffer.  Step 3 resolves these
    // against a .mtl; until then they are for logging.
    obj_material_ref *Materials;
    uint32 MaterialCount;
};

internal loaded_model
ObjLoadModel(memory_arena *Arena, char *Contents, uint32 ContentsSize)
{
    loaded_model Result = {};

    obj_source_data Obj = ObjParseSourceData(Arena, Contents, ContentsSize);
    if(!Obj.FaceVertexCount)
    {
        return(Result);
    }

    // Every face corner is a vertex until proven a duplicate, so the file's
    // own corner count is the upper bound.  Allocating for the worst case and
    // using less is the arena's whole trick - no growing, no reallocation.
    uint32 MaxVertexCount = Obj.Counts.FaceVertexCount;

    Result.Vertices = PushArray(Arena, MaxVertexCount, obj_vertex);
    Result.Indices = PushArray(Arena, Obj.FaceVertexCount, uint32);

    // NOTE(yigit): A linear search over every emitted vertex would be O(n^2) -
    // fine for a cube, minutes for a real model.  Instead the search is bucketed
    // by POSITION index, which the data hands us for free: two vertices can only
    // be duplicates if they share a position, and a given position is usually
    // reused only two or three times with different normals.  So each bucket
    // holds a handful of entries and the whole pass is effectively linear.
    //
    // FirstVertexForPosition is the head of each bucket; NextVertexInBucket
    // chains the rest.  -1 terminates.
    int32 *FirstVertexForPosition = PushArray(Arena, Obj.Counts.PositionCount, int32);
    int32 *NextVertexInBucket = PushArray(Arena, MaxVertexCount, int32);
    obj_face_vertex *VertexSource = PushArray(Arena, MaxVertexCount, obj_face_vertex);

    // NOTE(yigit): Those three are scratch.  The arena has no free, so they stay
    // allocated for the life of the program - about 12 bytes per face corner,
    // wasted.  The proper fix is a temporary arena carved out of
    // TransientStorage, which is a full gigabyte and currently unused.  Left
    // for when a model is big enough to care.
    for(uint32 I = 0; I < Obj.Counts.PositionCount; ++I)
    {
        FirstVertexForPosition[I] = -1;
    }

    // ---- Where each material's indices will start ------------------------
    //
    // Count the triangles per material, then prefix-sum those counts into a
    // starting triangle for each.  With the starts known, the loop below can
    // drop every triangle straight into its material's run - no sorting pass,
    // no second copy of the index buffer.
    uint32 *TrianglesInMaterial = PushArray(Arena, Obj.MaterialCount, uint32);
    uint32 *MaterialFirstTriangle = PushArray(Arena, Obj.MaterialCount, uint32);
    uint32 *MaterialCursor = PushArray(Arena, Obj.MaterialCount, uint32);

    for(uint32 I = 0; I < Obj.MaterialCount; ++I)
    {
        TrianglesInMaterial[I] = 0;
        MaterialCursor[I] = 0;
    }

    uint32 TriangleCount = Obj.FaceVertexCount / 3;
    for(uint32 I = 0; I < TriangleCount; ++I)
    {
        ++TrianglesInMaterial[Obj.TriangleMaterials[I]];
    }

    uint32 RunningTriangle = 0;
    for(uint32 I = 0; I < Obj.MaterialCount; ++I)
    {
        MaterialFirstTriangle[I] = RunningTriangle;
        RunningTriangle += TrianglesInMaterial[I];
    }
    Assert(RunningTriangle == TriangleCount);

    // Only materials that actually own triangles become submeshes.  A file can
    // name a material and then never use it.
    Result.Submeshes = PushArray(Arena, Obj.MaterialCount, obj_submesh);
    for(uint32 I = 0; I < Obj.MaterialCount; ++I)
    {
        if(TrianglesInMaterial[I])
        {
            obj_submesh *Submesh = Result.Submeshes + Result.SubmeshCount++;
            Submesh->FirstIndex = 3*MaterialFirstTriangle[I];
            Submesh->IndexCount = 3*TrianglesInMaterial[I];
            Submesh->MaterialIndex = I;
        }
    }

    Result.Materials = Obj.Materials;
    Result.MaterialCount = Obj.MaterialCount;

    for(uint32 CornerIndex = 0;
        CornerIndex < Obj.FaceVertexCount;
        ++CornerIndex)
    {
        obj_face_vertex Corner = Obj.FaceVertices[CornerIndex];

        // Where this corner lands in the REORDERED buffer.  The cursor only
        // advances on the first corner of each triangle, so all three stay
        // together and keep their winding.
        uint32 Material = Obj.TriangleMaterials[CornerIndex / 3];
        if((CornerIndex % 3) == 0)
        {
            ++MaterialCursor[Material];
        }
        uint32 DestTriangle = MaterialFirstTriangle[Material] + MaterialCursor[Material] - 1;
        uint32 DestIndex = 3*DestTriangle + (CornerIndex % 3);

        // A corner with no position is a malformed face; skip it rather than
        // indexing out of the positions array.
        if(Corner.PositionIndex < 0)
        {
            Result.Indices[DestIndex] = 0;
            continue;
        }

        int32 Found = -1;
        for(int32 Candidate = FirstVertexForPosition[Corner.PositionIndex];
            Candidate >= 0;
            Candidate = NextVertexInBucket[Candidate])
        {
            if((VertexSource[Candidate].TexCoordIndex == Corner.TexCoordIndex) &&
               (VertexSource[Candidate].NormalIndex == Corner.NormalIndex))
            {
                Found = Candidate;
                break;
            }
        }

        if(Found < 0)
        {
            Assert(Result.VertexCount < MaxVertexCount);

            Found = (int32)Result.VertexCount++;

            obj_vertex *Vertex = Result.Vertices + Found;
            Vertex->Position = Obj.Positions[Corner.PositionIndex];

            // A face may name no normal or no texcoord.  Zero is a placeholder,
            // not a correct value - a model with no normals will shade flat
            // black until something generates them.
            Vertex->Normal = (Corner.NormalIndex >= 0)
                ? Obj.Normals[Corner.NormalIndex]
                : Vec3(0.0f, 0.0f, 0.0f);
            Vertex->TexCoord = (Corner.TexCoordIndex >= 0)
                ? Obj.TexCoords[Corner.TexCoordIndex]
                : Vec2(0.0f, 0.0f);

            VertexSource[Found] = Corner;

            // Push onto the front of this position's bucket.
            NextVertexInBucket[Found] = FirstVertexForPosition[Corner.PositionIndex];
            FirstVertexForPosition[Corner.PositionIndex] = Found;
        }

        Result.Indices[DestIndex] = (uint32)Found;
    }

    Result.IndexCount = Obj.FaceVertexCount;

    return(Result);
}

#define HANDMADE_OBJ_H
#endif
