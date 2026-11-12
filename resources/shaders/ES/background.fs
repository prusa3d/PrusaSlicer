#version 100

precision highp float;

uniform vec4 top_color;
uniform vec4 bottom_color;

varying vec2 tex_coord;

float interleaved_gradient_noise(vec2 uv)
{
    vec3 magic = vec3(0.06711056, 0.00583715, 52.9829189);
    return fract(magic.z * fract(dot(uv.xy, magic.xy)));
}


void main()
{
    gl_FragColor = mix(bottom_color, top_color, tex_coord.y) + (interleaved_gradient_noise(tex_coord.xy) - 0.5) / 255.0;
}
