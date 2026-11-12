#version 140

// Clipping planes, x = min z, y = max z. Used by the SLA preview to clip with a top / bottom plane.
uniform vec2 z_range;

in float world_z;

void main()
{
    if (world_z < z_range.x || z_range.y < world_z)
        discard;
}
