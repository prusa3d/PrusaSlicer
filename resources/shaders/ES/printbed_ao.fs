#version 100

// normalized material id = id / 255
uniform float material_id;
uniform vec3 back_color_dark;
uniform vec3 back_color_light;

uniform sampler2D in_texture;

varying vec3 eye_normal;
varying vec2 tex_coord;

// eye normal encoded into two floats
layout (location = 0) out vec2 g_eye_normal;
layout (location = 1) out vec4 g_color;

vec2 octahedral_normal_encoding(vec3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    return (n.z >= 0.0) ? n.xy : (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
}

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
    g_eye_normal = octahedral_normal_encoding(eye_normal);
    g_color.rgb = gradient_color().rgb;
    g_color.a = material_id;
}