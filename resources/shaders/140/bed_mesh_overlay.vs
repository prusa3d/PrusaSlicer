#version 140

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

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform mat3 view_normal_matrix;

// Parameters for reconstructing the raw mm-space Z value at each fragment,
// for contour-line drawing. v_position.z is encoded as
//   20.0 + (raw_z - z_mean) * z_scale
// so raw_z = (v_position.z - z_base) / z_scale + z_mean.
uniform float u_z_mean;
uniform float u_z_scale;
uniform float u_z_base;

in vec3 v_position;
in vec3 v_normal;
in vec3 v_extra; // per-vertex color (RGB)

// x = tainted, y = specular;
out vec2 intensity;
out vec3 vertex_color;
out float raw_z_mm;

void main()
{
    vec3 normal = normalize(view_normal_matrix * v_normal);

    float NdotL = max(dot(normal, LIGHT_TOP_DIR), 0.0);

    intensity.x = INTENSITY_AMBIENT + NdotL * LIGHT_TOP_DIFFUSE;
    vec4 position = view_model_matrix * vec4(v_position, 1.0);
    intensity.y = LIGHT_TOP_SPECULAR * pow(max(dot(-normalize(position.xyz), reflect(-LIGHT_TOP_DIR, normal)), 0.0), LIGHT_TOP_SHININESS);

    NdotL = max(dot(normal, LIGHT_FRONT_DIR), 0.0);
    intensity.x += NdotL * LIGHT_FRONT_DIFFUSE;

    vertex_color = v_extra;
    raw_z_mm = (v_position.z - u_z_base) / max(u_z_scale, 1e-6) + u_z_mean;

    gl_Position = projection_matrix * position;
}
