#version 330 core

in vec2 UV;
out vec4 FragColor;

uniform sampler2D hdrScene;     // your HDR texture
uniform float exposure;         // brightness multiplier, like a camera's

uniform sampler2D bloomTexture; // the blurred image, from the bloom chain
uniform float bloomStrength;    // how much of it to mix in: 0.04 = 4%

vec3 ACES(vec3 x)
{
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
void main()
{
    // 1. This pixel's HDR colour.  Anything: 0.01, 1.0, 37.0...
    vec3 Color = texture(hdrScene, UV).rgb;

    // 2. Bloom.  MIX, not add: 96% sharp image + 4% blurred image.  The total
    //    stays the same, so bloom moves light around rather than inventing
    //    it - and a pixel only visibly glows if it is bright enough that 4%
    //    of it still stands out.  A lamp at 20.0 does; a wall at 0.3 does not.
    //
    //    Before exposure and the curve, because bloom happens in the LENS, to
    //    real light.  Tonemapping and gamma are about the SCREEN.
    vec3 Bloom = texture(bloomTexture, UV).rgb;
    Color = mix(Color, Bloom, bloomStrength);

    // 3. Exposure: scale everything BEFORE the curve.
    Color *= exposure;

    // 4. ACES: squeeze 0..infinity into 0..1 with an S-shaped curve, so
    //    highlights roll off smoothly instead of being cut off.
    Color = ACES(Color);

    // 5. Encode for the screen: linear light -> sRGB.  Always last.
    Color = pow(Color, vec3(1.0 / 2.2));

    FragColor = vec4(Color, 1.0);
}
