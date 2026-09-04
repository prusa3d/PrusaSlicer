#version 140

uniform sampler2D in_tex;
uniform int filter_size;

in vec2 tex_coord;

out float out_color;

void main()
{
    vec2 texel_size = 1.0 / vec2(textureSize(in_tex, 0));
    int half_filter_size = filter_size / 2;
    float result = 0.0;
    for (int y = -half_filter_size; y < -half_filter_size + filter_size; ++y) {
        result += texture(in_tex, tex_coord + vec2(0.0, float(y)) * texel_size).r;
    }
    out_color = result / filter_size;
}

