#version 100

precision highp float;

const vec2 ZERO = vec2(0.0, 0.0);

uniform vec4 uniform_color;

// x = diffuse, y = specular;
varying vec2 intensity;
varying vec2 clipping_planes_dots;

void main()
{
    if (any(lessThan(clipping_planes_dots, ZERO)))
        discard;

    gl_FragColor = vec4(vec3(intensity.y) + uniform_color.rgb * intensity.x, uniform_color.a);
}
