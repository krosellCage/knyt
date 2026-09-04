#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in vec3 LightPosView;

struct Material {
    sampler2D diffuse;
    sampler2D specular;
    sampler2D emission;
    float shininess;
};

struct Light {
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

uniform Light light;
uniform Material material;
uniform float time;

void main()
{
    // Sampled once and reused by ambient and diffuse below.  The same texture
    // at the same coordinate cannot give two different answers, so asking
    // twice was only ever a second chance to mistype one of them.
    vec3 DiffuseSample = texture(material.diffuse, TexCoords).rgb;

    vec3 ambient = light.ambient * DiffuseSample;

    // Scrolls the COORDINATE, not the colour.  Only works because the texture
    // was loaded with GL_REPEAT - past 1.0 the coordinate wraps back around
    // instead of smearing the edge pixel.
    vec3 emission = texture(material.emission, TexCoords + vec2(0.0, time / 9.0)).rgb;

    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(LightPosView - FragPos);

    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = light.diffuse * diff * DiffuseSample;

    vec3 viewDir = normalize(-FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);

    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    vec3 specular = light.specular * spec * texture(material.specular, TexCoords).rgb;

    // Emission is added, never multiplied - it does not reflect the light, so
    // no lighting term is allowed to touch it.
    FragColor = vec4(ambient + diffuse + specular + emission, 1.0);
}
