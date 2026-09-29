#version 330 core

// A fullscreen triangle drawn into ONE face of a cube map, handing each pixel
// the direction that pixel of that face looks in.  The baking shaders then
// only have to answer "what light arrives from around this direction?".

out vec3 Direction;

uniform int face;       // 0..5, OpenGL's order: +X -X +Y -Y +Z -Z

void main()
{
    vec2 P = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vec2 Clip = P*2.0 - 1.0;
    gl_Position = vec4(Clip, 0.0, 1.0);

    // Where on the face this is, -1..+1 across (S) and up (T).
    float S = Clip.x;
    float T = Clip.y;

    // NOTE(yigit): OpenGL's cube face layout, run backwards.  Its spec says
    // which face a direction lands on and where; these are the inverse of
    // that table.  The minus signs are not a mistake - cube faces were laid
    // out for a left-handed world long before OpenGL, and each face's
    // "across" and "up" point wherever that convention put them.
    if     (face == 0) { Direction = vec3( 1.0,   -T,   -S); }   // +X
    else if(face == 1) { Direction = vec3(-1.0,   -T,    S); }   // -X
    else if(face == 2) { Direction = vec3(   S,  1.0,    T); }   // +Y
    else if(face == 3) { Direction = vec3(   S, -1.0,   -T); }   // -Y
    else if(face == 4) { Direction = vec3(   S,   -T,  1.0); }   // +Z
    else               { Direction = vec3(  -S,   -T, -1.0); }   // -Z
}
