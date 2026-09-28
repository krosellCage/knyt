#version 330 core

// No vertex attributes at all.  One triangle big enough to cover the screen,
// with its corners made up from gl_VertexID:
//
//   ID 0 -> (-1,-1)   bottom-left of the screen
//   ID 1 -> ( 3,-1)   far off to the right
//   ID 2 -> (-1, 3)   far off the top
//
// The parts hanging off the screen are clipped away for free.

out vec3 Direction;     // which way this corner of the screen looks, WORLD space

uniform mat4 view;
uniform mat4 projection;

void main()
{
    vec2 P = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vec2 Clip = P * 2.0 - 1.0;

    // z = w = 1 puts it at depth 1.0, the far plane - behind everything.
    gl_Position = vec4(Clip, 1.0, 1.0);

    // Run the camera BACKWARDS to find what this screen corner is looking at.
    //
    //   inverse(projection): screen position -> a point in view space.
    //   transpose(mat3(view)): view space -> world space, rotation only.
    //
    // mat3() drops the translation, which is what makes the sky infinitely far
    // away: however far the camera walks, the directions do not change.  And
    // for a pure rotation the transpose IS the inverse, and far cheaper.
    vec4 ViewPos = inverse(projection) * vec4(Clip, 1.0, 1.0);
    Direction = transpose(mat3(view)) * (ViewPos.xyz / ViewPos.w);
}
