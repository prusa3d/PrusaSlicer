#version 140

uniform sampler2D in_texture;

in vec2 tex_coord;

out vec4 out_color;

void main()
{
    out_color = texture(in_texture, tex_coord);
}
