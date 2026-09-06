#version 330 core
out vec4 FragColor;

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;

struct Material {
    sampler2D diffuse;
    sampler2D specular;
    float shininess;
};

struct Light {
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;

    // Book ch. 16.2 - attenuation.  Distance falls off as
    // 1 / (constant + linear*d + quadratic*d*d).
    float constant;
    float linear;
    float quadratic;

    // Book ch. 16.3 - the cone.  Stored as the COSINE of the half-angle, not
    // the angle: dot() below hands back a cosine for free, and comparing
    // cosines avoids an acos() per fragment.
    //
    // Book ch. 16.4 - outerCutOff is a WIDER cone, so its cosine is SMALLER.
    // The band between the two is the soft edge.
    float cutOff;
    float outerCutOff;
};

uniform Light light;
uniform Material material;

void main()
{
    vec3 DiffuseSample = texture(material.diffuse, TexCoords).rgb;

    // Outside the cone test, so the scene is never pitch black.
    vec3 ambient = light.ambient * DiffuseSample;

    // NOTE(yigit): In VIEW SPACE the camera sits at the origin and looks down
    // -Z, by definition.  So a flashlight held at the camera has a position
    // and a direction that are CONSTANTS - the book has to pass camera.Position
    // and camera.Front as uniforms only because it lights in world space.
    const vec3 SpotDirection = vec3(0.0, 0.0, -1.0);

    // The light is at the origin, so "towards the light" is just -FragPos.
    vec3 lightDir = normalize(-FragPos);

    // dot() of two unit vectors IS the cosine of the angle between them.
    // -SpotDirection points back up the cone's axis, towards the lamp, the same
    // way lightDir does - so theta is 1 dead centre and falls off outward.
    //
    // NOTE(yigit): Cosine runs BACKWARDS.  A smaller angle gives a BIGGER
    // cosine, so "inside the cone" is > cutOff, not <.
    float theta = dot(lightDir, -SpotDirection);

    // Book ch. 16.4 - the soft edge, and the reason there is no longer an
    // if(theta > cutOff) here.  The clamp already covers all three cases:
    //   inside the inner cone  -> theta large  -> clamps to 1, full light
    //   in the band between    -> 0 .. 1       -> the fade
    //   outside the outer cone -> theta small  -> clamps to 0, nothing
    // Branching on cutOff as well would throw the fade band away before this
    // ever got to soften it.
    float epsilon   = light.cutOff - light.outerCutOff;
    float intensity = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);

    vec3 norm = normalize(Normal);

    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = light.diffuse * diff * DiffuseSample;

    // In view space the eye is the origin too, so for a flashlight held at
    // the camera viewDir and lightDir are literally the same vector.
    vec3 viewDir = lightDir;
    vec3 reflectDir = reflect(-lightDir, norm);

    float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
    vec3 specular = light.specular * spec * texture(material.specular, TexCoords).rgb;

    // Distance from the camera, which is where the light is.
    float Distance = length(FragPos);
    float attenuation = 1.0 / (light.constant +
                               light.linear * Distance +
                               light.quadratic * (Distance * Distance));

    // Both scalars, so the order they are applied in does not matter.  Ambient
    // gets neither, which is what keeps the scene outside the cone visible.
    diffuse  *= attenuation * intensity;
    specular *= attenuation * intensity;

    FragColor = vec4(ambient + diffuse + specular, 1.0);
}
