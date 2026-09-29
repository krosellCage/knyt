#version 330 core

// The part of a reflection that has nothing to do with the sky: for a
// viewing angle (UV.x = NdotV) and a roughness (UV.y), how much of the
// reflected light survives Fresnel and the surface shading itself.
//
// Split in two, because Fresnel depends on the material's F0 and that is
// only known later, per pixel:
//     reflection = sky * (F0 * Scale + Bias)
// This stores Scale in red and Bias in green.

in vec2 UV;
out vec4 FragColor;

const float PI = 3.14159265359;
const uint SampleCount = 1024u;

float RadicalInverse(uint Bits)
{
    Bits = (Bits << 16u) | (Bits >> 16u);
    Bits = ((Bits & 0x55555555u) << 1u) | ((Bits & 0xAAAAAAAAu) >> 1u);
    Bits = ((Bits & 0x33333333u) << 2u) | ((Bits & 0xCCCCCCCCu) >> 2u);
    Bits = ((Bits & 0x0F0F0F0Fu) << 4u) | ((Bits & 0xF0F0F0F0u) >> 4u);
    Bits = ((Bits & 0x00FF00FFu) << 8u) | ((Bits & 0xFF00FF00u) >> 8u);
    return float(Bits) * 2.3283064365386963e-10;
}

vec2 Hammersley(uint I, uint N)
{
    return vec2(float(I)/float(N), RadicalInverse(I));
}

vec3 ImportanceSampleGGX(vec2 Xi, vec3 N, float Roughness)
{
    float A = Roughness*Roughness;

    float Phi = 2.0*PI*Xi.x;
    float CosTheta = sqrt((1.0 - Xi.y) / (1.0 + (A*A - 1.0)*Xi.y));
    float SinTheta = sqrt(1.0 - CosTheta*CosTheta);

    // N is always +Z here, so no turning into place is needed.
    return vec3(cos(Phi)*SinTheta, sin(Phi)*SinTheta, CosTheta);
}

// Self-shadowing, as in the lit shader - but with K = alpha/2 rather than
// (roughness+1)^2/8.  That other K is a tweak for small lights like the sun,
// and would be wrong for light arriving from the whole sky.
float GeometrySchlickGGX(float NdotX, float Roughness)
{
    float A = Roughness*Roughness;
    float K = A / 2.0;
    return NdotX / (NdotX*(1.0 - K) + K);
}

void main()
{
    float NdotV = max(UV.x, 0.001);     // never exactly edge-on
    float Roughness = UV.y;

    // A surface facing +Z, looked at from the angle NdotV.
    vec3 V = vec3(sqrt(1.0 - NdotV*NdotV), 0.0, NdotV);
    vec3 N = vec3(0.0, 0.0, 1.0);

    float Scale = 0.0;
    float Bias = 0.0;

    for(uint I = 0u; I < SampleCount; ++I)
    {
        vec3 H = ImportanceSampleGGX(Hammersley(I, SampleCount), N, Roughness);
        vec3 L = normalize(2.0*dot(V, H)*H - V);

        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);

        if(NdotL > 0.0)
        {
            float G = GeometrySchlickGGX(NdotV, Roughness) *
                      GeometrySchlickGGX(NdotL, Roughness);
            float GVis = G*VdotH / (NdotH*NdotV);

            // Fresnel's (1 - VdotH)^5, pulled out of the F0 term so the two
            // halves can be stored separately.
            float Fc = pow(1.0 - VdotH, 5.0);

            Scale += (1.0 - Fc)*GVis;
            Bias  += Fc*GVis;
        }
    }

    FragColor = vec4(Scale / float(SampleCount), Bias / float(SampleCount), 0.0, 1.0);
}
