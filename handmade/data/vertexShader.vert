#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

uniform vec3 lightPos;
out vec3 LightPosView;

void main()
{
    FragPos = vec3(view * model * vec4(aPos, 1.0));

    Normal = mat3(view * model) * aNormal;
    LightPosView = vec3(view * vec4(lightPos, 1.0));

    TexCoords = aTexCoord;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
