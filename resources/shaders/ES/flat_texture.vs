#version 100

uniform mat4 projection_view_model_matrix;

attribute vec3 v_position;
attribute vec2 v_tex_coord;

varying vec2 tex_coord;

void main()
{
    tex_coord = v_tex_coord;
    gl_Position = projection_view_model_matrix * vec4(v_position, 1.0);
}
