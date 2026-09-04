#version 330

uniform mat4 projection_view_model_matrix;
uniform mat3 view_normal_matrix;

in vec3 v_position;
in vec3 v_normal;
in vec2 v_tex_coord;

out vec3 eye_normal;
out vec2 tex_coord;

void main()
{
    eye_normal = view_normal_matrix * v_normal;
    tex_coord = v_tex_coord;
    gl_Position = projection_view_model_matrix * vec4(v_position, 1.0);
}
