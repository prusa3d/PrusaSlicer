#version 140

#define MAX_LIGHTS 4
#define MAX_MATERIALS 255
#define PI 3.1415926535897932384626433832795

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

struct Material
{
    float metal;
    float roughness;
    float ior;
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

uniform bool apply_pbr;
uniform float pbr_intensity;
uniform bool apply_shadows;
uniform float shadows_intensity;
uniform int num_lights;
uniform Light lights[MAX_LIGHTS];
uniform int light_shadows_id;
uniform Material materials[MAX_MATERIALS];
uniform PrintVolumeDetection print_volume;
uniform vec2 viewport_size;
uniform mat4 view_matrix;
uniform mat4 inverse_projection_matrix;
uniform mat4 inverse_projection_view_matrix;
uniform mat4 light_matrix;
uniform float ambient_intensity;

uniform sampler2D g_depth;
uniform sampler2D g_eye_normal;
uniform sampler2D g_color;
uniform sampler2D ssao;
uniform sampler2D shadowsmap;

in vec2 tex_coord;

out vec4 out_color;

vec3 world_position_from_depth(vec2 uv, float depth)
{
    vec4 world = inverse_projection_view_matrix * vec4(vec3(uv, depth) * 2.0 - vec3(1.0), 1.0);
    return world.xyz / world.w;
}

vec3 octahedral_normal_decoding(vec2 f)
{
    vec3 n = vec3(f.xy, 1.0 - abs(f.x) - abs(f.y));
    vec2 cond = step(n.z, vec2(0.0));
    n.xy = mix(n.xy, (1.0 - abs(n.yx)) * sign(n.xy), cond.x);
    return normalize(n);
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

vec3 light_direction(Light light)
{
    // return light direction in eye coordinates
    return (light.system == 0) ? (view_matrix * vec4(-light.direction, 0.0)).xyz : -light.direction;
}

vec3 lighting_phong(vec3 in_color, float depth, vec3 eye_position, vec3 eye_normal, float shadow, float ao)
{
    float ambient = 0.0;
    float diffuse = 0.0;
    float specular = 0.0;
    vec3 v = -normalize(eye_position);
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (i >= num_lights)
            break;
        ambient += lights[i].ambient;
        vec3 dir = light_direction(lights[i]);
        float NdotL = max(dot(eye_normal, dir), 0.0);
        float shadow_factor = (i == light_shadows_id) ? shadow : 1.0;
        diffuse += shadow_factor * lights[i].diffuse * NdotL;
        specular += shadow_factor * lights[i].specular * pow(max(dot(v, reflect(-dir, eye_normal)), 0.0), lights[i].shininess);
    }

    return vec3(in_color * (ambient * ao + diffuse + specular));
}

float ior_to_f0(float ior)
{
    float num = ior - 1.0f;
    float denom = ior + 1.0f;
    return num * num / (denom * denom);
}

float distribution_ggx(vec3 n, vec3 h, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(n, h), 0.0);
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float geometry_schlick_ggx(float cos_theta, float roughness)
{
    float r = roughness + 1.0;
    float k = r * r / 8.0;
    return cos_theta / (cos_theta * (1.0 - k) + k);
}

float geometry_smith(vec3 n, vec3 v, vec3 l, float roughness)
{
    float ggx1 = geometry_schlick_ggx(max(dot(n, v), 0.0), roughness);
    float ggx2 = geometry_schlick_ggx(max(dot(n, l), 0.0), roughness);
    return ggx1 * ggx2;
}

vec3 fresnel_schlick(float cos_theta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

vec3 light_radiance(vec3 F0, vec3 v, vec3 n, vec3 l, float diffuse, Material material, vec3 color)
{
    vec3 h = normalize(v + l);
    vec3 radiance = vec3(diffuse);

    // Cook-Torrance BRDF
    float NDF = distribution_ggx(n, h, material.roughness);
    float G = geometry_smith(n, v, l, material.roughness);
    vec3 F = fresnel_schlick(clamp(dot(h, v), 0.0, 1.0), F0);

    float NdotL = max(dot(n, l), 0.0);
    float denom = 4.0 * max(dot(n, v), 0.0) * NdotL + 0.0001;
    vec3 specular = NDF * G * F / denom;

    // energy conservation
    vec3 kD = vec3(1.0) - F;
    // multiply kD by the inverse metalness such that only non-metals 
    // have diffuse lighting, or a linear blend if partly metal (pure metals
    // have no diffuse light).
    kD *= 1.0 - material.metal;

    // note that we already multiplied the BRDF by the Fresnel (kS) so we won't multiply by kS again
    return (kD * color / PI + specular) * radiance * NdotL;
}

vec3 lighting_pbr(vec3 in_color, float depth, vec3 eye_position, vec3 eye_normal, int material_id, float shadow, float ao)
{
    Material material = materials[material_id];
    vec3 color = pow(in_color, vec3(2.2));
    vec3 F0 = mix(vec3(ior_to_f0(material.ior)), color, material.metal);
    vec3 v = -normalize(eye_position);
    vec3 lo = vec3(0.0);
    for (int i = 0; i < MAX_LIGHTS; ++i) {
        if (i >= num_lights)
            break;
        vec3 dir = light_direction(lights[i]);
        float shadow_factor = (i == light_shadows_id) ? shadow : 1.0;
        lo += shadow_factor * light_radiance(F0, v, eye_normal, dir, pbr_intensity * lights[i].diffuse, material, color);
    }

    vec3 ambient = vec3(ambient_intensity) * color;
    vec3 pbr_color = (ambient + lo) * ao;

    // HDR tonemapping
    pbr_color = pbr_color / (pbr_color + vec3(1.0));
//    // gamma correct
//    pbr_color = pow(pbr_color, vec3(1.0/2.2));

    return pbr_color;
}

void main()
{
    vec4 color = texture(g_color, tex_coord);
    float depth = texture(g_depth, tex_coord).r;
    if (depth >= 1.0)
        discard;

    vec3 eye_normal = octahedral_normal_decoding(texture(g_eye_normal, tex_coord).xy);
    int material_id = int(color.w * MAX_MATERIALS + 0.5);

    vec3 world_position = world_position_from_depth(tex_coord, depth);
    vec3 eye_position = (view_matrix * vec4(world_position, 1.0)).xyz;
    vec4 light_position = light_matrix * vec4(world_position, 1.0);

    float shadow = (apply_shadows && lights[light_shadows_id].shadows) ?
        shadow_pcf(light_position, max(dot(eye_normal, light_direction(lights[light_shadows_id])), 0.0)) : 1.0;

    float ao = texture(ssao, tex_coord).r;

    color.rgb = apply_pbr ? lighting_pbr(color.rgb, depth, eye_position, eye_normal, material_id, shadow, ao) :
        lighting_phong(color.rgb, depth, eye_position, eye_normal, shadow, ao);
    out_color = select_color(world_position, vec4(color.rgb, 1.0));
}
