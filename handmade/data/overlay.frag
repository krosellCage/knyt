#version 330 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D atlas;
uniform vec3 color;

void main()
{
    // NOTE(yigit): The atlas carries COVERAGE, not colour - one channel saying
    // how much of this pixel the glyph covers.  GL_RED is what a single-channel
    // texture loads as, so the value is in .r, and it becomes the ALPHA of a
    // flat colour rather than the colour itself.
    //
    // That alpha is a partial value at every antialiased edge, which is why
    // this pass needs real blending and cannot use discard.
    float Coverage = texture(atlas, TexCoords).r;

    FragColor = vec4(color, Coverage);
}
