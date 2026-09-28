#version 330 core

in vec3 Direction;
out vec4 FragColor;

// samplerCube, not sampler2D: it is looked up with a DIRECTION, not a UV.
// The GPU picks the face the direction points at and the spot on that face.
// Length does not matter, so Direction is not normalized.
uniform samplerCube sky;

void main()
{
    FragColor = vec4(texture(sky, Direction).rgb, 1.0);
}
