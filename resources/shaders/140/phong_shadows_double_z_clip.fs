#version 140

#define MAX_LIGHTS 4

struct Light
{
    int system;
    vec3 direction;
    bool shadows;
    float ambient;
    float diffuse;
    float specular;
    float shininess;
};

uniform float shadows_intensity;
uniform vec4 uniform_color;
uniform int num_lights;
uniform Light lights[MAX_LIGHTS];
uniform int light_shadows_id;
uniform mat4 view_matrix;
// Clipping planes, x = min z, y = max z. Used by the SLA preview to clip with a top / bottom plane.
uniform vec2 z_range;

uniform sampler2D shadowsmap;

in vec3 eye_position;
in vec3 eye_normal;
in vec4 light_position;
in float world_z;

out vec4 out_color;

vec3 light_direction(Light light)
{
    // return light direction in eye coordinates
    return (light.system == 0) ? (view_matrix * vec4(-light.direction, 0.0)).xyz : -light.direction;
}

float shadow_pcf(vec4 position, float NdotL)
{
    // perform perspective divide
    vec3 proj_coords = position.xyz / position.w;
    // transform to [0,1] range
    proj_coords = proj_coords * 0.5 + 0.5;

    if (proj_coords.x < 0.0 || proj_coords.x > 1.0 || proj_coords.y < 0.0 || proj_coords.y > 1.0)
        // Fully lit
        return 1.0;

    float bias = max(0.0025 * (1.0 - NdotL), 0.00025);

    // PCF
    float shadow = 0.0;
    vec2 texel_size = 1.0 / textureSize(shadowsmap, 0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcf_depth = texture(shadowsmap, proj_coords.xy + vec2(x, y) * texel_size).r;
            // Unified test: 
            // If z > 1, use the shadowsmap as a silhouette
            // If z <= 1, use the standard biased depth test
            float compare_val = (proj_coords.z > 1.0) ? 1.0 : proj_coords.z - bias;
            shadow += (compare_val > pcf_depth) ? 1.0 : 0.0;
        }    
    }
    shadow /= 9.0;

    return 1.0 - shadows_intensity * shadow;
}

vec4 lighting_phong(float shadow)
{
    vec3 normal = normalize(eye_normal);

    float ambient = 0.0;
    float diffuse = 0.0;
    float specular = 0.0;
    vec3 v = -normalize(eye_position);
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (i >= num_lights)
            break;
        ambient += lights[i].ambient;
        vec3 dir = light_direction(lights[i]);
        float NdotL = max(dot(normal, dir), 0.0);
        float shadow_factor = (i == light_shadows_id) ? shadow : 1.0;
        diffuse += shadow_factor * lights[i].diffuse * NdotL;
        specular += shadow_factor * lights[i].specular * pow(max(dot(v, reflect(-dir, normal)), 0.0), lights[i].shininess);
    }

    return vec4(uniform_color.rgb * (ambient + diffuse + specular), uniform_color.a);
}

void main()
{
    if (world_z < z_range.x || z_range.y < world_z)
        discard;

    float shadow = lights[light_shadows_id].shadows ?
        shadow_pcf(light_position, max(dot(eye_normal, light_direction(lights[light_shadows_id])), 0.0)) : 1.0;
    out_color = lighting_phong(shadow);
}