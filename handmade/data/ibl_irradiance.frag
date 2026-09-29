#version 330 core

// For a surface facing Direction: all the sky light arriving over the half
// of the sky it faces, each part weighted by how squarely it arrives.  That
// is what a matte surface scatters, and it changes so slowly with direction
// that a 32x32 cube holds it.

in vec3 Direction;
out vec4 FragColor;

uniform samplerCube sky;

const float PI = 3.14159265359;

void main()
{
    vec3 N = normalize(Direction);

    // Two directions at right angles to N, to walk around it with.
    vec3 Up    = (abs(N.y) < 0.999) ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);
    vec3 Right = normalize(cross(Up, N));
    Up = cross(N, Right);

    // Walk the half-sphere around N: Phi round the edge, Theta down from the
    // top.  About 4000 samples - fine once at startup, far too many per pixel
    // per frame, which is the reason this is baked.
    const float Step = 0.05;
    vec3 Sum = vec3(0.0);
    float Count = 0.0;

    for(float Phi = 0.0; Phi < 2.0*PI; Phi += Step)
    {
        for(float Theta = 0.0; Theta < 0.5*PI; Theta += Step)
        {
            // The sample direction, first around the Z axis, then turned
            // to sit around N.
            vec3 Local = vec3(sin(Theta)*cos(Phi), sin(Theta)*sin(Phi), cos(Theta));
            vec3 SampleDir = Local.x*Right + Local.y*Up + Local.z*N;

            // cos(Theta): light arriving at a slant is spread over more
            // surface.  sin(Theta): rings near the top of the half-sphere
            // are smaller, so each step there covers less sky.
            //
            // Mip 3 of the sky, not the full-size face - already averaged,
            // so a single bright texel cannot become a sparkle.
            Sum += textureLod(sky, SampleDir, 3.0).rgb * cos(Theta) * sin(Theta);
            Count += 1.0;
        }
    }

    // Stored so that the lit shader's diffuse light is just this times the
    // surface colour - the same convention the hemisphere ambient used.
    FragColor = vec4(PI * Sum / Count, 1.0);
}
