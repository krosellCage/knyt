#version 330 core

// Where on the screen this pixel is: (0,0) bottom-left to (1,1) top-right.
// The fragment shader uses it to know which pixel of the HDR texture to read.
out vec2 UV;

void main()
{
    // No vertex buffer.  gl_VertexID is 0, 1 or 2, and the bit tricks turn
    // it into three corners:
    //   ID 0 -> (0, 0)
    //   ID 1 -> (2, 0)
    //   ID 2 -> (0, 2)
    vec2 P = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);

    UV = P;

    // Screen space runs -1..+1, so *2-1 makes the corners
    // (-1,-1), (3,-1), (-1,3): one triangle bigger than the screen.
    gl_Position = vec4(P * 2.0 - 1.0, 0.0, 1.0);
}
