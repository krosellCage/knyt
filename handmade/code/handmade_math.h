#ifndef HANDMADE_MATH_H
#define HANDMADE_MATH_H
#include <math.h>

// NOTE(yigit): Thin wrappers so the rest of the code never touches the CRT
// names directly.  These were the only two ever used from the old intrinsics
// header, so they live here now.
// TODO(yigit): Implement these ourselves and drop math.h entirely.
inline real32 Sin(real32 Angle) { return(sinf(Angle)); }
inline real32 Cos(real32 Angle) { return(cosf(Angle)); }

struct mat4
{
    real32 E[16]; // column-major
};

inline mat4
Mat4Identity()
{
    mat4 M = {};
    M.E[0]  = 1; M.E[5]  = 1;
    M.E[10] = 1; M.E[15] = 1;
    return(M);
}

inline mat4
Mat4Mul(mat4 A, mat4 B)
{
    mat4 M = {};
    for(int Col = 0; Col < 4; ++Col)
    {
        for(int Row = 0; Row < 4; ++Row)
        {
            real32 Sum = 0;
            for(int K = 0; K < 4; ++K)
            {
                Sum += A.E[K*4 + Row] * B.E[Col*4 + K];
            }
            M.E[Col*4 + Row] = Sum;
        }
    }
    return(M);
}

// Simple perspective matrix (standard OpenGL convention)
inline mat4
Mat4Perspective(real32 FovYRadians, real32 Aspect, real32 Near, real32 Far)
{
    mat4 M = {};
    real32 F = 1.0f / tanf(FovYRadians * 0.5f);
    M.E[0]  =  F / Aspect;
    M.E[5]  =  F;
    M.E[10] = (Far + Near) / (Near - Far);
    M.E[11] = -1.0f;
    M.E[14] = (2.0f * Far * Near) / (Near - Far);
    return(M);
}

// Translation matrix
inline mat4
Mat4Translation(real32 X, real32 Y, real32 Z)
{
    mat4 M = Mat4Identity();
    M.E[12] = X;
    M.E[13] = Y;
    M.E[14] = Z;
    return(M);
}

// Scale matrix - the diagonal, so each axis is multiplied independently
inline mat4
Mat4Scale(real32 X, real32 Y, real32 Z)
{
    mat4 M = Mat4Identity();
    M.E[0]  = X;
    M.E[5]  = Y;
    M.E[10] = Z;
    return(M);
}

inline struct mat4 Mat4RotationX(real32 angle) {
    struct mat4 M = Mat4Identity();
    real32 c = cosf(angle);
    real32 s = sinf(angle);

    // Column-major layout for X rotation:
    // [ 1  0    0   0 ]
    // [ 0 cos -sin  0 ]
    // [ 0 sin  cos  0 ]
    // [ 0  0    0   1 ]
    M.E[5]  = c;  M.E[9]  = -s;
    M.E[6]  = s;  M.E[10] = c;
    return M;
}

// Rotation around Y-axis
inline struct mat4 Mat4RotationY(real32 angle) {
    struct mat4 M = Mat4Identity();
    real32 c = cosf(angle);
    real32 s = sinf(angle);

    // Column-major layout for Y rotation:
    // [ cos 0  sin  0 ]
    // [  0  1   0   0 ]
    // [-sin 0  cos  0 ]
    // [  0  0   0   1 ]
    M.E[0]  = c;  M.E[8]  = s;
    M.E[2]  = -s; M.E[10] = c;
    return M;
}

inline struct mat4 Mat4RotationZ(real32 angle) {
    struct mat4 M = Mat4Identity();
    real32 c = cosf(angle);
    real32 s = sinf(angle);

    // Column-major layout for Z rotation:
    // [ cos -sin  0   0 ]
    // [ sin  cos  0   0 ]
    // [  0    0   1   0 ]
    // [  0    0   0   1 ]
    M.E[0]  = c;  M.E[4]  = -s;
    M.E[1]  = s;  M.E[5] = c;
    return M;
}

// NOTE(yigit): Added for OBJ texture coordinates.  Deliberately bare - no
// operators until something actually needs one.
struct vec2
{
    real32 X, Y;
};

inline vec2
Vec2(real32 X, real32 Y)
{
    vec2 Result = {X, Y};
    return(Result);
}

struct vec3
{
    real32 X, Y, Z;
};

// Forward declarations - Mat4RotationAxis below needs these, and they are
// defined further down with the rest of the vector maths.
inline vec3 Vec3(real32 X, real32 Y, real32 Z);
inline vec3 Normalize(vec3 A);

// Rotation about any axis (Rodrigues' formula).  This is the general case -
// Mat4RotationX/Y/Z are just this with the axis on a basis vector, and they
// stay because they are cheaper and clearer when that is all you need.
//
// NOTE(yigit): The axis is normalized here, so callers can pass something
// like (0.5, 1, 0) without worrying about its length.  GLM's rotate does the
// same.  A zero-length axis yields the identity rather than NaNs.
inline mat4
Mat4RotationAxis(vec3 Axis, real32 Angle)
{
    mat4 M = Mat4Identity();

    vec3 A = Normalize(Axis);
    real32 c = cosf(Angle);
    real32 s = sinf(Angle);
    real32 t = 1.0f - c;

    M.E[0] = t*A.X*A.X + c;      M.E[4] = t*A.X*A.Y - s*A.Z;  M.E[8]  = t*A.X*A.Z + s*A.Y;
    M.E[1] = t*A.X*A.Y + s*A.Z;  M.E[5] = t*A.Y*A.Y + c;      M.E[9]  = t*A.Y*A.Z - s*A.X;
    M.E[2] = t*A.X*A.Z - s*A.Y;  M.E[6] = t*A.Y*A.Z + s*A.X;  M.E[10] = t*A.Z*A.Z + c;

    return(M);
}

inline vec3
Vec3(real32 X, real32 Y, real32 Z)
{
    vec3 Result = {X, Y, Z};
    return(Result);
}

struct vec4
{
    real32 X, Y, Z, W;
};

inline vec4
Vec4(real32 X, real32 Y, real32 Z, real32 W)
{
    vec4 Result = {X, Y, Z, W};
    return(Result);
}

// NOTE(yigit): Mirrors GLSL's vec4(someVec3, w).
inline vec4
Vec4(vec3 XYZ, real32 W)
{
    vec4 Result = {XYZ.X, XYZ.Y, XYZ.Z, W};
    return(Result);
}

// -------------------------------------------------------------------------
// vec3 operators
// -------------------------------------------------------------------------
inline vec3 operator-(vec3 A)              { return(Vec3(-A.X, -A.Y, -A.Z)); }
inline vec3 operator+(vec3 A, vec3 B)      { return(Vec3(A.X + B.X, A.Y + B.Y, A.Z + B.Z)); }
inline vec3 operator-(vec3 A, vec3 B)      { return(Vec3(A.X - B.X, A.Y - B.Y, A.Z - B.Z)); }
inline vec3 operator*(vec3 A, real32 S)    { return(Vec3(A.X * S, A.Y * S, A.Z * S)); }
inline vec3 operator*(real32 S, vec3 A)    { return(Vec3(A.X * S, A.Y * S, A.Z * S)); }
inline vec3 operator/(vec3 A, real32 S)    { return(Vec3(A.X / S, A.Y / S, A.Z / S)); }

inline vec3 &operator+=(vec3 &A, vec3 B)   { A = A + B; return(A); }
inline vec3 &operator-=(vec3 &A, vec3 B)   { A = A - B; return(A); }
inline vec3 &operator*=(vec3 &A, real32 S) { A = A * S; return(A); }

// -------------------------------------------------------------------------
// vec4 operators
// -------------------------------------------------------------------------
inline vec4 operator-(vec4 A)              { return(Vec4(-A.X, -A.Y, -A.Z, -A.W)); }
inline vec4 operator+(vec4 A, vec4 B)      { return(Vec4(A.X + B.X, A.Y + B.Y, A.Z + B.Z, A.W + B.W)); }
inline vec4 operator-(vec4 A, vec4 B)      { return(Vec4(A.X - B.X, A.Y - B.Y, A.Z - B.Z, A.W - B.W)); }
inline vec4 operator*(vec4 A, real32 S)    { return(Vec4(A.X * S, A.Y * S, A.Z * S, A.W * S)); }
inline vec4 operator*(real32 S, vec4 A)    { return(Vec4(A.X * S, A.Y * S, A.Z * S, A.W * S)); }
inline vec4 operator/(vec4 A, real32 S)    { return(Vec4(A.X / S, A.Y / S, A.Z / S, A.W / S)); }

inline vec4 &operator+=(vec4 &A, vec4 B)   { A = A + B; return(A); }
inline vec4 &operator-=(vec4 &A, vec4 B)   { A = A - B; return(A); }
inline vec4 &operator*=(vec4 &A, real32 S) { A = A * S; return(A); }

// -------------------------------------------------------------------------
// Transforming a point by a matrix.  Each output component is one ROW of the
// matrix dotted with the vector - and since storage is column-major, a row is
// the four entries spaced four apart.
//
// NOTE(yigit): W decides whether translation applies.  W = 1 means "this is a
// position", so the fourth column (E[12..14]) is added in.  W = 0 means "this
// is a direction", and translation drops out - which is what you want for
// normals and velocities, since moving something shouldn't rotate which way
// it faces.
// -------------------------------------------------------------------------
inline vec4
operator*(mat4 M, vec4 V)
{
    vec4 Result;
    Result.X = M.E[0]*V.X + M.E[4]*V.Y + M.E[8] *V.Z + M.E[12]*V.W;
    Result.Y = M.E[1]*V.X + M.E[5]*V.Y + M.E[9] *V.Z + M.E[13]*V.W;
    Result.Z = M.E[2]*V.X + M.E[6]*V.Y + M.E[10]*V.Z + M.E[14]*V.W;
    Result.W = M.E[3]*V.X + M.E[7]*V.Y + M.E[11]*V.Z + M.E[15]*V.W;
    return(Result);
}

// -------------------------------------------------------------------------
// Vector products and lengths
// -------------------------------------------------------------------------

// Sum of componentwise products.  For unit vectors this is the cosine of the
// angle between them: 1 means same direction, 0 perpendicular, -1 opposite.
inline real32 Dot(vec3 A, vec3 B) { return(A.X*B.X + A.Y*B.Y + A.Z*B.Z); }
inline real32 Dot(vec4 A, vec4 B) { return(A.X*B.X + A.Y*B.Y + A.Z*B.Z + A.W*B.W); }

// A vector perpendicular to both inputs.  Order matters - Cross(A, B) points
// the opposite way from Cross(B, A).  Only meaningful in 3D.
inline vec3
Cross(vec3 A, vec3 B)
{
    vec3 Result;
    Result.X = A.Y*B.Z - A.Z*B.Y;
    Result.Y = A.Z*B.X - A.X*B.Z;
    Result.Z = A.X*B.Y - A.Y*B.X;
    return(Result);
}

// -------------------------------------------------------------------------
// View matrix from a camera position, a point to look at, and which way is up.
// The equivalent of glm::lookAt (book ch. 10.2).
//
// It builds two things and multiplies them: a rotation whose ROWS are the
// camera's own axes, and a translation by -Eye.  Rows rather than columns
// because the view matrix is the INVERSE of the camera's transform - it moves
// the world in front of a fixed eye at the origin, rather than moving a
// camera.  For an inverse rotation, transposing is all it takes.
//
// NOTE(yigit): F points BACKWARD, from the target toward the eye, because
// OpenGL's camera looks down -Z.  This is why Eye - Target rather than the
// other way round, and it is the sign that catches everybody.
// -------------------------------------------------------------------------
inline mat4
Mat4LookAt(vec3 Eye, vec3 Target, vec3 Up)
{
    vec3 F = Normalize(Eye - Target);       // backward
    vec3 R = Normalize(Cross(Up, F));       // right
    vec3 U = Cross(F, R);                   // true up, already unit length

    mat4 M = Mat4Identity();

    // Column-major: E[Col*4 + Row], so a ROW is four entries spaced 4 apart.
    M.E[0] = R.X;  M.E[4] = R.Y;  M.E[8]  = R.Z;  M.E[12] = -Dot(R, Eye);
    M.E[1] = U.X;  M.E[5] = U.Y;  M.E[9]  = U.Z;  M.E[13] = -Dot(U, Eye);
    M.E[2] = F.X;  M.E[6] = F.Y;  M.E[10] = F.Z;  M.E[14] = -Dot(F, Eye);

    return(M);
}

// NOTE(yigit): Squared length skips the square root.  Use it whenever you are
// only comparing distances - "is A nearer than B" needs no sqrt.
inline real32 LengthSq(vec3 A) { return(Dot(A, A)); }
inline real32 LengthSq(vec4 A) { return(Dot(A, A)); }

inline real32 Length(vec3 A) { return(sqrtf(LengthSq(A))); }
inline real32 Length(vec4 A) { return(sqrtf(LengthSq(A))); }

// NOTE(yigit): Returns a zero vector rather than NaNs if the input has no
// length.  A NaN silently poisons every later calculation and is miserable to
// track down, so the branch is worth it.
inline vec3
Normalize(vec3 A)
{
    vec3 Result = {};
    real32 Len = Length(A);
    if(Len > 0.0f)
    {
        Result = A / Len;
    }
    return(Result);
}

inline vec4
Normalize(vec4 A)
{
    vec4 Result = {};
    real32 Len = Length(A);
    if(Len > 0.0f)
    {
        Result = A / Len;
    }
    return(Result);
}

// NOTE(yigit): Standard FPS camera basis vectors, derived from Yaw only
// (so movement stays on the ground plane even while looking up/down).
// Yaw = 0 looks down -Z, consistent with Mat4RotationY below.
inline vec3
CameraForward(real32 Yaw)
{
    return Vec3(-sinf(Yaw), 0.0f, -cosf(Yaw));
}

inline vec3
CameraRight(real32 Yaw)
{
    return Vec3(cosf(Yaw), 0.0f, -sinf(Yaw));
}


#endif
