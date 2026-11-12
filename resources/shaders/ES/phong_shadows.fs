#version 100

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

struct PrintVolumeDetection
{
    // 0 -> Invalid
    // 1 -> Rectangle
    // 2 -> Circle
    // 3 -> Convex
    // 4 -> Custom
    int type;
    // Invalid/Rectangle/Convex/Custom:
    //   x = min x
    //   y = min y
    //   z = max x
    //   w = max y
    // Circle:
    //   x = center x
    //   y = center y
    //   z = radius
    //   w = not used
    vec4 xy_data;
    // x = min z
    // y = max z
    vec2 z_data;
};

const int TYPE_INVALID = 0;
const int TYPE_RECTANGLE = 1;
const int TYPE_CIRCLE = 2;

uniform float shadows_intensity;
uniform vec4 uniform_color;
uniform int num_lights;
uniform Light lights[MAX_LIGHTS];
uniform int light_shadows_id;
uniform mat4 view_matrix;
uniform PrintVolumeDetection print_volume;

uniform sampler2D shadowsmap;

varying vec3 eye_normal;
varying vec3 eye_position;
varying vec4 light_position;
varying vec3 world_position;

vec3 light_direction(Light light)
{
    // return light direction in eye coordinates
    return (light.system == 0) ? (view_matrix * vec4(-light.direction, 0.0)).xyz : -light.direction;
}

vec4 select_color(vec3 world_position, vec4 color)
{
    // Default to "inside" (1.0)
    float inside = 1.0;

    // 1. Handle Rectangle
    if (print_volume.type == TYPE_RECTANGLE) {
        // Returns 1.0 if inside the bounds, 0.0 if outside
        // we check if world_pos is between min (xy_data.xy) and max (xy_data.zw)
        vec3 s = step(vec3(print_volume.xy_data.x, print_volume.xy_data.y, print_volume.z_data.x), world_position) *
                 step(world_position, vec3(print_volume.xy_data.z, print_volume.xy_data.w, print_volume.z_data.y));
        inside = s.x * s.y * s.z;
    }
    // 2. Handle Circle
    else if (print_volume.type == TYPE_CIRCLE) {
        float d = distance(world_position.xy, print_volume.xy_data.xy);
        float in_circle = step(d, print_volume.xy_data.z);
        float in_z = step(print_volume.z_data.x, world_position.z) * step(world_position.z, print_volume.z_data.y);
        inside = in_circle * in_z;
    }
    // 3. Handle Invalid / Default (Just Z-check)
    else if (print_volume.type != TYPE_INVALID)
        inside = step(print_volume.z_data.x, world_position.z) * step(world_position.z, print_volume.z_data.y);

    // Apply darkening: mix original color with darkened version based on 'inside'
    // if inside == 1.0, it returns color. If inside == 0.0, it returns color * 0.666
    return vec4(mix(color.rgb * 0.6666, color.rgb, inside), color.a);
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

    vec4 color = select_color(world_position, uniform_color);
    return vec4(color.rgb * (ambient + diffuse + specular), color.a);
}

void main()
{
    float shadow = lights[light_shadows_id].shadows ?
        shadow_pcf(light_position, max(dot(eye_normal, light_direction(lights[light_shadows_id])), 0.0)) : 1.0;
    gl_FragColor = lighting_phong(shadow);
}