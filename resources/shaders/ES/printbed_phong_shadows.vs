#version 100

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform mat3 view_normal_matrix;
uniform mat4 light_matrix;

attribute vec3 v_position;
attribute vec3 v_normal;
attribute vec2 v_tex_coord;

varying vec3 eye_position;
varying vec3 eye_normal;
varying vec4 light_position;
varying vec2 tex_coord;

void main()
{
    tex_coord = v_tex_coord;
    eye_normal = view_normal_matrix * v_normal;
    eye_position = (view_model_matrix * vec4(v_position, 1.0)).xyz;
    light_position = light_matrix * vec4(v_position, 1.0);
    gl_Position = projection_matrix * vec4(eye_position, 1.0);
}
