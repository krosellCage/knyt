#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoord;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    // The lighting maths all happens in WORLD space, so the fragment shader
    // needs this vertex's world position - model only, no view or projection.
    // Those two would move it into camera space, where the light position
    // uniform no longer means the same thing.
    FragPos = vec3(model * vec4(aPos, 1.0));

    Normal = aNormal;

    TexCoord = aTexCoord;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
