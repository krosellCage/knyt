#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoord;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;

// Where this point sits in the SUN'S view - the same maths shadow.vert does,
// so it lands on exactly the shadow-map pixel this point was drawn into.
//
// The lamp and outline programs share this shader but never set lightSpace
// or read this output; the compiler strips it from them.
out vec4 FragPosLightSpace;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpace;

void main()
{
    FragPos = vec3(view * model * vec4(aPos, 1.0));

    Normal = mat3(view * model) * aNormal;

    TexCoords = aTexCoord;

    FragPosLightSpace = lightSpace * model * vec4(aPos, 1.0);

    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
