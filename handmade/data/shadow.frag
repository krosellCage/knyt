#version 330 core

// This pass exists only for its DEPTH, which the GPU writes by itself for
// every pixel that survives.  There is no colour to write - the framebuffer
// does not even have a colour image.
//
// The one job left is deciding which pixels survive.

in vec2 TexCoords;

// The same two textures the lit shader's alpha test reads.  GameDrawModel
// binds them to units 0 and 2 for every submesh, whichever program it draws
// with - the ShadowPass command points these samplers at those units.
uniform sampler2D diffuseMap;
uniform sampler2D alphaMask;

void main()
{
    // Exactly the lit shader's alpha test.  Leaves and chains are flat cards
    // with their shape cut out by a mask.  A pixel thrown away here writes no
    // depth, so the sun shines through the gaps - without this, every leaf
    // card would cast a solid rectangle.
    float Opacity = texture(diffuseMap, TexCoords).a *
                    texture(alphaMask, TexCoords).r;
    if(Opacity < 0.5)
    {
        discard;
    }
}
