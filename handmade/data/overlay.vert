#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoords;

// NOTE(yigit): Orthographic, not perspective.  aPos arrives in PIXELS and this
// is the only thing that turns it into clip space, so there is no view matrix
// and no model matrix - an overlay is already where it wants to be.
uniform mat4 projection;

void main()
{
    TexCoords = aTexCoord;
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
}
