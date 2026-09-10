#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

#define NR_POINT_LIGHTS 4

struct Material {
    sampler2D diffuse;
    sampler2D specular;

    // Book ch. 18 - the .mtl Kd and Ks.  These MULTIPLY the texture sample
    // rather than replacing it, so one code path covers both cases: a material
    // with no texture binds a 1x1 white texture, and white times Kd is Kd.
    // No branch, no second shader.
    vec3 diffuseColor;
    vec3 specularColor;

    // Book ch. 22 - map_d, an opacity mask.  Sampled as .r, and multiplied by
    // the diffuse texture's own alpha, because this scene supplies the cutout
    // both ways: merged into the diffuse alpha for the foliage, as a separate
    // greyscale file for the chains.  A material with neither binds white and
    // opaque 1x1 textures, so both terms come out 1.
    sampler2D alphaMask;

    float shininess;
};

// Book ch. 17.1 - no position, so no attenuation and no cone.
struct DirLight {
    vec3 direction;     // VIEW space, pointing from the light into the scene

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

// Book ch. 17.2 - a position instead of a direction, and distance falls off as
// 1 / (constant + linear*d + quadratic*d*d).
struct PointLight {
    vec3 position;      // VIEW space

    float constant;
    float linear;
    float quadratic;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

// Book ch. 17.3 - a point light with a cone bolted on.
//
// NOTE(yigit): No position or direction member.  This flashlight is held at the
// camera, and in VIEW SPACE the camera is the origin looking down -Z by
// definition - so both are constants inside CalcSpotLight.  The book has to
// pass camera.Position and camera.Front only because it lights in world space.
struct SpotLight {
    float constant;
    float linear;
    float quadratic;

    // Cosines of the half-angles, not the angles: dot() hands back a cosine for
    // free, so the cone test never needs an acos().  outerCutOff is the WIDER
    // cone, so its cosine is the SMALLER number.
    float cutOff;
    float outerCutOff;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

uniform Material material;
uniform DirLight dirLight;
uniform PointLight pointLights[NR_POINT_LIGHTS];
uniform SpotLight spotLight;

vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir)
{
    // Negated because light.direction points INTO the scene, and the dot
    // product below wants a vector pointing from the surface back to the light.
    vec3 lightDir = normalize(-light.direction);

    float diff = max(dot(normal, lightDir), 0.0);

    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);

    vec3 DiffuseSample = material.diffuseColor * texture(material.diffuse, TexCoords).rgb;

    vec3 ambient  = light.ambient  * DiffuseSample;
    vec3 diffuse  = light.diffuse  * diff * DiffuseSample;
    vec3 specular = light.specular * spec * material.specularColor * texture(material.specular, TexCoords).rgb;

    return ambient + diffuse + specular;
}

vec3 CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir)
{
    // Unlike the directional light, this one has a place, so every fragment
    // gets its own direction to it - and its own distance.
    vec3 lightDir = normalize(light.position - fragPos);

    float diff = max(dot(normal, lightDir), 0.0);

    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);

    float Distance = length(light.position - fragPos);
    float attenuation = 1.0 / (light.constant +
                               light.linear * Distance +
                               light.quadratic * (Distance * Distance));

    vec3 DiffuseSample = material.diffuseColor * texture(material.diffuse, TexCoords).rgb;

    vec3 ambient  = light.ambient  * DiffuseSample;
    vec3 diffuse  = light.diffuse  * diff * DiffuseSample;
    vec3 specular = light.specular * spec * material.specularColor * texture(material.specular, TexCoords).rgb;

    return (ambient + diffuse + specular) * attenuation;
}

vec3 CalcSpotLight(SpotLight light, vec3 normal, vec3 fragPos, vec3 viewDir)
{
    // Both constants - see the note on the struct.
    const vec3 SpotDirection = vec3(0.0, 0.0, -1.0);
    const vec3 SpotPosition  = vec3(0.0, 0.0,  0.0);

    vec3 lightDir = normalize(SpotPosition - fragPos);

    // NOTE(yigit): Cosine runs BACKWARDS.  A smaller angle gives a BIGGER
    // cosine, so nearer the middle of the cone means a LARGER theta.
    float theta = dot(lightDir, -SpotDirection);

    // The clamp is the whole cone test - there is deliberately no if here:
    //   inside the inner cone  -> theta large  -> clamps to 1, full light
    //   in the band between    -> 0 .. 1       -> the soft edge
    //   outside the outer cone -> theta small  -> clamps to 0, nothing
    float epsilon   = light.cutOff - light.outerCutOff;
    float intensity = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);

    float diff = max(dot(normal, lightDir), 0.0);

    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);

    float Distance = length(SpotPosition - fragPos);
    float attenuation = 1.0 / (light.constant +
                               light.linear * Distance +
                               light.quadratic * (Distance * Distance));

    vec3 DiffuseSample = material.diffuseColor * texture(material.diffuse, TexCoords).rgb;

    vec3 ambient  = light.ambient  * DiffuseSample;
    vec3 diffuse  = light.diffuse  * diff * DiffuseSample;
    vec3 specular = light.specular * spec * material.specularColor * texture(material.specular, TexCoords).rgb;

    // Ambient is left out of the cone gate so the scene is never pitch black.
    return ambient + (diffuse + specular) * attenuation * intensity;
}

void main()
{
    // Book ch. 22 - alpha testing.  Foliage and chains are flat cards with the
    // leaf or link shape punched out of them; without this they render as solid
    // rectangles.
    //
    // NOTE(yigit): discard, not blending.  The cutout is binary - a fragment is
    // either leaf or gap - so there is nothing to blend, and blending would
    // demand the geometry be sorted back-to-front every frame.  The cost is
    // that discard defeats early-Z on most hardware, so this shader gets
    // slower for every fragment, not just the cut ones.
    float Opacity = texture(material.diffuse, TexCoords).a *
                    texture(material.alphaMask, TexCoords).r;
    if(Opacity < 0.5)
    {
        discard;
    }

    vec3 norm = normalize(Normal);

    // The eye is the origin in view space, so "towards the viewer" is -FragPos.
    vec3 viewDir = normalize(-FragPos);

    // Book ch. 17.3 - contributions ADD, because light adds in the real world.
    vec3 result = CalcDirLight(dirLight, norm, viewDir);

    for(int i = 0; i < NR_POINT_LIGHTS; ++i)
    {
        result += CalcPointLight(pointLights[i], norm, FragPos, viewDir);
    }

    result += CalcSpotLight(spotLight, norm, FragPos, viewDir);

    FragColor = vec4(result, 1.0);
}
