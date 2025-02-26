#version 110

#define INTENSITY_CORRECTION 0.6

// normalized values for (-0.6/1.31, 0.6/1.31, 1./1.31)
const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);
#define LIGHT_TOP_DIFFUSE    (0.8 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SPECULAR   (0.125 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SHININESS  20.0

// normalized values for (1./1.43, 0.2/1.43, 1./1.43)
const vec3 LIGHT_FRONT_DIR = vec3(0.6985074, 0.1397015, 0.6985074);
#define LIGHT_FRONT_DIFFUSE  (0.3 * INTENSITY_CORRECTION)
//#define LIGHT_FRONT_SPECULAR (0.0 * INTENSITY_CORRECTION)
//#define LIGHT_FRONT_SHININESS 5.0

#define INTENSITY_AMBIENT    0.3

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform mat3 view_normal_matrix;
uniform mat4 volume_world_matrix;

// Clipping planes used to clip the tool marker model.
uniform vec4 clipping_planes[2];

attribute vec3 v_position;
attribute vec3 v_normal;

// x = diffuse, y = specular;
varying vec2 intensity;
varying vec2 clipping_planes_dots;

void main()
{
    // First transform the normal into camera space and normalize the result.
    vec3 eye_normal = normalize(view_normal_matrix * v_normal);

    // Compute the cos of the angle between the normal and lights direction. The light is directional so the direction is constant for every vertex.
    // Since these two are normalized the cosine is the dot product. We also need to clamp the result to the [0,1] range.
    float NdotL = max(dot(eye_normal, LIGHT_TOP_DIR), 0.0);

    intensity.x = INTENSITY_AMBIENT + NdotL * LIGHT_TOP_DIFFUSE;
    vec4 position = view_model_matrix * vec4(v_position, 1.0);
    intensity.y = LIGHT_TOP_SPECULAR * pow(max(dot(-normalize(position.xyz), reflect(-LIGHT_TOP_DIR, eye_normal)), 0.0), LIGHT_TOP_SHININESS);

    // Perform the same lighting calculation for the 2nd light source (no specular applied).
    NdotL = max(dot(eye_normal, LIGHT_FRONT_DIR), 0.0);
    intensity.x += NdotL * LIGHT_FRONT_DIFFUSE;

    // Point in homogenous coordinates.
    vec4 world_pos = volume_world_matrix * vec4(v_position, 1.0);
    // Fill in the scalars for fragment shader clipping. Fragments with any of these components lower than zero are discarded.
    clipping_planes_dots.x = dot(world_pos, clipping_planes[0]);
    clipping_planes_dots.y = dot(world_pos, clipping_planes[1]);

    gl_Position = projection_matrix * position;
}
