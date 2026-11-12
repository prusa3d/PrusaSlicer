#version 330

uniform mat4 model_matrix;
uniform mat4 projection_view_model_matrix;
uniform mat3 view_normal_matrix;

in vec3 v_position;
in vec3 v_normal;

out vec3 eye_normal;
out vec3 world_position;

void main()
{
    eye_normal = view_normal_matrix * v_normal;
    world_position = (model_matrix * vec4(v_position, 1.0)).xyz;
    gl_Position = projection_view_model_matrix * vec4(v_position, 1.0);
}