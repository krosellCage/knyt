#version 330 core

// For a reflection looking along Direction: the sky blurred the way a
// surface of this roughness blurs it.  Roughness 0 is a sharp mirror image;
// each mip level up is rougher and blurrier.

in vec3 Direction;
out vec4 FragColor;

uniform samplerCube sky;
uniform float roughness;

const float PI = 3.14159265359;
const uint SampleCount = 512u;

// The i-th of N points spread evenly over a square - evenly enough that 512
// of them blur as smoothly as thousands of random ones would.  "Hammersley
// points": i/N across, and i with its bits mirrored down.
float RadicalInverse(uint Bits)
{
    Bits = (Bits << 16u) | (Bits >> 16u);
    Bits = ((Bits & 0x55555555u) << 1u) | ((Bits & 0xAAAAAAAAu) >> 1u);
    Bits = ((Bits & 0x33333333u) << 2u) | ((Bits & 0xCCCCCCCCu) >> 2u);
    Bits = ((Bits & 0x0F0F0F0Fu) << 4u) | ((Bits & 0xF0F0F0F0u) >> 4u);
    Bits = ((Bits & 0x00FF00FFu) << 8u) | ((Bits & 0xFF00FF00u) >> 8u);
    return float(Bits) * 2.3283064365386963e-10;    // / 2^32
}

vec2 Hammersley(uint I, uint N)
{
    return vec2(float(I)/float(N), RadicalInverse(I));
}

// A half vector H, chosen the way GGX spreads a surface's microscopic
// facets: bunched tightly around N when smooth, scattered when rough.
// Spending samples where facets actually are is what makes 512 enough.
vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float Roughness)
{
    float A = Roughness*Roughness;

    float Phi = 2.0*PI*Xi.x;
    float CosTheta = sqrt((1.0 - Xi.y) / (1.0 + (A*A - 1.0)*Xi.y));
    float SinTheta = sqrt(1.0 - CosTheta*CosTheta);

    vec3 Local = vec3(cos(Phi)*SinTheta, sin(Phi)*SinTheta, CosTheta);

    vec3 Up      = (abs(N.z) < 0.999) ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    vec3 TangentX = normalize(cross(Up, N));
    vec3 TangentY = cross(N, TangentX);

    return normalize(TangentX*Local.x + TangentY*Local.y + N*Local.z);
}

float DistributionGGX(float NdotH, float Roughness)
{
    float A  = Roughness*Roughness;
    float A2 = A*A;
    float D  = NdotH*NdotH*(A2 - 1.0) + 1.0;
    return A2 / (PI*D*D);
}

void main()
{
    // NOTE(yigit): Assumes the eye looks straight down the reflection
    // (N = V = R).  Not true at grazing angles, where real reflections
    // stretch - but it is what lets the whole thing be baked per direction
    // at all, and the BRDF table makes up most of the difference.
    vec3 N = normalize(Direction);
    vec3 V = N;

    // The size of one texel of the sky's full-size face, as a patch of the
    // sphere.  Compared below with the patch each sample stands for, to pick
    // a sky mip level that matches.
    float SkySize = float(textureSize(sky, 0).x);
    float TexelSolidAngle = 4.0*PI / (6.0*SkySize*SkySize);

    vec3 Sum = vec3(0.0);
    float Weight = 0.0;

    for(uint I = 0u; I < SampleCount; ++I)
    {
        vec3 H = ImportanceSampleGGX(Hammersley(I, SampleCount), N, roughness);
        vec3 L = normalize(2.0*dot(V, H)*H - V);    // V mirrored about H

        float NdotL = dot(N, L);
        if(NdotL > 0.0)
        {
            // Each sample covers a patch of sky that shrinks where samples
            // are dense.  Read the sky mip whose texels are about that size,
            // so the patch is averaged rather than hit at one bright point.
            float NdotH = max(dot(N, H), 0.0);
            float HdotV = max(dot(H, V), 0.0);
            float PDF = DistributionGGX(NdotH, roughness)*NdotH / (4.0*HdotV) + 0.0001;
            float SampleSolidAngle = 1.0 / (float(SampleCount)*PDF + 0.0001);
            float Mip = (roughness == 0.0) ? 0.0 : 0.5*log2(SampleSolidAngle / TexelSolidAngle);

            Sum += textureLod(sky, L, Mip).rgb * NdotL;
            Weight += NdotL;
        }
    }

    FragColor = vec4(Sum / Weight, 1.0);
}
