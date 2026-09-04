#version 140

uniform vec4 uniform_color;
// Clipping planes, x = min z, y = max z. Used by the SLA preview to clip with a top / bottom plane.
uniform vec2 z_range;

// x = tainted, y = specular;
in vec2 intensity;
in float world_z;

out vec4 out_color;

void main()
{
    if (world_z < z_range.x || z_range.y < world_z)
        discard;

    out_color = vec4(vec3(intensity.y) + uniform_color.rgb * intensity.x, uniform_color.a);
}
