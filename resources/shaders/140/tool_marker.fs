#version 140

const vec2 ZERO = vec2(0.0, 0.0);

uniform vec4 uniform_color;

// x = diffuse, y = specular;
in vec2 intensity;
in vec2 clipping_planes_dots;

out vec4 out_color;

void main()
{
    if (any(lessThan(clipping_planes_dots, ZERO)))
        discard;

    out_color = vec4(vec3(intensity.y) + uniform_color.rgb * intensity.x, uniform_color.a);
}
