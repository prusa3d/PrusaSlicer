#version 140

uniform mat4 ProjMtx;

in vec2 v_position;
in vec2 v_tex_coord;
in vec4 v_color;

out vec2 Frag_UV;
out vec4 Frag_Color;

void main()
{
    Frag_UV = v_tex_coord;
    Frag_Color = v_color;
    gl_Position = ProjMtx * vec4(v_position.xy, 0.0, 1.0);
}