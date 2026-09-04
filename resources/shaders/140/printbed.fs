#version 140

uniform vec3 back_color_dark;
uniform vec3 back_color_light;

uniform sampler2D in_texture;

in vec2 tex_coord;

out vec4 out_color;

vec4 gradient_color()
{
    // takes foreground from texture
    vec4 fore_color = texture(in_texture, tex_coord);

    // calculates radial gradient
    vec3 back_color = vec3(mix(back_color_light, back_color_dark, smoothstep(0.0, 0.5, length(abs(tex_coord.xy) - vec2(0.5)))));

    // blends foreground with background
    return vec4(mix(back_color, fore_color.rgb, fore_color.a), 0.5 * fore_color.a);
}

void main()
{
    out_color = gradient_color();
}
