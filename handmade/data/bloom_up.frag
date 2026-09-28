#version 330 core

// One step UP the bloom chain: draws a smoothed, double-size copy of
// srcTexture, which blending then ADDS on top of the bigger level.

in vec2 UV;
out vec4 FragColor;

uniform sampler2D srcTexture;   // the SMALLER level being grown
uniform float filterRadius;     // how far apart the reads are, in UV units

/*
  A 3x3 "tent": the centre counts most, the edges half as much, the corners
  a quarter as much.

      1 2 1
      2 4 2      / 16, so the weights add up to 1.0
      1 2 1

  This shader only produces the grown copy.  The ADDING happens in the
  blending hardware (glBlendFunc(GL_ONE, GL_ONE)), which is how every level's
  blur ends up stacked together in the biggest one.
*/
void main()
{
    float r = filterRadius;

    vec3 a = texture(srcTexture, UV + vec2(-r,   r)).rgb;
    vec3 b = texture(srcTexture, UV + vec2(0.0,  r)).rgb;
    vec3 c = texture(srcTexture, UV + vec2( r,   r)).rgb;
    vec3 d = texture(srcTexture, UV + vec2(-r, 0.0)).rgb;
    vec3 e = texture(srcTexture, UV                ).rgb;
    vec3 f = texture(srcTexture, UV + vec2( r, 0.0)).rgb;
    vec3 g = texture(srcTexture, UV + vec2(-r,  -r)).rgb;
    vec3 h = texture(srcTexture, UV + vec2(0.0, -r)).rgb;
    vec3 i = texture(srcTexture, UV + vec2( r,  -r)).rgb;

    vec3 Color = e * 4.0;
    Color += (b + d + f + h) * 2.0;
    Color += (a + c + g + i);
    Color *= 1.0 / 16.0;

    FragColor = vec4(Color, 1.0);
}
