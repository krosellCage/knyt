#version 330 core
layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in vec4 aColor;

out vec2 TexCoords;
out vec4 Color;

// NOTE(yigit): Orthographic, not perspective.  aPos arrives in PIXELS and this
// is the only thing that turns it into clip space, so there is no view matrix
// and no model matrix - an overlay is already where it wants to be.
uniform mat4 projection;

void main()
{
    TexCoords = aTexCoord;
    Color = aColor;
    gl_Position = projection * vec4(aPos, 0.0, 1.0);
}
