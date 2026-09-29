#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in vec4 FragPosLightSpace;      // this point as the SUN sees it
in vec3 WorldNormal;
// The sun's view of the scene, depth only: for every direction the sun
// looks in, how far away the first thing it hits is.
uniform sampler2D shadowMap;

#define NR_POINT_LIGHTS 4

struct Material {
    sampler2D diffuse;
    sampler2D specular;

    // The .mtl Kd and Ks.  These MULTIPLY the texture sample
    // rather than replacing it, so one code path covers both cases: a material
    // with no texture binds a 1x1 white texture, and white times Kd is Kd.
    // No branch, no second shader.
    vec3 diffuseColor;
    vec3 specularColor;

    // map_d, an opacity mask.  Sampled as .r, and multiplied by
    // the diffuse texture's own alpha, because this scene supplies the cutout
    // both ways: merged into the diffuse alpha for the foliage, as a separate
    // greyscale file for the chains.  A material with neither binds white and
    // opaque 1x1 textures, so both terms come out 1.
    sampler2D alphaMask;

    float shininess;
};

// A directional light: no position, so no attenuation and no cone.
struct DirLight {
    vec3 direction;     // VIEW space, pointing from the light into the scene

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

// A position instead of a direction, and distance falls off as
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

uniform Material material;
uniform DirLight dirLight;
uniform PointLight pointLights[NR_POINT_LIGHTS];

// How many of pointLights[] are real this frame, 0..NR_POINT_LIGHTS.
//
// NOTE(yigit): The loop MUST stop here rather than at NR_POINT_LIGHTS.  An
// unused slot is not "no light" - uniforms keep whatever was last written,
// so a light removed from the scene would keep shining, and a slot never
// written at all holds zeros, whose attenuation is 1/(0+0+0): infinity,
// times a zero colour, is NaN, and NaN renders black.
uniform int pointLightCount;

/*
  How shiny this spot is, from the specular map - the same amount for red,
  green and blue.

  NOTE(yigit): .r, spread to all three, not .rgb.  Most of Sponza's specular
  maps are greyscale, which load as one-channel GL_RED textures, and those
  read back as (value, 0, 0): with .rgb, every highlight would come out red.
  The colour maps (floor_gloss.png is RGBA) are grey anyway, so their red
  channel is as good a measure as any.
*/
vec3 SpecularMap(vec2 UV)
{
    return vec3(texture(material.specular, UV).r);
}

/*
  How much of the sun this point does NOT get: 0 = fully lit, 1 = fully in
  shadow, in between = the soft edge of a shadow.
*/
float CalcShadow(vec3 normal, vec3 lightDir)
{
    // 1. Where is this point in the shadow map?  lightSpace produced clip
    //    coordinates, -1..+1 on every axis.  The map is read with 0..1 UVs
    //    and its stored depths are 0..1 as well, so remap all three.
    //
    //    The divide by w does nothing for the sun's orthographic camera,
    //    where w is always 1.  It is here so a perspective light - a spot -
    //    would work too.
    vec3 P = FragPosLightSpace.xyz / FragPosLightSpace.w;
    P = P * 0.5 + 0.5;

    // 2. Outside the sun's box nothing was drawn into the map, so nothing
    //    there can cast a shadow.  Call it lit, rather than read the map's
    //    clamped edge and invent a shadow.
    if(P.z > 1.0 || P.x < 0.0 || P.x > 1.0 || P.y < 0.0 || P.y > 1.0)
    {
        return 0.0;
    }

    // 3. Bias.  Without it a surface shadows ITSELF in stripes - "shadow
    //    acne".  One shadow-map pixel covers a small patch of surface, but
    //    stores a single depth for it, so on a slanted patch half of it is
    //    always a little further from the sun than that one number.  Moving
    //    the comparison back a touch stops it.  The more a surface is angled
    //    away from the sun, the more its patch slants, so the bias grows as
    //    dot(normal, lightDir) shrinks.
    float Bias = max(0.002 * (1.0 - dot(normal, lightDir)), 0.0005);

    // 4. PCF - percentage-closer filtering.  One comparison gives a hard,
    //    pixel-stepped edge.  Nine - this map pixel and its eight neighbours
    //    - averaged, give a soft one: a point near a shadow's edge comes out
    //    as, say, 5 of 9 in shadow = 0.56 rather than a hard 1.
    //
    //    NOT the same as filtering the map LINEAR, which would average the
    //    depths themselves.  This compares first and averages the answers.
    vec2 TexelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float Shadow = 0.0;
    for(int X = -1; X <= 1; ++X)
    {
        for(int Y = -1; Y <= 1; ++Y)
        {
            // The closest thing the sun saw in this direction...
            float Closest = texture(shadowMap, P.xy + vec2(X, Y)*TexelSize).r;

            // ...and is THIS point further from the sun than that?
            Shadow += (P.z - Bias > Closest) ? 1.0 : 0.0;
        }
    }

    return Shadow / 9.0;
}

/*
  Ambient light from a sky above and a ground below, blended by which way
  the surface faces.  Stands in for all the light that has bounced around
  the scene - a flat grey from everywhere was what made shadows look dead.
*/
vec3 HemisphereAmbient()
{
    const vec3 SkyColor    = vec3(0.10, 0.13, 0.20);   // bluish, from above
    const vec3 GroundColor = vec3(0.08, 0.06, 0.04);   // warm, bounced off the stone below

    // Y of the world normal: +1 facing straight up, 0 sideways, -1 straight down.
    // Remapped to 0..1: 1 = all sky, 0.5 = half and half, 0 = all ground.
    float Up = normalize(WorldNormal).y * 0.5 + 0.5;

    return mix(GroundColor, SkyColor, Up);
}

vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir)
{
    // Negated because light.direction points INTO the scene, and the dot
    // product below wants a vector pointing from the surface back to the light.
    vec3 lightDir = normalize(-light.direction);

    float diff = max(dot(normal, lightDir), 0.0);

    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);

    vec3 DiffuseSample = material.diffuseColor * texture(material.diffuse, TexCoords).rgb;

    vec3 ambient  =  HemisphereAmbient() * DiffuseSample;
    vec3 diffuse  = light.diffuse  * diff * DiffuseSample;
    vec3 specular = light.specular * spec * material.specularColor * SpecularMap(TexCoords);

    // Only the DIRECT sunlight is blocked.  Ambient stands for light that
    // has bounced around the scene and arrives from everywhere, so an
    // object between here and the sun does not stop it - which is also
    // what keeps shadows from going pitch black.
    float Shadow = CalcShadow(normal, lightDir);

    return ambient + (1.0 - Shadow) * (diffuse + specular);
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
    vec3 specular = light.specular * spec * material.specularColor * SpecularMap(TexCoords);

    return (ambient + diffuse + specular) * attenuation;
}

void main()
{
    // Alpha testing.  Foliage and chains are flat cards with the
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

    // Contributions ADD, because light adds in the real world.
    vec3 result = CalcDirLight(dirLight, norm, viewDir);

    for(int i = 0; i < pointLightCount; ++i)
    {
        result += CalcPointLight(pointLights[i], norm, FragPos, viewDir);
    }

    FragColor = vec4(result, 1.0);
}
