#version 140

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

uniform vec4 uniform_color;
uniform PrintVolumeDetection print_volume;

// x = tainted, y = specular;
in vec2 intensity;
in vec3 world_position;

out vec4 out_color;

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

void main()
{
    vec4 color = select_color(world_position, uniform_color);
    out_color = vec4(vec3(intensity.y) + color.rgb * intensity.x, color.a);
}
