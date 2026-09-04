#version 110

uniform mat4 ProjMtx;

attribute vec2 v_position;
attribute vec2 v_tex_coord;
attribute vec4 v_color;

varying vec2 Frag_UV;
varying vec4 Frag_Color;

void main()
{
	Frag_UV = v_tex_coord;
	Frag_Color = v_color;
    gl_Position = ProjMtx * vec4(v_position.xy, 0.0, 1.0);
}