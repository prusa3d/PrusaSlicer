#version 140

uniform mat4 projection_view_model_matrix;
uniform mat4 model_matrix;

out float world_z;

in vec3 v_position;

void main()
{
    gl_Position = projection_view_model_matrix * vec4(v_position, 1.0);
    world_z = (model_matrix * vec4(v_position, 1.0)).z;
}
