#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoord;

uniform vec3 lightPos;
uniform vec3 viewPos;
uniform vec3 lightColor;
uniform vec3 objectColor;

void main()
{
    // Book ch. 13.1 - ambient.  A flat fraction of the light reaches every
    // surface regardless of which way it faces, standing in for light that has
    // bounced around the scene before arriving.
    float ambientStrength = 0.1;
    vec3 ambient = ambientStrength * lightColor;

    // Book ch. 13.4 - diffuse.  How much light a surface catches depends on
    // the angle between the way it faces and the direction the light is
    // coming from.
    //
    // Both vectors are normalized because the dot product of two UNIT vectors
    // is exactly the cosine of the angle between them: 1 when they line up,
    // 0 at right angles.  Skip the normalize and the lengths scale the result
    // as well, which is the classic beginner mistake the book warns about.
    //
    // Normal arrives interpolated across the triangle, which can leave it
    // slightly off unit length even though every vertex fed in a unit vector.
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);

    // max against 0 because past 90 degrees the dot product goes negative -
    // that is a surface facing away from the light, which should get nothing,
    // not a negative amount of light.
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;

    // Book ch. 13.6 - specular.  Diffuse asks "is this surface facing the
    // light".  Specular asks "is the light bouncing off it INTO MY EYE", so it
    // is the only term that depends on where the camera is - which is why the
    // highlight slides across the face as you fly around.
    float specularStrength = 0.5;

    vec3 viewDir = normalize(viewPos - FragPos);

    // reflect() wants a vector pointing FROM the light TOWARDS the surface,
    // but lightDir above points the other way, so it gets negated here.
    vec3 reflectDir = reflect(-lightDir, norm);

    // The exponent is shininess.  Raising a number below 1 to a high power
    // collapses it fast, so a bigger value makes a smaller, tighter highlight:
    // 32 is a modest gloss, 256 is almost a mirror point.
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
    vec3 specular = specularStrength * spec * lightColor;

    // Add all three contributions, then filter by what the surface reflects.
    vec3 result = (ambient + diffuse + specular) * objectColor;
    FragColor = vec4(result, 1.0);
}
