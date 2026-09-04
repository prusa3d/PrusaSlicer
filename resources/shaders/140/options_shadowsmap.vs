#version 150

const float scaling_factor = 1.5;

uniform mat4 projection_view_model_matrix;

uniform samplerBuffer position_tex;
uniform samplerBuffer height_width_angle_tex;
uniform usamplerBuffer segment_index_tex;

in vec3 v_position;

void main()
{
    int id = int(texelFetch(segment_index_tex, gl_InstanceID).r);
    vec2 height_width = texelFetch(height_width_angle_tex, id).xy;
    vec3 offset = texelFetch(position_tex, id).xyz;
    offset.z -= 0.5 * height_width.x;
    height_width *= scaling_factor;
    vec3 final_pos = v_position * vec3(height_width.y, height_width.y, height_width.x) + offset;
    gl_Position = projection_view_model_matrix * vec4(final_pos, 1.0);
}
