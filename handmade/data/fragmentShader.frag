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

    // map_bump, a HEIGHT map: bright = raised.  Only how it changes across
    // the surface matters - that is what tilts the normal.  Surfaces without
    // one get the 1x1 white texture: the same height everywhere, so no tilt.
    sampler2D bump;

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

/*
  How deep the bumps are: the height, in world units, between black and white
  in the bump map.  Sponza at scale 0.02 has one unit = 50 cm, so 0.03 is
  about a centimetre and a half - roughly how far mortar sits back from a
  brick face.  0 turns bump mapping off exactly.
*/
const float BumpStrength = 0.03;

/*
  Tilts the surface normal N to follow the height map, at this pixel.

  NOTE(yigit): Height maps, not normal maps, and no tangent vectors - the
  mesh carries none.  Instead this measures, on SCREEN, how the surface and
  the height both change from this pixel to its neighbours, and works the
  tilt out from those two.  dFdx/dFdy are the GPU telling us how much a
  value changes to the next pixel right (x) and the next pixel up (y); it
  can, because pixels are shaded in 2x2 blocks that run in lockstep.

  The method is Morten Mikkelsen's, "Bump Mapping Unparametrized Surfaces on
  the GPU" (2010).

  N is the view-space normal, P the view-space position, UV the texture
  coordinate.  Returns the tilted normal, length 1.
*/
vec3 BumpNormal(vec3 N, vec3 P, vec2 UV)
{
    // 1. How far across the SURFACE one pixel step goes: from this pixel to
    //    its right neighbour (dPdx) and to its upper neighbour (dPdy).
    vec3 dPdx = dFdx(P);
    vec3 dPdy = dFdy(P);

    // 2. How much the HEIGHT changes over those same two steps.
    //
    //    Read at the neighbours' UVs rather than dFdx(Height).  dFdx works
    //    on whole 2x2 blocks, so it returns the same answer for all four
    //    pixels of a block and the bumps would come out in blocky steps.
    vec2 dUVdx = dFdx(UV);
    vec2 dUVdy = dFdy(UV);

    float Height = texture(material.bump, UV).r;
    float dHdx   = texture(material.bump, UV + dUVdx).r - Height;
    float dHdy   = texture(material.bump, UV + dUVdy).r - Height;

    // 3. Turn "the height rises this much over these two surface steps"
    //    into a direction along the surface: the way uphill points.
    //
    //    R1 and R2 are the two steps turned sideways within the surface
    //    (crossed with N), so that walking uphill in x and in y can be added
    //    up into one direction.  Det is the area the two steps span, with a
    //    sign that flips if the texture is mirrored; it scales N below so
    //    that the tilt comes out the same whatever the pixel size.
    vec3 R1 = cross(dPdy, N);
    vec3 R2 = cross(N, dPdx);
    float Det = dot(dPdx, R1);

    vec3 Uphill = sign(Det) * (dHdx * R1 + dHdy * R2);

    // 4. Lean the normal AWAY from uphill.  On a slope rising to the right
    //    the surface faces a little to the left - which is what makes the
    //    near side of a brick catch the light and the far side fall into
    //    shade.
    return normalize(abs(Det) * N - BumpStrength * Uphill);
}

// ---------------------------------------------------------------------------
// Physically based shading: Cook-Torrance, the GGX flavour.
//
// Every surface is described by three numbers instead of Phong's grab-bag of
// ambient/diffuse/specular colours and an exponent:
//
//   Albedo     its colour
//   Roughness  0 = mirror-smooth, 1 = chalk.  How spread out reflections are.
//   Metallic   0 = stone, wood, cloth, plastic.  1 = bare metal.
//
// and light obeys one rule Phong never did: a surface cannot send out more
// light than arrives.  What it reflects as a mirror-like highlight is taken
// away from what it scatters as colour.
// ---------------------------------------------------------------------------

const float PI = 3.14159265359;

// Every dielectric - anything not a metal - reflects about 4% of the light
// that hits it head-on, whatever its colour.  Water, stone, plastic and skin
// all sit within a few percent of this.
const vec3 DielectricF0 = vec3(0.04);

// NOTE(yigit): Sponza predates PBR and is all stone, cloth and wood, so
// nothing in it is a metal.  A per-material value belongs in the .mtl the
// day a PBR asset arrives.
const float Metallic = 0.0;

/*
  D: how many of the surface's microscopic facets face exactly along H, the
  half vector - the only facets that can mirror this light into the eye.

  A smooth surface has almost all its facets facing along the normal, so D
  is a tall, narrow spike around N: a small, bright highlight.  A rough one
  has them scattered everywhere: a low, wide hump, a big, dim highlight.
  Trowbridge-Reitz, better known as GGX.
*/
float DistributionGGX(vec3 N, vec3 H, float Roughness)
{
    // Squared, because roughness is set by eye and this makes it read
    // evenly: 0.5 then LOOKS half as rough as 1, which the raw value does not.
    float A  = Roughness*Roughness;
    float A2 = A*A;

    float NdotH = max(dot(N, H), 0.0);
    float Denominator = NdotH*NdotH*(A2 - 1.0) + 1.0;

    return A2 / (PI*Denominator*Denominator);
}

/*
  G: how many of those facets are not hidden by their neighbours - shadowed
  from the light, or blocked from the eye.  Matters most at grazing angles
  and on rough surfaces, and is what keeps their edges from glowing.
  Schlick's approximation of Smith's model, once towards the eye and once
  towards the light.
*/
float GeometrySchlickGGX(float NdotX, float Roughness)
{
    float R = Roughness + 1.0;
    float K = (R*R) / 8.0;

    return NdotX / (NdotX*(1.0 - K) + K);
}

float GeometrySmith(float NdotV, float NdotL, float Roughness)
{
    return GeometrySchlickGGX(NdotV, Roughness) * GeometrySchlickGGX(NdotL, Roughness);
}

/*
  F: what share of the light reflects as a mirror would, rather than going
  into the surface.  F0 is that share looking straight on; towards grazing it
  climbs to 100% for everything - why a lake mirrors the far shore but you
  see the bottom at your feet.  Schlick's approximation.
*/
vec3 FresnelSchlick(float CosTheta, vec3 F0)
{
    return F0 + (1.0 - F0)*pow(clamp(1.0 - CosTheta, 0.0, 1.0), 5.0);
}

/*
  Light arriving from direction L with the given radiance, reflected towards
  the eye along V, by a surface facing N.
*/
vec3 PBRDirect(vec3 N, vec3 V, vec3 L, vec3 Radiance,
               vec3 Albedo, float Roughness)
{
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    // Metals have no diffuse colour at all - their colour IS their
    // reflection - so for them F0 is the albedo.  Dielectrics get the 4%.
    vec3 F0 = mix(DielectricF0, Albedo, Metallic);

    float D = DistributionGGX(N, H, Roughness);
    float G = GeometrySmith(NdotV, NdotL, Roughness);
    vec3  F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    // The mirror-like part.  The 4*NdotV*NdotL corrects for how the facets
    // are counted; the tiny constant stops a divide by zero at grazing
    // angles, where the numerator is already zero anyway.
    vec3 Specular = (D*G*F) / (4.0*NdotV*NdotL + 0.0001);

    // The scattered, coloured part gets only what the mirror part did NOT
    // take - that is the "never more light out than in" rule - and none at
    // all on a metal.  Divided by PI because it spreads over a whole
    // hemisphere of directions.
    vec3 KD = (vec3(1.0) - F)*(1.0 - Metallic);
    vec3 Diffuse = KD*Albedo / PI;

    // NdotL: light arriving at a slant is spread over more surface.
    return (Diffuse + Specular)*Radiance*NdotL;
}

/*
  This surface's roughness.

  NOTE(yigit): An ESTIMATE.  Sponza has no roughness maps, so it is worked
  out from what the .mtl does have.  Ns, the Blinn-Phong exponent, converts
  to GGX's alpha as sqrt(2 / (Ns + 2)), and roughness is the square root of
  alpha (see DistributionGGX).  Ns 70, the floor, comes out at 0.41; Ns 10,
  most of the walls, at 0.64.

  The specular map then roughens it where the map is dark: the floor's gloss
  map makes worn tiles rough and polished ones smooth.  A surface without a
  map reads white there, and keeps the Ns estimate as it is.
*/
float MaterialRoughness()
{
    float Alpha = sqrt(2.0 / (material.shininess + 2.0));
    float Roughness = sqrt(Alpha);

    float Gloss = SpecularMap(TexCoords).r;
    Roughness = mix(1.0, Roughness, Gloss);

    // Never quite a mirror: at roughness 0 the highlight of a sun-sized light
    // shrinks to a single blinding pixel that flickers as the camera moves.
    return clamp(Roughness, 0.05, 1.0);
}

vec3 MaterialAlbedo()
{
    return material.diffuseColor * texture(material.diffuse, TexCoords).rgb;
}

// normal lights the pixel; geoNormal - the flat, un-bumped one - only sets
// the shadow bias, which is about the real triangle and its tilt towards the
// sun.  Bumped, it would shrink wherever a bump faces the sun and bring the
// shadow acne back.
vec3 CalcDirLight(DirLight light, vec3 normal, vec3 geoNormal, vec3 viewDir)
{
    // Negated because light.direction points INTO the scene, and the dot
    // product below wants a vector pointing from the surface back to the light.
    vec3 lightDir = normalize(-light.direction);

    vec3 Albedo = MaterialAlbedo();
    float Roughness = MaterialRoughness();

    // light.diffuse is the sun's colour AND strength - its radiance.
    // light.specular and material.specularColor (Ks) are Phong's, and PBR
    // has no use for either: every surface's highlight now comes from F0.
    vec3 Direct = PBRDirect(normal, viewDir, lightDir, light.diffuse,
                            Albedo, Roughness);

    // The sky-and-ground light, scattered by the surface's colour.  No PI
    // here: HemisphereAmbient gives radiance from a whole hemisphere, and
    // gathering a hemisphere of it multiplies by exactly the PI that the
    // diffuse term divides by.
    vec3 Ambient = HemisphereAmbient() * Albedo * (1.0 - Metallic);

    // Only the DIRECT sunlight is blocked.  Ambient stands for light that
    // has bounced around the scene and arrives from everywhere, so an
    // object between here and the sun does not stop it - which is also
    // what keeps shadows from going pitch black.
    float Shadow = CalcShadow(geoNormal, lightDir);

    return Ambient + (1.0 - Shadow) * Direct;
}

vec3 CalcPointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir)
{
    // Unlike the directional light, this one has a place, so every fragment
    // gets its own direction to it - and its own distance.
    vec3 lightDir = normalize(light.position - fragPos);

    float Distance = length(light.position - fragPos);
    float attenuation = 1.0 / (light.constant +
                               light.linear * Distance +
                               light.quadratic * (Distance * Distance));

    // The same shading as the sun, with the light's strength fading over
    // distance.  Its own ambient is gone - the hemisphere covers that now.
    return PBRDirect(normal, viewDir, lightDir, light.diffuse * attenuation,
                     MaterialAlbedo(), MaterialRoughness());
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

    // The same normal, tilted to follow the bump map.  Lighting uses this
    // one; the flat one is still needed for the shadow bias.
    vec3 bumpedNorm = BumpNormal(norm, FragPos, TexCoords);

    // The eye is the origin in view space, so "towards the viewer" is -FragPos.
    vec3 viewDir = normalize(-FragPos);

    // Contributions ADD, because light adds in the real world.
    vec3 result = CalcDirLight(dirLight, bumpedNorm, norm, viewDir);

    for(int i = 0; i < pointLightCount; ++i)
    {
        result += CalcPointLight(pointLights[i], bumpedNorm, FragPos, viewDir);
    }

    FragColor = vec4(result, 1.0);
}
