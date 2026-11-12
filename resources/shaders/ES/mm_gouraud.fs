#version 100
#extension GL_OES_standard_derivatives : enable

precision highp float;

#define INTENSITY_CORRECTION 0.6

// normalized values for (-0.6/1.31, 0.6/1.31, 1./1.31)
const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);
#define LIGHT_TOP_DIFFUSE    (0.8 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SPECULAR   (0.125 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SHININESS  20.0

// normalized values for (1./1.43, 0.2/1.43, 1./1.43)
const vec3 LIGHT_FRONT_DIR = vec3(0.6985074, 0.1397015, 0.6985074);
#define LIGHT_FRONT_DIFFUSE  (0.3 * INTENSITY_CORRECTION)

#define INTENSITY_AMBIENT    0.3

const vec3  ZERO    = vec3(0.0, 0.0, 0.0);
const float EPSILON = 0.0001;

struct OverhangDetection
{
    bool  enabled;
    float max_normal_z;
    mat3  world_normal_matrix;
};

uniform vec4 uniform_color;

uniform bool volume_mirrored;

uniform mat4 view_model_matrix;
uniform mat3 view_normal_matrix;
uniform OverhangDetection overhang;

varying vec3 clipping_planes_dots;
varying vec4 model_pos;

void main()
{
    if (any(lessThan(clipping_planes_dots, ZERO)))
        discard;

    vec3  color = uniform_color.rgb;
    float alpha = uniform_color.a;

    vec3 triangle_normal = normalize(cross(dFdx(model_pos.xyz), dFdy(model_pos.xyz)));

    if (volume_mirrored)
        triangle_normal = -triangle_normal;

    // First transform the normal into camera space and normalize the result.
    vec3 eye_normal = normalize(view_normal_matrix * triangle_normal);

    // Z component of normal vector in world coordinate used for overhang detection.
    float world_normal_z = overhang.enabled ? (normalize(overhang.world_normal_matrix * triangle_normal)).z : 0.0;

    // Apply a different color for detected overhangs.
    if (overhang.enabled && world_normal_z < overhang.max_normal_z - EPSILON) {
        color = vec3(0.7, 0.7, 1.0);
        alpha = 1.0;
    }

    // Compute the cos of the angle between the normal and lights direction. The light is directional so the direction is constant for every vertex.
    // Since these two are normalized the cosine is the dot product. We also need to clamp the result to the [0,1] range.
    float NdotL = max(dot(eye_normal, LIGHT_TOP_DIR), 0.0);

    // x = diffuse, y = specular;
    vec2 intensity = vec2(0.0);
    intensity.x = INTENSITY_AMBIENT + NdotL * LIGHT_TOP_DIFFUSE;
    vec3 position = (view_model_matrix * model_pos).xyz;
    intensity.y = LIGHT_TOP_SPECULAR * pow(max(dot(-normalize(position), reflect(-LIGHT_TOP_DIR, eye_normal)), 0.0), LIGHT_TOP_SHININESS);

    // Perform the same lighting calculation for the 2nd light source (no specular applied).
    NdotL = max(dot(eye_normal, LIGHT_FRONT_DIR), 0.0);
    intensity.x += NdotL * LIGHT_FRONT_DIFFUSE;

    gl_FragColor = vec4(vec3(intensity.y) + color * intensity.x, alpha);
}
