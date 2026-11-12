#version 330

uniform vec4 uniform_color;
// normalized material id = id / 255
uniform float material_id;
// Clipping planes, x = min z, y = max z. Used by the SLA preview to clip with a top / bottom plane.
uniform vec2 z_range;

in vec3 eye_normal;
in float world_z;

// eye normal encoded into two floats
layout (location = 0) out vec2 g_eye_normal;
layout (location = 1) out vec4 g_color;

vec2 octahedral_normal_encoding(vec3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    return (n.z >= 0.0) ? n.xy : (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
}

void main()
{
    if (world_z < z_range.x || z_range.y < world_z)
        discard;

    g_eye_normal = octahedral_normal_encoding(eye_normal);
    g_color.rgb = uniform_color.rgb;
    g_color.a = material_id;
}