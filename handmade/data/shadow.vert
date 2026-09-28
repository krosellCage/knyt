#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 2) in vec2 aTexCoord;

// Only for the alpha test in shadow.frag - leaves need their holes.
out vec2 TexCoords;

// Same model matrix as the lit pass, but seen through the SUN'S camera.
uniform mat4 lightSpace;
uniform mat4 model;

void main()
{
    TexCoords = aTexCoord;
    gl_Position = lightSpace * model * vec4(aPos, 1.0);
}
