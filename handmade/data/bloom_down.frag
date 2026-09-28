#version 330 core

// One step DOWN the bloom chain: draws a half-size copy of srcTexture.

in vec2 UV;
out vec4 FragColor;

uniform sampler2D srcTexture;   // the BIGGER level being shrunk
uniform vec2 srcTexelSize;      // 1 / its size: the width of one of ITS pixels

/*
  13 reads in a pattern around this pixel, averaged with weights.

  Picking one pixel out of every 2x2 block would shrink the image but flicker
  as the camera moves - a tiny bright spot would appear and vanish depending
  on which pixel got picked.  Reading a wider area, weighted smoothly, stops
  that.

      a . b . c
      . j . k .
      d . e . f        e = this pixel's centre
      . l . m .        one step = one pixel of the SOURCE
      g . h . i
*/
void main()
{
    float x = srcTexelSize.x;
    float y = srcTexelSize.y;

    vec3 a = texture(srcTexture, UV + vec2(-2.0*x,  2.0*y)).rgb;
    vec3 b = texture(srcTexture, UV + vec2(   0.0,  2.0*y)).rgb;
    vec3 c = texture(srcTexture, UV + vec2( 2.0*x,  2.0*y)).rgb;
    vec3 d = texture(srcTexture, UV + vec2(-2.0*x,    0.0)).rgb;
    vec3 e = texture(srcTexture, UV                       ).rgb;
    vec3 f = texture(srcTexture, UV + vec2( 2.0*x,    0.0)).rgb;
    vec3 g = texture(srcTexture, UV + vec2(-2.0*x, -2.0*y)).rgb;
    vec3 h = texture(srcTexture, UV + vec2(   0.0, -2.0*y)).rgb;
    vec3 i = texture(srcTexture, UV + vec2( 2.0*x, -2.0*y)).rgb;
    vec3 j = texture(srcTexture, UV + vec2(    -x,      y)).rgb;
    vec3 k = texture(srcTexture, UV + vec2(     x,      y)).rgb;
    vec3 l = texture(srcTexture, UV + vec2(    -x,     -y)).rgb;
    vec3 m = texture(srcTexture, UV + vec2(     x,     -y)).rgb;

    // The weights add up to exactly 1.0, so no light is gained or lost:
    //   0.125 + 4 x 0.03125 + 4 x 0.0625 + 4 x 0.125 = 1.0
    vec3 Color = e * 0.125;
    Color += (a + c + g + i) * 0.03125;
    Color += (b + d + f + h) * 0.0625;
    Color += (j + k + l + m) * 0.125;

    FragColor = vec4(Color, 1.0);
}
