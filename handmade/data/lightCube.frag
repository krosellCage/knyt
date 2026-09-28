#version 330 core

out vec4 FragColor;
uniform vec4 LightColor;
void main()
{
   // Far brighter than white, so the lamps glow under bloom.  Only possible
   // because the scene draws into a float texture - in an 8-bit one this
   // would be cut straight back down to 1.0.
   FragColor = vec4(LightColor.rgb * 20.0, 1.0);
}
