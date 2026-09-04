#version 100

precision highp float;

uniform vec3 back_color_dark;
uniform vec3 back_color_light;

uniform sampler2D in_texture;

varying vec2 tex_coord;

vec4 gradient_color()
{
    // takes foreground from texture
    vec4 fore_color = texture2D(in_texture, tex_coord);

    // calculates radial gradient
    vec3 back_color = vec3(mix(back_color_light, back_color_dark, smoothstep(0.0, 0.5, length(abs(tex_coord.xy) - vec2(0.5)))));

    // blends foreground with background
    return vec4(mix(back_color, fore_color.rgb, fore_color.a), 0.5 * fore_color.a);
}

void main()
{
    gl_FragColor = gradient_color();
}