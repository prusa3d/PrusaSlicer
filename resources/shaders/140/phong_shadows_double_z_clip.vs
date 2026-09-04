#version 140

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;
uniform mat3 view_normal_matrix;
uniform mat4 model_matrix;
uniform mat4 light_matrix;

in vec3 v_position;
in vec3 v_normal;

out vec3 eye_position;
out vec3 eye_normal;
out vec4 light_position;
out float world_z;

void main()
{
    eye_normal = view_normal_matrix * v_normal;
    eye_position = (view_model_matrix * vec4(v_position, 1.0)).xyz;
    light_position = light_matrix * vec4(v_position, 1.0);
    gl_Position = projection_matrix * vec4(eye_position, 1.0);
    world_z = (model_matrix * vec4(v_position, 1.0)).z;
}