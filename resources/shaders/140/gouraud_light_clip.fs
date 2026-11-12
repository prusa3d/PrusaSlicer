#version 140

uniform vec4 uniform_color;

// x = tainted, y = specular;
in vec2 intensity;
in float clipping_planes_dot;

out vec4 out_color;

void main()
{
    if (clipping_planes_dot < 0.0)
        discard;

    out_color = vec4(vec3(intensity.y) + uniform_color.rgb * intensity.x, uniform_color.a);
}