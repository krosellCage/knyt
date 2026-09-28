#version 330 core

/*
  The outline pass of a stencil outline.

  Deliberately the simplest shader in the engine: one flat colour, no lighting,
  no texture.  An outline is not a surface being lit, it is a marker drawn on
  top of the image, so anything that made it respond to the lights would make
  it disappear on the unlit side of the object it is meant to be marking.

  The colour is a constant rather than a uniform because there is one caller
  and one outline.  It becomes a uniform the first time something wants a
  second colour, and not before.
*/

out vec4 FragColor;

void main()
{
    FragColor = vec4(0.96, 0.55, 0.12, 1.0);
}
